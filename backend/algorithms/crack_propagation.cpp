#include "crack_propagation.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace porcelain_monitor {
namespace algorithms {

double CrackPropagationModel::calculate_stress_intensity(double crack_length,
                                                         double stress) const {
    double effective_stress = stress;
    if (params_.enable_residual_stress_correction) {
        double residual_parallel = params_.residual_stress_parallel
            * params_.residual_stress_calibration_factor;
        double residual_perp = params_.residual_stress_perpendicular
            * params_.residual_stress_calibration_factor;
        double k_sigma = 1.0 + (residual_parallel + residual_perp) /
            (2.0 * std::abs(stress) + 1.0);
        effective_stress = stress * k_sigma + 0.3 * residual_parallel;
    }
    return params_.geometric_factor_Y * effective_stress * sqrt(M_PI * crack_length);
}

double CrackPropagationModel::calculate_effective_stress(
    double applied_stress, double crack_angle_rad) const {
    if (!params_.enable_residual_stress_correction) {
        return applied_stress;
    }
    double cos2 = cos(crack_angle_rad) * cos(crack_angle_rad);
    double sin2 = sin(crack_angle_rad) * sin(crack_angle_rad);
    double sigma_xx = params_.residual_stress_parallel
        * params_.residual_stress_calibration_factor;
    double sigma_yy = params_.residual_stress_perpendicular
        * params_.residual_stress_calibration_factor;
    double residual_on_crack = sigma_xx * cos2 + sigma_yy * sin2;
    double alpha = 0.4;
    return applied_stress + alpha * residual_on_crack;
}

double CrackPropagationModel::calculate_residual_stress_at_point(
    double x, double y, double z) const {
    if (params_.xrd_calibration_data.empty()) {
        return params_.residual_stress_parallel
            * params_.residual_stress_calibration_factor;
    }
    return interpolate_xrd_data(x, y, z, true);
}

double CrackPropagationModel::interpolate_xrd_data(
    double x, double y, double z, bool parallel_component) const {
    if (params_.xrd_calibration_data.empty()) return 0.0;

    double total_weight = 0.0;
    double weighted_sum = 0.0;

    for (const auto& dp : params_.xrd_calibration_data) {
        if (!dp.is_valid) continue;
        double dx = x - dp.measurement_position_x;
        double dy = y - dp.measurement_position_y;
        double dz = z - dp.measurement_position_z;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        double h = 2.0;
        double weight = exp(-dist * dist / (2.0 * h * h));
        double value = parallel_component ? dp.sigma_parallel : dp.sigma_perpendicular;
        weighted_sum += weight * value;
        total_weight += weight;
    }

    if (total_weight < 1e-10) {
        return parallel_component
            ? params_.residual_stress_parallel * params_.residual_stress_calibration_factor
            : params_.residual_stress_perpendicular * params_.residual_stress_calibration_factor;
    }

    return weighted_sum / total_weight * params_.residual_stress_calibration_factor;
}

void CrackPropagationModel::calibrate_with_xrd(
    const std::vector<XRDResidualStressData>& xrd_data) {
    if (xrd_data.empty()) return;

    params_.xrd_calibration_data = xrd_data;

    double sum_parallel = 0.0;
    double sum_perp = 0.0;
    size_t valid_count = 0;

    for (const auto& dp : xrd_data) {
        if (!dp.is_valid) continue;
        sum_parallel += dp.sigma_parallel;
        sum_perp += dp.sigma_perpendicular;
        valid_count++;
    }

    if (valid_count > 0) {
        double avg_parallel = sum_parallel / valid_count;
        double avg_perp = sum_perp / valid_count;
        if (std::abs(params_.residual_stress_parallel) > 1e-6) {
            params_.residual_stress_calibration_factor =
                avg_parallel / params_.residual_stress_parallel;
            params_.residual_stress_calibration_factor =
                std::max(0.5, std::min(2.0, params_.residual_stress_calibration_factor));
        }
        params_.residual_stress_parallel = avg_parallel;
        params_.residual_stress_perpendicular = avg_perp;
    }
}

double CrackPropagationModel::calculate_crack_growth_rate(double delta_K) const {
    if (delta_K <= 0) return 0;
    return params_.C * pow(delta_K, params_.m);
}

double CrackPropagationModel::critical_crack_length() const {
    return pow(params_.fracture_toughness /
               (params_.geometric_factor_Y * params_.maximum_stress), 2) / M_PI;
}

double CrackPropagationModel::runge_kutta_step(double a, double t, double dt) const {
    double sigma_max_eff = calculate_effective_stress(params_.maximum_stress, 0.0);
    double sigma_min_eff = calculate_effective_stress(params_.minimum_stress, 0.0);
    double K_max = calculate_stress_intensity(a, sigma_max_eff);
    double K_min = calculate_stress_intensity(a, sigma_min_eff);
    double delta_K = K_max - K_min;

    double da_dN = calculate_crack_growth_rate(delta_K);
    double cycles_per_sec = params_.cyclic_frequency;

    return da_dN * cycles_per_sec * dt;
}

double CrackPropagationModel::estimate_remaining_life(double initial_length,
                                                      double critical_length) const {
    if (critical_length <= initial_length) return 0;

    double a = initial_length;
    double t = 0;
    double dt = 3600.0;

    while (a < critical_length && t < 1e8) {
        double da = runge_kutta_step(a, t, dt);
        a += da;
        t += dt;

        if (da < 1e-15) break;
    }

    return t / 3600.0;
}

ParisLawResult CrackPropagationModel::predict(const CrackInfo& crack,
                                              int time_horizon_hours,
                                              int time_steps) {
    ParisLawResult result;
    result.parameters = params_;

    double a0 = crack.total_length > 0 ? crack.total_length : 0.001;
    double depth0 = crack.max_depth > 0 ? crack.max_depth : 0.0005;
    double width0 = crack.max_width > 0 ? crack.max_width : 0.0001;

    params_.initial_crack_length = a0;

    double dt = time_horizon_hours * 3600.0 / time_steps;
    double a = a0;
    double depth = depth0;
    double width = width0;

    double critical_length = critical_crack_length();
    double ttf = estimate_remaining_life(a0, critical_length);

    for (int i = 0; i <= time_steps; ++i) {
        double t_hours = i * dt / 3600.0;
        result.time_hours.push_back(t_hours);
        result.crack_length.push_back(a);
        result.crack_depth.push_back(depth);
        result.crack_width.push_back(width);

        double sigma_max_eff = calculate_effective_stress(params_.maximum_stress, 0.0);
        double sigma_min_eff = calculate_effective_stress(params_.minimum_stress, 0.0);
        double K_max = calculate_stress_intensity(a, sigma_max_eff);
        double K_min = calculate_stress_intensity(a, sigma_min_eff);
        double delta_K = K_max - K_min;
        result.stress_intensity_range.push_back(delta_K);

        double da_dN = calculate_crack_growth_rate(delta_K);
        result.crack_growth_rate.push_back(da_dN);

        if (i < time_steps) {
            double k1 = runge_kutta_step(a, i * dt, dt);
            double k2 = runge_kutta_step(a + 0.5 * k1 * dt, i * dt + 0.5 * dt, dt);
            double k3 = runge_kutta_step(a + 0.5 * k2 * dt, i * dt + 0.5 * dt, dt);
            double k4 = runge_kutta_step(a + k3 * dt, i * dt + dt, dt);

            double da = (k1 + 2 * k2 + 2 * k3 + k4) * dt / 6.0;
            a += da;

            depth = depth0 + (a - a0) * 0.4;
            width = width0 + (a - a0) * 0.25;

            if (a >= critical_length) {
                result.time_to_failure_hours = t_hours;
                break;
            }
        }
    }

    int idx_720 = std::min(time_steps, 720 * time_steps / time_horizon_hours);
    if (idx_720 < result.crack_length.size()) {
        result.predicted_length_720h = result.crack_length[idx_720];
        result.predicted_depth_720h = result.crack_depth[idx_720];
        result.predicted_width_720h = result.crack_width[idx_720];
    } else if (!result.crack_length.empty()) {
        result.predicted_length_720h = result.crack_length.back();
        result.predicted_depth_720h = result.crack_depth.back();
        result.predicted_width_720h = result.crack_width.back();
    }

    if (result.time_to_failure_hours < 0) {
        result.time_to_failure_hours = ttf;
    }

    result.risk_level = assess_risk_level(result);

    if (ttf > 0 && ttf < time_horizon_hours) {
        result.confidence = 0.9;
    } else if (a0 / critical_length > 0.5) {
        result.confidence = 0.8;
    } else {
        result.confidence = 0.75;
    }

    return result;
}

ParisLawResult CrackPropagationModel::predict_with_history(
    const CrackInfo& crack,
    const std::vector<CrackInfo>& history,
    int time_horizon_hours) {

    if (history.size() >= 2) {
        std::vector<double> times, lengths;
        for (const auto& h : history) {
            auto ts = std::chrono::duration_cast<std::chrono::hours>(
                h.detected_at.time_since_epoch()).count();
            times.push_back(ts);
            lengths.push_back(h.total_length);
        }
        calibrate_with_measurement(times, lengths);
    }

    return predict(crack, time_horizon_hours);
}

std::string CrackPropagationModel::assess_risk_level(const ParisLawResult& result) const {
    if (result.time_to_failure_hours > 0 && result.time_to_failure_hours < 720) {
        return "CRITICAL";
    }

    double growth_rate = 0;
    if (!result.crack_growth_rate.empty()) {
        growth_rate = result.crack_growth_rate.back();
    }

    if (growth_rate > 1e-8) {
        return "HIGH";
    } else if (growth_rate > 1e-10) {
        return "MEDIUM";
    } else if (growth_rate > 1e-12) {
        return "LOW";
    } else {
        return "NEGLIGIBLE";
    }
}

nlohmann::json CrackPropagationModel::result_to_json(const ParisLawResult& result) const {
    nlohmann::json j;
    j["time_hours"] = result.time_hours;
    j["crack_length"] = result.crack_length;
    j["crack_depth"] = result.crack_depth;
    j["crack_width"] = result.crack_width;
    j["stress_intensity_range"] = result.stress_intensity_range;
    j["crack_growth_rate"] = result.crack_growth_rate;
    j["predicted_length_720h"] = result.predicted_length_720h;
    j["predicted_depth_720h"] = result.predicted_depth_720h;
    j["predicted_width_720h"] = result.predicted_width_720h;
    j["time_to_failure_hours"] = result.time_to_failure_hours;
    j["confidence"] = result.confidence;
    j["risk_level"] = result.risk_level;
    j["parameters"] = {
        {"C", result.parameters.C},
        {"m", result.parameters.m},
        {"stress_ratio", result.parameters.stress_ratio_R},
        {"initial_crack_length", result.parameters.initial_crack_length},
        {"maximum_stress", result.parameters.maximum_stress},
        {"fracture_toughness", result.parameters.fracture_toughness},
        {"residual_stress_parallel", result.parameters.residual_stress_parallel},
        {"residual_stress_perpendicular", result.parameters.residual_stress_perpendicular},
        {"residual_stress_calibration_factor", result.parameters.residual_stress_calibration_factor},
        {"enable_residual_stress_correction", result.parameters.enable_residual_stress_correction},
        {"xrd_calibration_points", result.parameters.xrd_calibration_data.size()}
    };
    return j;
}

void CrackPropagationModel::calibrate_with_measurement(
    const std::vector<double>& times,
    const std::vector<double>& lengths) {

    if (times.size() < 2 || lengths.size() < 2) return;

    double dt = times.back() - times.front();
    double da = lengths.back() - lengths.front();

    if (dt > 0 && da > 0) {
        double avg_length = (lengths.back() + lengths.front()) / 2.0;
        double delta_stress = params_.maximum_stress - params_.minimum_stress;
        double delta_K = params_.geometric_factor_Y * delta_stress * sqrt(M_PI * avg_length);

        double cycles = params_.cyclic_frequency * dt * 3600.0;
        double measured_da_dN = da / cycles;

        if (delta_K > 0) {
            params_.C = measured_da_dN / pow(delta_K, params_.m);
            params_.C = std::max(1e-12, std::min(1e-8, params_.C));
        }
    }
}

double CrackPropagationModel::interpolate_history(
    const std::vector<double>& times,
    const std::vector<double>& values,
    double target_time) const {

    if (times.empty()) return 0;
    if (target_time <= times.front()) return values.front();
    if (target_time >= times.back()) return values.back();

    for (size_t i = 1; i < times.size(); ++i) {
        if (times[i] >= target_time) {
            double t = (target_time - times[i-1]) / (times[i] - times[i-1]);
            return values[i-1] + t * (values[i] - values[i-1]);
        }
    }

    return values.back();
}

}
}
