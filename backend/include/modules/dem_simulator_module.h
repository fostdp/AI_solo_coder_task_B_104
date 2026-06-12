#pragma once

#include <memory>
#include <vector>
#include "common.h"
#include "dem_simulation.h"
#include "lockfree_queue.h"
#include "module_base.h"
#include "config.h"

namespace porcelain_monitor {
namespace modules {

class DemSimulatorModule
    : public ModuleBase<CrackDetectionMessage, DemSimulationMessage> {
public:
    using Base = ModuleBase<CrackDetectionMessage, DemSimulationMessage>;
    using InputQueue = typename Base::InputQueue;
    using OutputQueue = typename Base::OutputQueue;

    DemSimulatorModule()
        : Base("DemSimulator") {
        default_material_.id = 1;
        default_material_.type = RepairMaterialType::ZIRCONIA;
        default_material_.name = "Zirconia Nano";
        default_material_.particle_size_nm = 25.0;
    }

    void set_default_material(const RepairMaterial& m) { default_material_ = m; }
    void set_parameters(const algorithms::DEMParameters& p) { params_ = p; }

protected:
    void process(const CrackDetectionMessage& msg, OutputQueue out) override {
        for (const auto& crack : msg.cracks) {
            algorithms::DEMSimulation sim;
            sim.set_parameters(params_);
            sim.set_crack_geometry(crack);
            sim.set_material_properties(default_material_);
            sim.generate_particles(config::get_config().algorithms.dem.max_particles / 10);

            auto result = sim.run(config::get_config().algorithms.dem.simulation_steps);

            RepairSimulation rs{};
            rs.crack_id = crack.id;
            rs.material_id = default_material_.id;
            rs.method = "DEM-DiscreteElement";
            rs.particle_count = result.particle_count;
            rs.filling_rate = result.filling_rate;
            rs.bonding_strength = result.bonding_strength;
            rs.surface_smoothness = result.surface_smoothness;
            rs.durability_score = result.durability_score;
            rs.result = sim.result_to_json(result);

            if (out) {
                DemSimulationMessage out_msg{crack, msg.porcelain_id, default_material_, rs};
                out->push(std::move(out_msg));
            }

            sim.reset();
        }
    }

private:
    RepairMaterial default_material_;
    algorithms::DEMParameters params_;
};

}
}
