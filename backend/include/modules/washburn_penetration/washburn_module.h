#pragma once

#include "modules/common/imodule.h"
#include "modules/common/thread_pool.h"
#include "washburn_model.h"
#include "common.h"
#include <memory>
#include <atomic>
#include <vector>
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>

namespace porcelain_monitor {
namespace modules {

struct WashburnBatchOptions {
    size_t min_batch_per_thread = 8;
    size_t max_parallel = 0;
    bool enable_cancellation = true;
    int material_id = 0;
};

class WashburnPenetrationModule : public IModule {
public:
    explicit WashburnPenetrationModule(size_t num_threads = 0);
    ~WashburnPenetrationModule() override;

    bool initialize(const json& config) override;
    bool execute(const json& input, ResultCallback callback = nullptr) override;
    bool execute_sync(const json& input, ModuleResult& result) override;
    ModuleStatus get_status() const override { return status_; }
    std::string get_name() const override { return "washburn_penetration"; }
    std::string get_version() const override { return "2.0.0"; }
    bool cancel() override;
    bool shutdown() override;
    bool supports_async() const override { return true; }
    bool supports_parallel() const override { return true; }

    size_t num_threads() const { return thread_pool_ ? thread_pool_->num_threads() : 0; }
    size_t active_tasks() const;
    void wait_all();

    std::vector<PenetrationPrediction> predict_batch(
        const std::vector<CrackInfo>& cracks,
        const RepairMaterial& material,
        double target_depth_um);

    PenetrationPrediction predict_single(
        const CrackInfo& crack,
        const RepairMaterial& material,
        double target_depth_um);

    std::vector<std::future<PenetrationPrediction>> predict_batch_async(
        const std::vector<CrackInfo>& cracks,
        const RepairMaterial& material,
        double target_depth_um);

    std::vector<PenetrationPrediction> predict_chunked(
        const std::vector<CrackInfo>& cracks,
        const RepairMaterial& material,
        double target_depth_um,
        const WashburnBatchOptions& options = {});

private:
    void run_async_batch(const std::vector<CrackInfo>& cracks,
                         const RepairMaterial& material,
                         double target_depth_um,
                         ResultCallback callback);

    PenetrationPrediction safe_predict_single(
        const CrackInfo& crack,
        const RepairMaterial& material,
        double target_depth_um);

    json result_to_json(const std::vector<PenetrationPrediction>& results) const;
    json prediction_to_json(const PenetrationPrediction& p) const;

    std::unique_ptr<ThreadPool> thread_pool_;
    std::vector<std::unique_ptr<algorithms::WashburnPenetrationModel>> per_thread_models_;
    std::mutex models_mutex_;
    std::atomic<ModuleStatus> status_{ModuleStatus::UNINITIALIZED};
    std::atomic<bool> cancelled_{false};
    algorithms::WashburnConfig washburn_config_;
};

}
}
