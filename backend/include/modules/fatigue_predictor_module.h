#pragma once

#include <memory>
#include <vector>
#include "common.h"
#include "crack_propagation.h"
#include "lockfree_queue.h"
#include "module_base.h"
#include "config.h"

namespace porcelain_monitor {
namespace modules {

class FatiguePredictorModule
    : public ModuleBase<CrackDetectionMessage, FatiguePredictionMessage> {
public:
    using Base = ModuleBase<CrackDetectionMessage, FatiguePredictionMessage>;
    using InputQueue = typename Base::InputQueue;
    using OutputQueue = typename Base::OutputQueue;

    FatiguePredictorModule()
        : Base("FatiguePredictor") {}

    void set_parameters(const algorithms::ParisLawParameters& params) {
        params_ = params;
    }

protected:
    void process(const CrackDetectionMessage& msg, OutputQueue out) override {
        for (const auto& crack : msg.cracks) {
            algorithms::ParisLawParameters p = params_;
            p.initial_crack_length = crack.total_length;

            algorithms::CrackPropagationModel model;
            model.set_parameters(p);

            auto result = model.predict(
                crack,
                config::get_config().algorithms.paris_law.prediction_horizon_hours);

            CrackPrediction pred{};
            pred.crack_id = crack.id;
            pred.model_type = "ParisLaw+ResidualStress";
            pred.time_horizon_hours = config::get_config().algorithms.paris_law.prediction_horizon_hours;
            pred.predicted_depth = result.predicted_depth_720h;
            pred.predicted_width = result.predicted_width_720h;
            pred.predicted_length = result.predicted_length_720h;
            pred.confidence = result.confidence;
            pred.risk_level = result.risk_level;
            pred.parameters = model.result_to_json(result);

            if (out) {
                FatiguePredictionMessage out_msg{crack, msg.porcelain_id, pred};
                out->push(std::move(out_msg));
            }
        }
    }

private:
    algorithms::ParisLawParameters params_;
};

}
}
