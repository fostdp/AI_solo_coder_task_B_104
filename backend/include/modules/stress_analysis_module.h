#pragma once

#include "common.h"
#include "lockfree_queue.h"
#include "module_base.h"
#include "stress_analysis.h"
#include "database.h"

namespace porcelain_monitor {
namespace modules {

class StressAnalysisModule
    : public ModuleBase<CrackDetectionMessage, StressAnalysisMessage> {
public:
    using Base = ModuleBase<CrackDetectionMessage, StressAnalysisMessage>;
    using InputQueue = typename Base::InputQueue;
    using OutputQueue = typename Base::OutputQueue;

    StressAnalysisModule()
        : Base("StressAnalysis") {
        config_.grid_resolution = 50;
        config_.youngs_modulus_gpa = 70.0;
        config_.poissons_ratio = 0.22;
        config_.crack_density_sensitivity = 50.0;
        config_.max_stress_mpa = 300.0;
        fem_.set_config(config_);
    }

    void set_fem_config(const algorithms::FEMConfig& config) {
        config_ = config;
        fem_.set_config(config_);
    }

protected:
    void process(const CrackDetectionMessage& msg, OutputQueue out) override {
        if (msg.cracks.empty()) {
            return;
        }

        try {
            auto result = fem_.analyze(msg.porcelain_id, msg.cracks);

            try {
                auto& db = DatabaseManager::instance();
                int64_t analysis_id = db.insert_stress_analysis(result);
                if (analysis_id > 0) {
                    db.insert_stress_grid_points(analysis_id, result.grid_points);
                }
            } catch (const std::exception& e) {
                std::cerr << "[StressAnalysis] DB insert failed: " << e.what() << std::endl;
            }

            if (out) {
                StressAnalysisMessage sa_msg;
                sa_msg.porcelain_id = msg.porcelain_id;
                sa_msg.cracks = msg.cracks;
                sa_msg.result = result;
                out->push(std::move(sa_msg));
            }

            if (++counter_ % 10 == 0) {
                std::cout << "[StressAnalysis] Processed " << counter_
                          << " porcelain analyses, last max_stress="
                          << result.max_von_mises << " MPa" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[StressAnalysis] Analysis failed: " << e.what() << std::endl;
        }
    }

private:
    algorithms::FEMConfig config_;
    algorithms::StressAnalysisFEM fem_;
    std::atomic<uint64_t> counter_{0};
};

}
}
