#pragma once

#include "modules/common/imodule.h"
#include "modules/common/ipc_channel.h"
#include "modules/common/thread_pool.h"
#include "modules/fem_stress/dealii_adapter.h"
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <future>

namespace porcelain_monitor {
namespace modules {

class FemStressModule : public IModule {
public:
    FemStressModule();
    ~FemStressModule() override;

    bool initialize(const json& config) override;
    bool execute(const json& input, ResultCallback callback = nullptr) override;
    bool execute_sync(const json& input, ModuleResult& result) override;
    ModuleStatus get_status() const override { return status_; }
    std::string get_name() const override { return "fem_stress"; }
    std::string get_version() const override { return "2.0.0"; }
    bool cancel() override;
    bool shutdown() override;
    bool supports_async() const override { return true; }
    bool supports_parallel() const override { return true; }
    bool is_external_process() const override { return use_external_process_; }

    void set_use_external_process(bool use) { use_external_process_ = use; }
    FemSolverBackend get_backend() const { return adapter_ ? adapter_->get_backend() : FemSolverBackend::BUILTIN; }
    bool is_dealii_available() const { return adapter_ ? adapter_->is_dealii_available() : false; }

    StressAnalysisResultData analyze_stress_sync(const config::StressFEMConfig& config,
                                                 const std::vector<CrackData>& cracks,
                                                 const GlazeDimensions& dims);

    std::vector<std::future<StressAnalysisResultData>> analyze_batch_parallel(
        const std::vector<std::pair<std::vector<CrackData>, GlazeDimensions>>& batch,
        const config::StressFEMConfig& config);

private:
    void run_async(const json& input, ResultCallback callback);
    StressAnalysisResultData dispatch_solve(const std::vector<CrackData>& cracks,
                                            const GlazeDimensions& dims);
    json result_to_json(const StressAnalysisResultData& result) const;
    bool initialize_external_process();
    StressAnalysisResultData solve_via_ipc(const json& input);
    StressAnalysisResultData compute_internal(const json& input);

    std::unique_ptr<DealIIAdapter> adapter_;
    std::unique_ptr<ThreadPool> compute_pool_;
    std::atomic<ModuleStatus> status_{ModuleStatus::UNINITIALIZED};
    bool use_external_process_ = false;
    std::unique_ptr<IIpcChannel> ipc_channel_;
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> cancelled_{false};
    std::atomic<int64_t> next_request_id_{1};
    config::StressFEMConfig fem_config_;
};

}
}
