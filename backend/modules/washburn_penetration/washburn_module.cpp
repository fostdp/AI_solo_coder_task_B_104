#include "modules/washburn_penetration/washburn_module.h"
#include "modules/common/module_factory.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <algorithm>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

static thread_local size_t tls_thread_index = 0;

WashburnPenetrationModule::WashburnPenetrationModule(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::max(1u, std::thread::hardware_concurrency());
    }
    thread_pool_ = std::make_unique<ThreadPool>(num_threads);
    per_thread_models_.reserve(num_threads);
}

WashburnPenetrationModule::~WashburnPenetrationModule() {
    shutdown();
}

bool WashburnPenetrationModule::initialize(const json& config) {
    try {
        if (config.contains("washburn_config")) {
            const auto& wc = config["washburn_config"];
            if (wc.contains("default_surface_tension_n_m"))
                washburn_config_.default_surface_tension_n_m = wc["default_surface_tension_n_m"].get<double>();
            if (wc.contains("default_contact_angle_deg"))
                washburn_config_.default_contact_angle_deg = wc["default_contact_angle_deg"].get<double>();
            if (wc.contains("default_viscosity_pa_s"))
                washburn_config_.default_viscosity_pa_s = wc["default_viscosity_pa_s"].get<double>();
            if (wc.contains("tortuosity_factor"))
                washburn_config_.tortuosity_factor = wc["tortuosity_factor"].get<double>();
            if (wc.contains("wall_roughness_ra_um"))
                washburn_config_.wall_roughness_ra_um = wc["wall_roughness_ra_um"].get<double>();
            if (wc.contains("wenzel_roughness_correction"))
                washburn_config_.wenzel_roughness_correction = wc["wenzel_roughness_correction"].get<bool>();
        }

        {
            std::lock_guard<std::mutex> lock(models_mutex_);
            per_thread_models_.clear();
            size_t n = thread_pool_ ? thread_pool_->num_threads() : 1;
            for (size_t i = 0; i < n; ++i) {
                auto m = std::make_unique<algorithms::WashburnPenetrationModel>();
                m->set_config(washburn_config_);
                per_thread_models_.push_back(std::move(m));
            }
        }

        status_ = ModuleStatus::IDLE;
        return true;
    } catch (const std::exception& e) {
        status_ = ModuleStatus::ERROR;
        return false;
    }
}

size_t WashburnPenetrationModule::active_tasks() const {
    return thread_pool_ ? thread_pool_->pending_tasks() : 0;
}

void WashburnPenetrationModule::wait_all() {
    if (thread_pool_) thread_pool_->wait_all();
}

PenetrationPrediction WashburnPenetrationModule::safe_predict_single(
    const CrackInfo& crack,
    const RepairMaterial& material,
    double target_depth_um)
{
    size_t idx = 0;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        idx = tls_thread_index % std::max<size_t>(1, per_thread_models_.size());
        tls_thread_index = (tls_thread_index + 1) % per_thread_models_.size();
    }
    algorithms::WashburnPenetrationModel* model = nullptr;
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        if (idx < per_thread_models_.size() && per_thread_models_[idx]) {
            model = per_thread_models_[idx].get();
        }
    }
    if (!model) {
        PenetrationPrediction p{};
        p.crack_id = crack.id;
        p.material_id = material.id;
        p.success = false;
        return p;
    }
    return model->predict(static_cast<int>(crack.id), material.id, crack, material, target_depth_um);
}

PenetrationPrediction WashburnPenetrationModule::predict_single(
    const CrackInfo& crack,
    const RepairMaterial& material,
    double target_depth_um)
{
    return safe_predict_single(crack, material, target_depth_um);
}

