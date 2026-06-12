#include "washburn_model.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace porcelain_monitor {
namespace algorithms {

WashburnPenetrationModel::WashburnPenetrationModel() = default;

void WashburnPenetrationModel::set_config(const WashburnConfig& config) {
    config_ = config;
}

void WashburnPenetrationModel::set_wall_roughness(double ra_um) {
    if (ra_um > 0.0) {
        config_.wall_roughness_ra_um = ra_um;
    }
}

double WashburnPenetrationModel::get_roughness_factor(double ra_um) const {
    if (ra_um <= 0.0) return 1.0;

    double lambda = 5.0;
    if (lambda < 1e-12) return 1.0;

    double r_s = 1.0 + (2.0 * M_PI * ra_um * ra_um) / (lambda * lambda);

    r_s = std::max(config_.min_roughness_factor, std::min(config_.max_roughness_factor, r_s));

    return r_s;
}

double WashburnPenetrationModel::wenzel_contact_angle(double theta_0_deg, double r_s) const {
    if (r_s <= 0.0) return theta_0_deg;
    if (r_s == 1.0) return theta_0_deg;

    double theta_0_rad = theta_0_deg * M_PI / 180.0;
    double cos_theta_0 = std::cos(theta_0_rad);
    double cos_theta_w = r_s * cos_theta_0;

    if (std::abs(cos_theta_w) > 1.0) {
        if (cos_theta_0 > 0.0) {
            cos_theta_w = 0.999;
        } else if (cos_theta_0 < 0.0) {
            cos_theta_w = -0.999;
        } else {
            return theta_0_deg;
        }
    }

    double theta_w_rad = std::acos(cos_theta_w);
    double theta_w_deg = theta_w_rad * 180.0 / M_PI;

    return theta_w_deg;
}

double WashburnPenetrationModel::roughness_tortuosity(double ra_um) const {
    if (ra_um <= 0.0) return config_.tortuosity_factor;

    double tau_eff = config_.tortuosity_factor * (1.0 + config_.roughness_tortuosity_coeff * ra_um);

    return tau_eff;
}

double WashburnPenetrationModel::roughness_effective_radius(double base_r, double ra_um) const {
    if (base_r <= 0.0) return 0.0;
    if (ra_um <= 0.0) return base_r;

    double r_eff = base_r * std::exp(-config_.roughness_radius_coeff * ra_um);

    return r_eff;
}

double WashburnPenetrationModel::dynamic_viscosity_correction(double viscosity, double shear_rate) const {
    if (viscosity <= 0.0) return 0.0;
    if (shear_rate <= 0.0) return viscosity;

    const double gamma_c = 1.0;
    const double n = 0.5;
    const double m = 0.3;

    double ratio = shear_rate / gamma_c;
    double factor = std::pow(1.0 + std::pow(ratio, n), -m);

    return viscosity * factor;
}

double WashburnPenetrationModel::effective_radius(double crack_width_um) const {
    double crack_width_m = crack_width_um * 1e-6;
    double r_parallel_plate = crack_width_m / 4.0;
    return r_parallel_plate / config_.surface_roughness_factor;
}

double WashburnPenetrationModel::capillary_pressure(double crack_width_um,
                                                    double surface_tension_n_m,
                                                    double contact_angle_deg) const {
    double r = effective_radius(crack_width_um);
    double theta_rad = contact_angle_deg * M_PI / 180.0;
    if (r < 1e-12) return 0.0;
    return (2.0 * surface_tension_n_m * cos(theta_rad)) / r;
}

double WashburnPenetrationModel::penetration_depth_at_time(double time_s,
                                                           double crack_width_um,
                                                           double viscosity_pa_s,
                                                           double surface_tension_n_m,
                                                           double contact_angle_deg) const {
    if (time_s <= 0.0) return 0.0;
    if (crack_width_um <= 0.0) return 0.0;
    if (viscosity_pa_s <= 0.0) return 0.0;
    if (surface_tension_n_m <= 0.0) return 0.0;

    double r_base = effective_radius(crack_width_um);
    double theta_eff_deg = contact_angle_deg;
    double tau_eff = config_.tortuosity_factor;
    double r_eff = r_base;
    double ra_um = config_.wall_roughness_ra_um;

    if (config_.wenzel_roughness_correction && ra_um > 0.0) {
        double r_s = get_roughness_factor(ra_um);
        theta_eff_deg = wenzel_contact_angle(contact_angle_deg, r_s);
        tau_eff = roughness_tortuosity(ra_um);
        r_eff = roughness_effective_radius(r_base, ra_um);
    }

    if (r_eff < 1e-12) return 0.0;

    double theta_rad = theta_eff_deg * M_PI / 180.0;
    double cos_theta = cos(theta_rad);
    double capillary_term = surface_tension_n_m * cos_theta * r_eff;
    double viscous_term = 2.0 * viscosity_pa_s * tau_eff;

    if (viscous_term < 1e-20) return 0.0;

    double h_ideal_sq = (capillary_term * time_s) / viscous_term;
    if (h_ideal_sq <= 0.0) return 0.0;

    double h_ideal_m = sqrt(h_ideal_sq);

    double gravity_threshold_m = 1e-4;
    if (h_ideal_m < gravity_threshold_m) {
        return h_ideal_m * 1e6;
    }

    double rho = 1000.0;
    double g = config_.gravity_m_s2;
    double h_eq = (2.0 * surface_tension_n_m * cos_theta) / (rho * g * r_eff);

    if (h_eq < 1e-6) {
        return h_ideal_m * 1e6;
    }

    double h = h_ideal_m;
    double dt = time_s / 200.0;
    double t = 0.0;

    while (t < time_s && h < h_eq * 0.999) {
        double dh_dt_numerator = capillary_term - rho * g * r_eff * r_eff * h;
        double dh_dt_denominator = 4.0 * viscosity_pa_s * tau_eff * h;
        if (dh_dt_denominator < 1e-20) break;
        if (dh_dt_numerator <= 0.0) break;

        double dh_dt_ideal = dh_dt_numerator / dh_dt_denominator;
        double shear_rate = dh_dt_ideal / r_eff;
        double eta_eff = dynamic_viscosity_correction(viscosity_pa_s, shear_rate);
        double eta_ratio = eta_eff / viscosity_pa_s;
        double dh_dt = dh_dt_ideal / eta_ratio;

        double dh = dh_dt * dt;

        h += dh;
        t += dt;

        if (dh < 1e-15) break;
    }

    return h * 1e6;
}

double WashburnPenetrationModel::time_to_reach_depth(double target_depth_um,
                                                     double crack_width_um,
                                                     double viscosity_pa_s,
                                                     double surface_tension_n_m,
                                                     double contact_angle_deg) const {
    if (target_depth_um <= 0.0) return 0.0;
    if (crack_width_um <= 0.0) return 1e20;
    if (viscosity_pa_s <= 0.0) return 1e20;
    if (surface_tension_n_m <= 0.0) return 1e20;

    double r_base = effective_radius(crack_width_um);
    double theta_eff_deg = contact_angle_deg;
    double tau_eff = config_.tortuosity_factor;
    double r_eff = r_base;
    double ra_um = config_.wall_roughness_ra_um;

    if (config_.wenzel_roughness_correction && ra_um > 0.0) {
        double r_s = get_roughness_factor(ra_um);
        theta_eff_deg = wenzel_contact_angle(contact_angle_deg, r_s);
        tau_eff = roughness_tortuosity(ra_um);
        r_eff = roughness_effective_radius(r_base, ra_um);
    }

    if (r_eff < 1e-12) return 1e20;

    double target_depth_m = target_depth_um * 1e-6;
    double theta_rad = theta_eff_deg * M_PI / 180.0;
    double cos_theta = cos(theta_rad);

    double numerator = 2.0 * viscosity_pa_s * tau_eff * target_depth_m * target_depth_m;
    double denominator = surface_tension_n_m * cos_theta * r_eff;

    if (denominator < 1e-20) return 1e20;

    double t_ideal = numerator / denominator;

    double gravity_threshold_m = 1e-4;
    if (target_depth_m < gravity_threshold_m) {
        return t_ideal;
    }

    double rho = 1000.0;
    double g = config_.gravity_m_s2;
    double h_eq = (2.0 * surface_tension_n_m * cos_theta) / (rho * g * r_eff);

    if (target_depth_m >= h_eq * 0.99) {
        return 1e20;
    }

    double h = 0.0;
    double t = 0.0;
    double dt = t_ideal / 500.0;
    if (dt < 1e-6) dt = 1e-6;

    while (h < target_depth_m && t < 1e8) {
        double dh_dt_numerator = surface_tension_n_m * cos_theta * r_eff - rho * g * r_eff * r_eff * h;
        double dh_dt_denominator = 4.0 * viscosity_pa_s * tau_eff * std::max(h, 1e-9);
        if (dh_dt_denominator < 1e-20) break;
        if (dh_dt_numerator <= 0.0) break;

        double dh_dt_ideal = dh_dt_numerator / dh_dt_denominator;
        double shear_rate = dh_dt_ideal / r_eff;
        double eta_eff = dynamic_viscosity_correction(viscosity_pa_s, shear_rate);
        double eta_ratio = eta_eff / viscosity_pa_s;
        double dh_dt = dh_dt_ideal / eta_ratio;

        double dh = dh_dt * dt;

        if (h + dh > target_depth_m) {
            double remaining = target_depth_m - h;
            t += remaining / dh_dt;
            h = target_depth_m;
            break;
        }

        h += dh;
        t += dt;

        if (dh < 1e-15) break;
    }

    return t;
}

double WashburnPenetrationModel::penetration_rate(double depth_um,
                                                  double crack_width_um,
                                                  double viscosity_pa_s,
                                                  double surface_tension_n_m,
                                                  double contact_angle_deg) const {
    if (depth_um <= 0.0) return 0.0;
    if (crack_width_um <= 0.0) return 0.0;
    if (viscosity_pa_s <= 0.0) return 0.0;
    if (surface_tension_n_m <= 0.0) return 0.0;

    double r_base = effective_radius(crack_width_um);
    double theta_eff_deg = contact_angle_deg;
    double tau_eff = config_.tortuosity_factor;
    double r_eff = r_base;
    double ra_um = config_.wall_roughness_ra_um;

    if (config_.wenzel_roughness_correction && ra_um > 0.0) {
        double r_s = get_roughness_factor(ra_um);
        theta_eff_deg = wenzel_contact_angle(contact_angle_deg, r_s);
        tau_eff = roughness_tortuosity(ra_um);
        r_eff = roughness_effective_radius(r_base, ra_um);
    }

    if (r_eff < 1e-12) return 0.0;

    double depth_m = depth_um * 1e-6;
    double theta_rad = theta_eff_deg * M_PI / 180.0;
    double cos_theta = cos(theta_rad);

    double numerator = surface_tension_n_m * cos_theta * r_eff;
    double denominator = 4.0 * viscosity_pa_s * tau_eff * depth_m;

    if (denominator < 1e-20) return 0.0;

    double dh_dt_ideal = numerator / denominator;

    double shear_rate = dh_dt_ideal / r_eff;
    double eta_eff = dynamic_viscosity_correction(viscosity_pa_s, shear_rate);
    double eta_ratio = eta_eff / viscosity_pa_s;
    dh_dt_ideal = dh_dt_ideal / eta_ratio;

    double gravity_threshold_m = 1e-4;
    if (depth_m < gravity_threshold_m) {
        return dh_dt_ideal * 1e6;
    }

    double rho = 1000.0;
    double g = config_.gravity_m_s2;
    double gravity_term = (rho * g * r_eff * r_eff) / (4.0 * viscosity_pa_s * tau_eff);

    double dh_dt = std::max(0.0, dh_dt_ideal - gravity_term);
    return dh_dt * 1e6;
}

PenetrationPrediction WashburnPenetrationModel::predict(int crack_id,
                                                        int material_id,
                                                        const CrackInfo& crack,
                                                        const RepairMaterial& material,
                                                        double target_depth_um) {
    PenetrationPrediction result;
    result.crack_id = crack_id;
    result.material_id = material_id;
    result.method = "Washburn";
    result.target_depth_um = target_depth_um;
    result.created_at = std::chrono::system_clock::now();

    double crack_width_um = crack.max_width > 0 ? crack.max_width * 1e6 : config_.default_surface_tension_n_m;
    if (crack.max_width <= 0) crack_width_um = 50.0;

    double viscosity_pa_s = material.viscosity > 0 ? material.viscosity : config_.default_viscosity_pa_s;
    double surface_tension_n_m = config_.default_surface_tension_n_m;
    double contact_angle_deg = config_.default_contact_angle_deg;

    if (material.properties.contains("surface_tension_n_m")) {
        surface_tension_n_m = material.properties["surface_tension_n_m"].get<double>();
    }
    if (material.properties.contains("contact_angle_deg")) {
        contact_angle_deg = material.properties["contact_angle_deg"].get<double>();
    }

    result.crack_width_um = crack_width_um;
    result.viscosity_pa_s = viscosity_pa_s;
    result.surface_tension_n_m = surface_tension_n_m;
    result.contact_angle_deg = contact_angle_deg;

    double ra_um = config_.wall_roughness_ra_um;
    bool correction_applied = config_.wenzel_roughness_correction && ra_um > 0.0;
    double r_s = 1.0;
    double theta_w = contact_angle_deg;
    double r_base = effective_radius(crack_width_um);
    double r_eff_final = r_base;
    double tau_eff_final = config_.tortuosity_factor;

    if (correction_applied) {
        r_s = get_roughness_factor(ra_um);
        theta_w = wenzel_contact_angle(contact_angle_deg, r_s);
        tau_eff_final = roughness_tortuosity(ra_um);
        r_eff_final = roughness_effective_radius(r_base, ra_um);
    }

    result.wall_roughness_ra_um = ra_um;
    result.roughness_factor = r_s;
    result.wenzel_contact_angle = theta_w;
    result.effective_radius_um = r_eff_final * 1e6;
    result.effective_tortuosity = tau_eff_final;
    result.roughness_correction_applied = correction_applied;

    double predicted_time_s = time_to_reach_depth(
        target_depth_um, crack_width_um, viscosity_pa_s,
        surface_tension_n_m, contact_angle_deg
    );
    result.predicted_time_s = predicted_time_s;

    double rate_at_target = penetration_rate(
        target_depth_um, crack_width_um, viscosity_pa_s,
        surface_tension_n_m, contact_angle_deg
    );
    result.penetration_rate_um_s = rate_at_target;

    int n_points = config_.time_series_points;
    double t_max = std::min(predicted_time_s * 1.2, predicted_time_s + 10.0);
    if (!std::isfinite(t_max) || t_max <= 0) t_max = 10.0;

    result.time_series.reserve(n_points + 1);
    result.depth_series.reserve(n_points + 1);

    for (int i = 0; i <= n_points; ++i) {
        double t = (t_max * i) / n_points;
        double h = penetration_depth_at_time(
            t, crack_width_um, viscosity_pa_s,
            surface_tension_n_m, contact_angle_deg
        );
        result.time_series.push_back(t);
        result.depth_series.push_back(h);
    }

    result.parameters = {
        {"tortuosity_factor", config_.tortuosity_factor},
        {"surface_roughness_factor", config_.surface_roughness_factor},
        {"gravity_m_s2", config_.gravity_m_s2},
        {"time_series_points", config_.time_series_points},
        {"effective_radius_um", effective_radius(crack_width_um) * 1e6},
        {"capillary_pressure_pa", capillary_pressure(crack_width_um, surface_tension_n_m, contact_angle_deg)},
        {"wall_roughness_ra_um", ra_um},
        {"wenzel_roughness_correction", correction_applied},
        {"roughness_factor", r_s},
        {"wenzel_contact_angle_deg", theta_w},
        {"corrected_effective_radius_um", r_eff_final * 1e6},
        {"corrected_tortuosity_factor", tau_eff_final},
        {"roughness_tortuosity_coeff", config_.roughness_tortuosity_coeff},
        {"roughness_radius_coeff", config_.roughness_radius_coeff}
    };

    result.result = result_to_json(result);

    return result;
}

json WashburnPenetrationModel::result_to_json(const PenetrationPrediction& prediction) const {
    json j;
    j["crack_id"] = prediction.crack_id;
    j["material_id"] = prediction.material_id;
    j["method"] = prediction.method;
    j["target_depth_um"] = prediction.target_depth_um;
    j["crack_width_um"] = prediction.crack_width_um;
    j["viscosity_pa_s"] = prediction.viscosity_pa_s;
    j["surface_tension_n_m"] = prediction.surface_tension_n_m;
    j["contact_angle_deg"] = prediction.contact_angle_deg;
    j["predicted_time_s"] = prediction.predicted_time_s;
    j["penetration_rate_um_s"] = prediction.penetration_rate_um_s;
    j["time_series"] = prediction.time_series;
    j["depth_series"] = prediction.depth_series;
    j["parameters"] = prediction.parameters;

    double effective_r_um = 0.0;
    double capillary_p = 0.0;
    try {
        effective_r_um = prediction.parameters.at("effective_radius_um").get<double>();
        capillary_p = prediction.parameters.at("capillary_pressure_pa").get<double>();
    } catch (...) {}

    double theta_rad = prediction.contact_angle_deg * M_PI / 180.0;
    double cos_theta = std::cos(theta_rad);
    double r_m = effective_r_um * 1e-6;
    double rho = 1000.0;
    double equilibrium_h_um = 0.0;
    if (r_m > 1e-15 && config_.gravity_m_s2 > 0) {
        equilibrium_h_um = (2.0 * prediction.surface_tension_n_m * cos_theta) /
                          (rho * config_.gravity_m_s2 * r_m) * 1e6;
    }

    j["derived_quantities"] = {
        {"effective_radius_um", effective_r_um},
        {"capillary_pressure_pa", capillary_p},
        {"tortuosity_factor", config_.tortuosity_factor},
        {"surface_roughness_factor", config_.surface_roughness_factor},
        {"equilibrium_penetration_depth_um", equilibrium_h_um},
        {"gravity_correction_applied", prediction.target_depth_um > 100.0}
    };

    j["roughness_correction"] = {
        {"roughness_correction_applied", prediction.roughness_correction_applied},
        {"wall_roughness_ra_um", prediction.wall_roughness_ra_um},
        {"roughness_factor", prediction.roughness_factor},
        {"wenzel_contact_angle_deg", prediction.wenzel_contact_angle},
        {"effective_radius_um", prediction.effective_radius_um},
        {"effective_tortuosity", prediction.effective_tortuosity}
    };

    return j;
}

}
}
