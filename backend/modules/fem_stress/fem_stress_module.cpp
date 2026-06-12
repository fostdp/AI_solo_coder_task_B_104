#include "modules/fem_stress/fem_stress_module.h"
#include "modules/common/module_factory.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <condition_variable>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

struct IpcResponseState {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    StressAnalysisResultData result;
};

static std::mutex g_ipc_response_mutex;
static std::unordered_map<int64_t, std::shared_ptr<IpcResponseState>> g_pending_responses;

FemStressModule::FemStressModule() = default;

FemStressModule::~FemStressModule() {
    shutdown();
}

bool FemStressModule::initialize(const json& config) {
    try {
        if (config.contains("external_process")) {
            use_external_process_ = config["external_process"].get<bool>();
        }
        if (config.contains("fem_config")) {
            const auto& fc = config["fem_config"];
            if (fc.contains("grid_resolution"))
                fem_config_.grid_resolution = fc["grid_resolution"].get<int>();
            if (fc.contains("youngs_modulus_gpa"))
                fem_config_.youngs_modulus_gpa = fc["youngs_modulus_gpa"].get<double>();
            if (fc.contains("poissons_ratio"))
                fem_config_.poissons_ratio = fc["poissons_ratio"].get<double>();
            if (fc.contains("use_adaptive_mesh"))
                fem_config_.use_adaptive_mesh = fc["use_adaptive_mesh"].get<bool>();
            if (fc.contains("max_refinement_level"))
                fem_config_.max_refinement_level = fc["max_refinement_level"].get<int>();
        }

        size_t pool_threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        if (config.contains("compute_threads")) {
            pool_threads = static_cast<size_t>(config["compute_threads"].get<int>());
        }
        compute_pool_ = std::make_unique<ThreadPool>(pool_threads);

        adapter_ = std::make_unique<DealIIAdapter>();
        adapter_->initialize(fem_config_);
        adapter_->set_external_process(use_external_process_);

        if (use_external_process_) {
            initialize_external_process();
        }

        status_ = ModuleStatus::IDLE;
        return true;
    } catch (const std::exception& e) {
        status_ = ModuleStatus::ERROR;
        return false;
    }
}

bool FemStressModule::initialize_external_process() {
    ipc_channel_ = IpcChannelFactory::create(IpcType::LOCAL);
    bool ok = ipc_channel_->initialize("fem_stress_client");
    if (ok) {
        ipc_channel_->set_message_callback([this](const IpcMessage& msg) {
            std::lock_guard<std::mutex> lock(g_ipc_response_mutex);
            auto it = g_pending_responses.find(msg.request_id);
            if (it != g_pending_responses.end()) {
                auto state = it->second;
                std::lock_guard<std::mutex> sl(state->mutex);
                StressAnalysisResultData r;
                r.success = msg.payload.value("success", false);
                r.total_nodes = msg.payload.value("total_nodes", 0u);
                r.total_elements = msg.payload.value("total_elements", 0u);
                r.max_von_mises_mpa = msg.payload.value("max_von_mises_mpa", 0.0);
                r.min_von_mises_mpa = msg.payload.value("min_von_mises_mpa", 0.0);
                r.average_stress_mpa = msg.payload.value("average_stress_mpa", 0.0);
                r.stress_concentration_factor = msg.payload.value("stress_concentration_factor", 0.0);
                r.solve_time_ms = msg.payload.value("solve_time_ms", 0.0);
                r.solver_backend = msg.payload.value("solver_backend", "external");
                state->result = r;
                state->ready = true;
                state->cv.notify_all();
                g_pending_responses.erase(it);
            }
        });
    }
    return ok;
}

bool FemStressModule::execute(const json& input, ResultCallback callback) {
    if (status_ == ModuleStatus::RUNNING) return false;
    cancelled_ = false;
    status_ = ModuleStatus::RUNNING;

    worker_thread_ = std::make_unique<std::thread>(
        &FemStressModule::run_async, this, input, callback
    );
    return true;
}

