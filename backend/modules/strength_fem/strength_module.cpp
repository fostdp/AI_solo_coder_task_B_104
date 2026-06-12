#include "modules/strength_fem/strength_module.h"
#include "modules/common/module_factory.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <condition_variable>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

struct StrengthIpcResponse {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false;
    BendingTestResult result;
};

static std::mutex g_strength_ipc_mutex;
static std::unordered_map<int64_t, std::shared_ptr<StrengthIpcResponse>> g_strength_pending;

StrengthFemModule::StrengthFemModule() = default;
StrengthFemModule::~StrengthFemModule() { shutdown(); }

bool StrengthFemModule::initialize(const json& config) {
    try {
        if (config.contains("external_process")) {
            use_external_process_ = config["external_process"].get<bool>();
        }
        if (config.contains("bending_config")) {
            const auto& bc = config["bending_config"];
            if (bc.contains("support_span_mm"))
                bending_config_.support_span_mm = bc["support_span_mm"].get<double>();
            if (bc.contains("specimen_thickness_mm"))
                bending_config_.specimen_thickness_mm = bc["specimen_thickness_mm"].get<double>();
            if (bc.contains("porcelain_strength_mpa"))
                bending_config_.porcelain_strength_mpa = bc["porcelain_strength_mpa"].get<double>();
            if (bc.contains("repair_interface_strength_ratio"))
                bending_config_.repair_interface_strength_ratio = bc["repair_interface_strength_ratio"].get<double>();
            if (bc.contains("enable_bayesian_calibration"))
                bending_config_.enable_bayesian_calibration = bc["enable_bayesian_calibration"].get<bool>();
            if (bc.contains("calibration_max_iter"))
                bending_config_.calibration_max_iter = bc["calibration_max_iter"].get<int>();
        }

        size_t pool_threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        compute_pool_ = std::make_unique<ThreadPool>(pool_threads);

        adapter_ = std::make_unique<StrengthDealIIAdapter>();
        adapter_->initialize(bending_config_);
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

bool StrengthFemModule::initialize_external_process() {
    ipc_channel_ = IpcChannelFactory::create(IpcType::LOCAL);
    bool ok = ipc_channel_->initialize("strength_fem_client");
    if (ok) {
        ipc_channel_->set_message_callback([this](const IpcMessage& msg) {
            std::lock_guard<std::mutex> lock(g_strength_ipc_mutex);
            auto it = g_strength_pending.find(msg.request_id);
            if (it != g_strength_pending.end()) {
                auto state = it->second;
                std::lock_guard<std::mutex> sl(state->mutex);
                BendingTestResult r{};
                r.success = true;
                r.porcelain_id = msg.payload.value("porcelain_id", 0);
                r.crack_id = msg.payload.value("crack_id", 0);
                r.material_id = msg.payload.value("material_id", 0);
                r.original_strength_mpa = msg.payload.value("original_strength_mpa", 0.0);
                r.unrepaired_strength_mpa = msg.payload.value("unrepaired_strength_mpa", 0.0);
                r.repaired_strength_mpa = msg.payload.value("repaired_strength_mpa", 0.0);
                r.strength_recovery_ratio = msg.payload.value("strength_recovery_ratio", 0.0);
                r.youngs_modulus_gpa = msg.payload.value("youngs_modulus_gpa", 0.0);
                r.fracture_toughness_mpa_m05 = msg.payload.value("fracture_toughness_mpa_m05", 0.0);
                state->result = r;
                state->ready = true;
                state->cv.notify_all();
                g_strength_pending.erase(it);
            }
        });
    }
    return ok;
}

bool StrengthFemModule::execute(const json& input, ResultCallback callback) {
    if (status_ == ModuleStatus::RUNNING) return false;
    cancelled_ = false;
    status_ = ModuleStatus::RUNNING;

    worker_thread_ = std::make_unique<std::thread>(
        &StrengthFemModule::run_async, this, input, callback
    );
    return true;
}

void StrengthFemModule::run_async(const json& input, ResultCallback callback) {
    ModuleResult result;
    result.success = false;
    result.error_message = "";
    try {
        auto r = compute_internal(input);
        result.data = result_to_json(r);
        result.success = r.success;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    status_ = cancelled_ ? ModuleStatus::IDLE : ModuleStatus::COMPLETED;
    if (callback) callback(result);
}

BendingTestResult StrengthFemModule::compute_internal(const json& input) {
    CrackInfo crack = {};
    RepairMaterial material = {};
    bool repaired = true;

    if (input.contains("porcelain_id")) {
        crack.porcelain_id = input["porcelain_id"].get<int>();
    }
    if (input.contains("crack")) {
        const auto& c = input["crack"];
        crack.id = c.value("id", 0);
        crack.max_depth = c.value("max_depth", 50.0);
        crack.max_width = c.value("max_width", 10.0);
    }
    if (input.contains("material")) {
        const auto& m = input["material"];
        material.id = m.value("id", 0);
        material.name = m.value("name", std::string("default"));
        material.viscosity = m.value("viscosity", 1.0);
    }
    if (input.contains("repaired")) {
        repaired = input["repaired"].get<bool>();
    }

    return dispatch_solve(crack, material, repaired);
}

BendingTestResult StrengthFemModule::dispatch_solve(
    const CrackInfo& crack,
    const RepairMaterial& material,
    bool repaired)
{
    if (use_external_process_ && ipc_channel_ && ipc_channel_->is_connected()) {
        json payload;
        payload["porcelain_id"] = crack.porcelain_id;
        payload["crack"] = {
            {"id", crack.id}, {"max_depth", crack.max_depth}, {"max_width", crack.max_width}
        };
        payload["material"] = {
            {"id", material.id}, {"name", material.name}, {"viscosity", material.viscosity}
        };
        payload["repaired"] = repaired;
        BendingTestResult r = solve_via_ipc(payload);
        if (r.success) return r;
    }
    return adapter_->solve(crack, material, repaired);
}

BendingTestResult StrengthFemModule::solve_via_ipc(const json& input) {
    BendingTestResult result;
    result.success = false;
    int64_t req_id = next_request_id_++;
    auto state = std::make_shared<StrengthIpcResponse>();
    {
        std::lock_guard<std::mutex> lock(g_strength_ipc_mutex);
        g_strength_pending[req_id] = state;
    }

    IpcMessage msg;
    msg.module_name = "strength_fem";
    msg.command = "simulate";
    msg.payload = input;
    msg.request_id = req_id;
    msg.timestamp = std::chrono::system_clock::now();

    if (!ipc_channel_->send(msg)) {
        std::lock_guard<std::mutex> lock(g_strength_ipc_mutex);
        g_strength_pending.erase(req_id);
        return result;
    }

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        bool ok = state->cv.wait_for(lock, std::chrono::seconds(30),
                                      [&state] { return state->ready; });
        if (ok) result = state->result;
    }
    return result;
}

bool StrengthFemModule::execute_sync(const json& input, ModuleResult& result) {
    if (status_ == ModuleStatus::RUNNING) return false;
    status_ = ModuleStatus::RUNNING;
    cancelled_ = false;
    try {
        auto r = compute_internal(input);
        result.data = result_to_json(r);
        result.success = r.success;
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

json StrengthFemModule::result_to_json(const BendingTestResult& r) const {
    json j;
    j["porcelain_id"] = r.porcelain_id;
    j["crack_id"] = r.crack_id;
    j["material_id"] = r.material_id;
    j["original_strength_mpa"] = r.original_strength_mpa;
    j["unrepaired_strength_mpa"] = r.unrepaired_strength_mpa;
    j["repaired_strength_mpa"] = r.repaired_strength_mpa;
    j["strength_recovery_ratio"] = r.strength_recovery_ratio;
    j["youngs_modulus_gpa"] = r.youngs_modulus_gpa;
    j["fracture_toughness_mpa_m05"] = r.fracture_toughness_mpa_m05;
    return j;
}

BendingTestResult StrengthFemModule::simulate_bending_sync(
    const CrackInfo& crack,
    const RepairMaterial& material,
    bool repaired)
{
    return adapter_ ? adapter_->solve(crack, material, repaired) : BendingTestResult{};
}

algorithms::FourPointBendingTest::CalibrationResult StrengthFemModule::calibrate(
    const std::vector<algorithms::CalibrationDataset>& dataset,
    bool update_params)
{
    algorithms::FourPointBendingTest solver;
    solver.set_config(bending_config_);
    return solver.calibrate_model(dataset, update_params);
}

std::vector<std::future<BendingTestResult>> StrengthFemModule::simulate_batch_parallel(
    const std::vector<std::tuple<CrackInfo, RepairMaterial, bool>>& batch)
{
    std::vector<std::future<BendingTestResult>> futures;
    futures.reserve(batch.size());
    if (!adapter_) return futures;

    for (size_t i = 0; i < batch.size(); ++i) {
        futures.push_back(compute_pool_->submit(
            [this, i, &batch]() -> BendingTestResult {
                return adapter_->solve(
                    std::get<0>(batch[i]),
                    std::get<1>(batch[i]),
                    std::get<2>(batch[i]));
            }
        ));
    }
    return futures;
}

bool StrengthFemModule::cancel() {
    cancelled_ = true;
    status_ = ModuleStatus::IDLE;
    return true;
}

bool StrengthFemModule::shutdown() {
    cancel();
    if (worker_thread_ && worker_thread_->joinable()) worker_thread_->join();
    worker_thread_.reset();
    if (compute_pool_) compute_pool_->shutdown();
    compute_pool_.reset();
    adapter_.reset();
    if (ipc_channel_) ipc_channel_->close();
    ipc_channel_.reset();
    status_ = ModuleStatus::UNINITIALIZED;
    return true;
}

REGISTER_MODULE(StrengthFemModule, "strength_fem")

}
}
