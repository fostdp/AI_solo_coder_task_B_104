#pragma once

#include "common.h"
#include "lockfree_queue.h"
#include "module_base.h"

namespace porcelain_monitor {
namespace modules {

class CrackRouterModule
    : public ModuleBase<ParsedLaserMessage, CrackDetectionMessage> {
public:
    using Base = ModuleBase<ParsedLaserMessage, CrackDetectionMessage>;
    using InputQueue = typename Base::InputQueue;
    using OutputQueue = typename Base::OutputQueue;

    CrackRouterModule()
        : Base("CrackRouter") {}

protected:
    void process(const ParsedLaserMessage& msg, OutputQueue out) override {
        if (!msg.data.crack_detected || msg.data.cracks.empty()) {
            return;
        }

        CrackDetectionMessage detection;
        detection.laser_data = msg.data;
        detection.porcelain_id = msg.data.porcelain_id;

        for (const auto& crack : msg.data.cracks) {
            detection.cracks.push_back(crack);
            detection.crack_points.push_back(crack.points);
        }

        if (out) {
            out->push(std::move(detection));
        }
    }
};

}
}