void FemStressModule::run_async(const json& input, ResultCallback callback) {
    ModuleResult result;
    result.success = false;
    result.error_message = "";

    try {
        StressAnalysisResultData stress_result = compute_internal(input);
        result.data = result_to_json(stress_result);
        result.success = stress_result.success;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    status_ = cancelled_ ? ModuleStatus::IDLE : ModuleStatus::COMPLETED;
    if (callback) callback(result);
}

StressAnalysisResultData FemStressModule::compute_internal(const json& input) {
    std::vector<CrackData> cracks;
    GlazeDimensions dims;

    if (input.contains("cracks")) {
        for (const auto& c : input["cracks"]) {
            CrackData cd;
            cd.id = c.value("id", 0);
            cd.start_x = c.value("start_x", 0.0);
            cd.start_y = c.value("start_y", 0.0);
            cd.end_x = c.value("end_x", 0.0);
            cd.end_y = c.value("end_y", 0.0);
            cd.depth_um = c.value("depth_um", 10.0);
            cd.width_um = c.value("width_um", 1.0);
            cd.length_mm = std::sqrt(std::pow(cd.end_x - cd.start_x, 2) +
                                    std::pow(cd.end_y - cd.start_y, 2));
            cracks.push_back(cd);
        }
    }
    if (input.contains("dimensions")) {
        const auto& d = input["dimensions"];
        dims.thickness_mm = d.value("thickness_mm", 3.0);
        dims.diameter_mm = d.value("diameter_mm", 100.0);
    }

    return dispatch_solve(cracks, dims);
}

StressAnalysisResultData FemStressModule::dispatch_solve(
    const std::vector<CrackData>& cracks,
    const GlazeDimensions& dims)
{
    if (use_external_process_ && ipc_channel_ && ipc_channel_->is_connected()) {
        json payload;
        payload["cracks"] = json::array();
        for (const auto& c : cracks) {
            payload["cracks"].push_back({
                {"id", c.id}, {"start_x", c.start_x}, {"start_y", c.start_y},
                {"end_x", c.end_x}, {"end_y", c.end_y},
                {"depth_um", c.depth_um}, {"width_um", c.width_um}
            });
        }
        payload["dimensions"] = {
            {"thickness_mm", dims.thickness_mm}, {"diameter_mm", dims.diameter_mm}
        };
        StressAnalysisResultData ipc_res = solve_via_ipc(payload);
        if (ipc_res.success) return ipc_res;
    }

    auto result = adapter_->solve(cracks, dims);
    result.solver_backend = (adapter_->get_backend() == FemSolverBackend::DEAL_II) ? "dealii" : "builtin";
    return result;
}

StressAnalysisResultData FemStressModule::solve_via_ipc(const json& input) {
    StressAnalysisResultData result;
    result.success = false;

    int64_t req_id = next_request_id_++;
    auto state = std::make_shared<IpcResponseState>();
    {
        std::lock_guard<std::mutex> lock(g_ipc_response_mutex);
        g_pending_responses[req_id] = state;
    }

    IpcMessage msg;
    msg.module_name = "fem_stress";
    msg.command = "analyze";
    msg.payload = input;
    msg.request_id = req_id;
    msg.timestamp = std::chrono::system_clock::now();

    if (!ipc_channel_->send(msg)) {
        std::lock_guard<std::mutex> lock(g_ipc_response_mutex);
        g_pending_responses.erase(req_id);
        return result;
    }

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        bool received = state->cv.wait_for(lock, std::chrono::seconds(30),
                                            [&state] { return state->ready; });
        if (received) result = state->result;
    }
    return result;
}

bool FemStressModule::execute_sync(const json& input, ModuleResult& result) {
    if (status_ == ModuleStatus::RUNNING) return false;
    status_ = ModuleStatus::RUNNING;
    cancelled_ = false;

    try {
        StressAnalysisResultData stress_result = compute_internal(input);
        result.data = result_to_json(stress_result);
        result.success = stress_result.success;
        result.error_message = "";
        status_ = ModuleStatus::COMPLETED;
        return true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
        result.success = false;
        status_ = ModuleStatus::ERROR;
        return false;
    }
}

json FemStressModule::result_to_json(const StressAnalysisResultData& r) const {
    json j;
    j["success"] = r.success;
    j["total_nodes"] = r.total_nodes;
    j["total_elements"] = r.total_elements;
    j["max_von_mises_mpa"] = r.max_von_mises_mpa;
    j["min_von_mises_mpa"] = r.min_von_mises_mpa;
    j["average_stress_mpa"] = r.average_stress_mpa;
    j["stress_concentration_factor"] = r.stress_concentration_factor;
    j["solve_time_ms"] = r.solve_time_ms;
    j["solver_backend"] = r.solver_backend;

    json points = json::array();
    size_t stride = std::max(static_cast<size_t>(1), r.stress_grid.size() / 1000);
    for (size_t i = 0; i < r.stress_grid.size(); i += stride) {
        const auto& p = r.stress_grid[i];
        points.push_back({
            {"x_mm", p.x_mm}, {"y_mm", p.y_mm}, {"z_mm", p.z_mm},
            {"von_mises_mpa", p.von_mises_mpa}, {"refinement_level", p.refinement_level}
        });
    }
    j["stress_grid_sample"] = points;
    return j;
}

StressAnalysisResultData FemStressModule::analyze_stress_sync(
    const config::StressFEMConfig& config,
    const std::vector<CrackData>& cracks,
    const GlazeDimensions& dims)
{
    StressAnalysisResultData result;
    result.success = false;

    if (!adapter_) return result;
    if (fem_config_.grid_resolution != config.grid_resolution ||
        fem_config_.youngs_modulus_gpa != config.youngs_modulus_gpa) {
        fem_config_ = config;
        adapter_->initialize(config);
    }

    return dispatch_solve(cracks, dims);
}

std::vector<std::future<StressAnalysisResultData>> FemStressModule::analyze_batch_parallel(
    const std::vector<std::pair<std::vector<CrackData>, GlazeDimensions>>& batch,
    const config::StressFEMConfig& config)
{
    std::vector<std::future<StressAnalysisResultData>> futures;
    futures.reserve(batch.size());

    if (!adapter_) return futures;
    if (fem_config_.grid_resolution != config.grid_resolution) {
        fem_config_ = config;
        adapter_->initialize(config);
    }

    for (size_t i = 0; i < batch.size(); ++i) {
        futures.push_back(compute_pool_->submit(
            [this, i, &batch]() -> StressAnalysisResultData {
                return adapter_->solve(batch[i].first, batch[i].second);
            }
        ));
    }
    return futures;
}

bool FemStressModule::cancel() {
    cancelled_ = true;
    status_ = ModuleStatus::IDLE;
    return true;
}

bool FemStressModule::shutdown() {
    cancel();
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    worker_thread_.reset();
    if (compute_pool_) compute_pool_->shutdown();
    compute_pool_.reset();
    adapter_.reset();
    if (ipc_channel_) ipc_channel_->close();
    ipc_channel_.reset();
    status_ = ModuleStatus::UNINITIALIZED;
    return true;
}

REGISTER_MODULE(FemStressModule, "fem_stress")

}
}
