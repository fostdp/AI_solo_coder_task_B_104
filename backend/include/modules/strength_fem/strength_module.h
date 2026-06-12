#pragma once

#include "modules/common/imodule.h"
#include "modules/common/ipc_channel.h"
#include "modules/common/thread_pool.h"
#include "modules/strength_fem/dealii_adapter.h"
#include "four_point_bending.h"
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <future>
#include <nlohmann/json.hpp>

namespace porcelain_monitor {
namespace modules {

class StrengthFemModule : public IModule {
public:
    StrengthFemModule();
    ~StrengthFemModule() override;

    bool initialize(const json& config) override;
    bool execute(const json& input, ResultCallback callback = nullptr) override;
    bool execute_sync(const json& input, ModuleResult& result) override;
    ModuleStatus get_status() const override { return status_; }
    std::string get_name() const override { return "strength_fem"; }
    std::string get_version() const override { return "2.0.0"; }
    bool cancel() override;
    bool shutdown() override;
    bool supports_async() const override { return true; }
    bool supports_parallel() const override { return true; }
    bool is_external_process() const override { return use_external_process_; }

    void set_use_external_process(bool use) { use_external_process_ = use; }
    StrengthSolverBackend get_backend() const { return adapter_ ? adapter_->get_backend() : StrengthSolverBackend::BUILTIN; }
    bool is_dealii_available() const { return adapter_ ? adapter_->is_dealii_available() : false; }
    BendingTestResult simulate_bending_sync(
        const CrackInfo& crack,
        const RepairMaterial& material,
        bool repaired = true);

    algorithms::FourPointBendingTest::CalibrationResult calibrate(
        const std::vector<algorithms::CalibrationDataset>& dataset,
        bool update_params = true);

    std::vector<std::future<BendingTestResult>> simulate_batch_parallel(
        const std::vector<std::tuple<CrackInfo, RepairMaterial, bool>>& batch);

private:
    void run_async(const json& input, ResultCallback callback);
    json result_to_json(const BendingTestResult& result) const;
    bool initialize_external_process();
    BendingTestResult compute_internal(const json& input);
    BendingTestResult dispatch_solve(const CrackInfo& crack,
                                     const RepairMaterial& material,
                                     bool repaired);
    BendingTestResult solve_via_ipc(const json& input);

    std::unique_ptr<StrengthDealIIAdapter> adapter_;
    std::unique_ptr<ThreadPool> compute_pool_;
    std::atomic<ModuleStatus> status_{ModuleStatus::UNINITIALIZED};
    bool use_external_process_ = false;
    std::unique_ptr<IIpcChannel> ipc_channel_;
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> cancelled_{false};
    std::atomic<int64_t> next_request_id_{1};
    algorithms::BendingTestConfig bending_config_;
};

}
}