std::vector<PenetrationPrediction> WashburnPenetrationModule::predict_batch(
    const std::vector<CrackInfo>& cracks,
    const RepairMaterial& material,
    double target_depth_um)
{
    if (cracks.empty()) return {};

    std::vector<std::future<PenetrationPrediction>> futures;
    futures.reserve(cracks.size());

    for (size_t i = 0; i < cracks.size(); ++i) {
        futures.push_back(thread_pool_->submit(
            [this, &cracks, &material, target_depth_um, i]() -> PenetrationPrediction {
                if (cancelled_) {
                    PenetrationPrediction p{};
                    p.crack_id = cracks[i].id;
                    p.material_id = material.id;
                    return p;
                }
                return safe_predict_single(cracks[i], material, target_depth_um);
            }
        ));
    }

    std::vector<PenetrationPrediction> results;
    results.reserve(futures.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }
    return results;
}

std::vector<std::future<PenetrationPrediction>> WashburnPenetrationModule::predict_batch_async(
    const std::vector<CrackInfo>& cracks,
    const RepairMaterial& material,
    double target_depth_um)
{
    std::vector<std::future<PenetrationPrediction>> futures;
    futures.reserve(cracks.size());
    for (size_t i = 0; i < cracks.size(); ++i) {
        futures.push_back(thread_pool_->submit(
            [this, &cracks, &material, target_depth_um, i]() -> PenetrationPrediction {
                return safe_predict_single(cracks[i], material, target_depth_um);
            }
        ));
    }
    return futures;
}

std::vector<PenetrationPrediction> WashburnPenetrationModule::predict_chunked(
    const std::vector<CrackInfo>& cracks,
    const RepairMaterial& material,
    double target_depth_um,
    const WashburnBatchOptions& options)
{
    if (cracks.empty()) return {};

    size_t num_workers = thread_pool_ ? thread_pool_->num_threads() : 1;
    if (options.max_parallel > 0) num_workers = std::min(num_workers, options.max_parallel);
    size_t chunk_size = std::max(options.min_batch_per_thread,
                                 (cracks.size() + num_workers - 1) / num_workers);
    size_t num_chunks = (cracks.size() + chunk_size - 1) / chunk_size;

    std::vector<std::future<std::vector<PenetrationPrediction>>> chunk_futures;
    chunk_futures.reserve(num_chunks);

    for (size_t c = 0; c < num_chunks; ++c) {
        size_t start = c * chunk_size;
        size_t end = std::min(start + chunk_size, cracks.size());
        chunk_futures.push_back(thread_pool_->submit(
            [this, &cracks, &material, target_depth_um, start, end, &options]()
                -> std::vector<PenetrationPrediction>
            {
                std::vector<PenetrationPrediction> chunk_results(end - start);
                for (size_t i = start; i < end; ++i) {
                    if (options.enable_cancellation && cancelled_) break;
                    chunk_results[i - start] = safe_predict_single(
                        cracks[i], material, target_depth_um);
                }
                return chunk_results;
            }
        ));
    }

    std::vector<PenetrationPrediction> results;
    results.reserve(cracks.size());
    for (auto& f : chunk_futures) {
        auto chunk = f.get();
        results.insert(results.end(), chunk.begin(), chunk.end());
    }
    return results;
}

bool WashburnPenetrationModule::execute(const json& input, ResultCallback callback) {
    if (status_ == ModuleStatus::RUNNING) return false;
    cancelled_ = false;
    status_ = ModuleStatus::RUNNING;

    std::vector<CrackInfo> cracks;
    RepairMaterial material;
    double target_depth_um = 50.0;

    try {
        if (input.contains("cracks")) {
            for (const auto& c : input["cracks"]) {
                CrackInfo crack{};
                crack.id = c.value("id", 0);
                crack.max_width = c.value("max_width", 10.0);
                crack.max_depth = c.value("max_depth", 50.0);
                crack.total_length = c.value("total_length", 1.0);
                cracks.push_back(crack);
            }
        }
        if (input.contains("material")) {
            const auto& m = input["material"];
            material.id = m.value("id", 0);
            material.name = m.value("name", std::string("default"));
            material.viscosity = m.value("viscosity", 1.0);
            material.type = RepairMaterialType::SILICA;
        }
        if (input.contains("target_depth_um")) {
            target_depth_um = input["target_depth_um"].get<double>();
        }
    } catch (const std::exception& e) {
        status_ = ModuleStatus::ERROR;
        return false;
    }

    std::thread([this, cracks, material, target_depth_um, callback]() {
        run_async_batch(cracks, material, target_depth_um, callback);
    }).detach();

    return true;
}

