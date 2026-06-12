#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {
namespace algorithms {

using json = nlohmann::json;

struct WashburnConfig {
    double default_surface_tension_n_m = 0.072;
    double default_contact_angle_deg = 30.0;
    double default_viscosity_pa_s = 1.0;
    double gravity_m_s2 = 9.81;
    double tortuosity_factor = 1.5;
    double surface_roughness_factor = 1.2;
    int time_series_points = 100;
    double wall_roughness_ra_um = 0.0;
    bool wenzel_roughness_correction = false;
    double roughness_tortuosity_coeff = 0.3;
    double roughness_radius_coeff = 0.15;
    double min_roughness_factor = 0.1;
    double max_roughness_factor = 3.0;
};

class WashburnPenetrationModel {
public:
    WashburnPenetrationModel();

    void set_config(const WashburnConfig& config);

    PenetrationPrediction predict(int crack_id, int material_id,
                                   const CrackInfo& crack,
                                   const RepairMaterial& material,
                                   double target_depth_um);

    double penetration_depth_at_time(double time_s, double crack_width_um,
                                     double viscosity_pa_s,
                                     double surface_tension_n_m,
                                     double contact_angle_deg) const;

    double time_to_reach_depth(double target_depth_um, double crack_width_um,
                                double viscosity_pa_s,
                                double surface_tension_n_m,
                                double contact_angle_deg) const;

    double penetration_rate(double depth_um, double crack_width_um,
                            double viscosity_pa_s,
                            double surface_tension_n_m,
                            double contact_angle_deg) const;

    json result_to_json(const PenetrationPrediction& prediction) const;

    void set_wall_roughness(double ra_um);
    double get_roughness_factor(double ra_um) const;

    double wenzel_contact_angle(double theta_0_deg, double roughness_factor) const;
    double roughness_tortuosity(double ra_um) const;
    double roughness_effective_radius(double base_radius, double ra_um) const;
    double dynamic_viscosity_correction(double viscosity, double shear_rate) const;

    double effective_radius(double crack_width_um) const;
    double capillary_pressure(double crack_width_um, double surface_tension_n_m,
                              double contact_angle_deg) const;

private:
    friend class WashburnModelTestAccessor;

    WashburnConfig config_;
};

}
}