void WashburnPenetrationModule::run_async_batch(
    const std::vector<CrackInfo>& cracks,
    const RepairMaterial& material,
    double target_depth_um,
    ResultCallback callback)
{
    ModuleResult result;
    result.success = false;
    result.error_message = "";

    try {
        auto predictions = predict_chunked(cracks, material, target_depth_um);
        result.data = result_to_json(predictions);
        result.success = true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }

    status_ = cancelled_ ? ModuleStatus::IDLE : ModuleStatus::COMPLETED;
    if (callback) callback(result);
}

bool WashburnPenetrationModule::execute_sync(const json& input, ModuleResult& result) {
    if (status_ == ModuleStatus::RUNNING) return false;
    status_ = ModuleStatus::RUNNING;
    cancelled_ = false;

    try {
        std::vector<CrackInfo> cracks;
        RepairMaterial material;
        double target_depth_um = 50.0;

        if (input.contains("cracks")) {
            for (const auto& c : input["cracks"]) {
                CrackInfo crack{};
                crack.id = c.value("id", 0);
                crack.max_width = c.value("max_width", 10.0);
                crack.max_depth = c.value("max_depth", 50.0);
                crack.total_length = c.value("total_length", 1.0);
                cracks.push_back(crack);
            }
        }
        if (input.contains("material")) {
            const auto& m = input["material"];
            material.id = m.value("id", 0);
            material.name = m.value("name", std::string("default"));
            material.viscosity = m.value("viscosity", 1.0);
        }
        if (input.contains("target_depth_um")) {
            target_depth_um = input["target_depth_um"].get<double>();
        }

        auto predictions = predict_chunked(cracks, material, target_depth_um);
        result.data = result_to_json(predictions);
        result.success = true;
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

json WashburnPenetrationModule::result_to_json(
    const std::vector<PenetrationPrediction>& results) const
{
    json j;
    j["count"] = static_cast<int>(results.size());
    j["predictions"] = json::array();
    for (const auto& p : results) {
        j["predictions"].push_back(prediction_to_json(p));
    }
    return j;
}

json WashburnPenetrationModule::prediction_to_json(const PenetrationPrediction& p) const {
    json j;
    j["crack_id"] = p.crack_id;
    j["material_id"] = p.material_id;
    j["target_depth_um"] = p.target_depth_um;
    j["predicted_time_s"] = p.predicted_time_s;
    j["penetration_rate_um_s"] = p.penetration_rate_um_s;
    j["crack_width_um"] = p.crack_width_um;
    j["viscosity_pa_s"] = p.viscosity_pa_s;
    j["wall_roughness_ra_um"] = p.wall_roughness_ra_um;
    j["roughness_factor"] = p.roughness_factor;
    j["wenzel_contact_angle"] = p.wenzel_contact_angle;
    j["effective_radius_um"] = p.effective_radius_um;
    j["effective_tortuosity"] = p.effective_tortuosity;
    j["roughness_correction_applied"] = p.roughness_correction_applied;
    return j;
}

bool WashburnPenetrationModule::cancel() {
    cancelled_ = true;
    status_ = ModuleStatus::IDLE;
    return true;
}

bool WashburnPenetrationModule::shutdown() {
    cancel();
    if (thread_pool_) {
        thread_pool_->shutdown();
    }
    thread_pool_.reset();
    {
        std::lock_guard<std::mutex> lock(models_mutex_);
        per_thread_models_.clear();
    }
    status_ = ModuleStatus::UNINITIALIZED;
    return true;
}

REGISTER_MODULE(WashburnPenetrationModule, "washburn_penetration")

}
}
