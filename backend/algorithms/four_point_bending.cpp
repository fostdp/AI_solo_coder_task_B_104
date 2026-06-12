#include "four_point_bending.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace porcelain_monitor {
namespace algorithms {

FourPointBendingTest::FourPointBendingTest()
    : failure_load_n_(0.0),
      crack_influence_coeffs_{1.12, -0.23, 10.6, -21.7, 30.4},
      interface_strength_ratio_(0.85) {
}

void FourPointBendingTest::set_config(const BendingTestConfig& config) {
    config_ = config;
}

double FourPointBendingTest::moment_of_inertia() const {
    double b = config_.specimen_width_mm * 1e-3;
    double h = config_.specimen_thickness_mm * 1e-3;
    return b * h * h * h / 12.0;
}

double FourPointBendingTest::crack_influence_factor(double crack_depth, double specimen_thickness) const {
    if (crack_depth <= 0.0 || specimen_thickness <= 0.0) return 1.0;
    if (crack_influence_coeffs_.size() < 5) return 1.0;
    double a_over_h = crack_depth / specimen_thickness;
    a_over_h = std::min(0.9, std::max(0.0, a_over_h));

    double f = crack_influence_coeffs_[0]
             + crack_influence_coeffs_[1] * a_over_h
             + crack_influence_coeffs_[2] * a_over_h * a_over_h
             + crack_influence_coeffs_[3] * a_over_h * a_over_h * a_over_h
             + crack_influence_coeffs_[4] * a_over_h * a_over_h * a_over_h * a_over_h;
    return f;
}

void FourPointBendingTest::compute_bending_moment_distribution(
    std::vector<double>& moments, double load_n) const {
    int n = static_cast<int>(moments.size());
    double L1 = config_.support_span_mm;
    double L2 = config_.loading_span_mm;
    double dx = L1 / (n - 1);
    double support_left = 0.0;
    double support_right = L1;
    double load_left = (L1 - L2) / 2.0;
    double load_right = L1 - (L1 - L2) / 2.0;
    double reaction = load_n / 2.0;

    for (int i = 0; i < n; ++i) {
        if (i == 0 || i == n - 1) {
            moments[i] = 0.0;
            continue;
        }
        double x = i * dx;
        if (x < load_left) {
            moments[i] = reaction * x;
        } else if (x <= load_right) {
            moments[i] = reaction * load_left;
        } else {
            moments[i] = reaction * (support_right - x);
        }
    }
}

double FourPointBendingTest::interpolate_stress(double y, double y_min, double y_max,
                                                 double moment, double inertia) const {
    if (inertia < 1e-18) return 0.0;
    double y_mid = (y_min + y_max) / 2.0;
    double y_from_na = y - y_mid;
    return moment * y_from_na / inertia;
}

void FourPointBendingTest::build_mesh(std::vector<BendingElement>& mesh, const CrackInfo& crack,
                                      const RepairMaterial& material, bool repaired) {
    int n_elem = config_.mesh_elements;
    mesh.clear();
    mesh.resize(n_elem);

    double L1 = config_.support_span_mm;
    double dx = L1 / n_elem;
    double h = config_.specimen_thickness_mm;

    double crack_center = L1 / 2.0;
    double crack_half_len = 0.0;
    if (!crack.points.empty()) {
        double min_x = 1e18, max_x = -1e18;
        for (const auto& p : crack.points) {
            min_x = std::min(min_x, p.x);
            max_x = std::max(max_x, p.x);
        }
        crack_center = (min_x + max_x) / 2.0;
        crack_half_len = (max_x - min_x) / 2.0;
        if (crack_half_len < 1e-6) crack_half_len = std::max(crack.total_length / 2.0, 1e-3);
    }

    double max_crack_depth = crack.max_depth * 1e-3;
    double bonding_strength = 0.0;
    try {
        if (material.properties.contains("bonding_strength")) {
            bonding_strength = material.properties["bonding_strength"].get<double>();
        }
    } catch (...) {
        bonding_strength = 0.0;
    }

    for (int i = 0; i < n_elem; ++i) {
        double x_center = (i + 0.5) * dx;
        mesh[i].x_center = x_center;
        mesh[i].length = dx;
        mesh[i].y_min = -h / 2.0;
        mesh[i].y_max = h / 2.0;
        mesh[i].damage = 0.0;
        mesh[i].has_crack = false;
        mesh[i].is_repaired = false;
        mesh[i].repair_bonding_strength = 0.0;

        double dist_to_crack = std::abs(x_center - crack_center);
        if (dist_to_crack <= crack_half_len + dx && max_crack_depth > 0.0) {
            mesh[i].has_crack = true;
            double depth_factor = std::exp(-dist_to_crack * dist_to_crack /
                                   (2.0 * (crack_half_len + dx) * (crack_half_len + dx)));
            mesh[i].damage = depth_factor * (max_crack_depth / h);

            if (repaired && bonding_strength > 0.0) {
                mesh[i].is_repaired = true;
                mesh[i].repair_bonding_strength = bonding_strength
                    * config_.repair_interface_strength_ratio;
            }
        }

        for (int k = 0; k < 6; ++k) {
            mesh[i].stress[k] = 0.0;
            mesh[i].strain[k] = 0.0;
        }
    }
}

void FourPointBendingTest::apply_four_point_loads(std::vector<BendingElement>& mesh, double load_n) {
    int n_elem = static_cast<int>(mesh.size());
    if (n_elem == 0) return;

    std::vector<double> moments(n_elem, 0.0);
    compute_bending_moment_distribution(moments, load_n);

    double I = moment_of_inertia();
    double h = config_.specimen_thickness_mm;
    double E = config_.porcelain_youngs_modulus_gpa * 1e9;

    for (int i = 0; i < n_elem; ++i) {
        double M = moments[i];
        double y_top = h / 2.0;
        double y_bottom = -h / 2.0;

        double sigma_top = interpolate_stress(y_top, y_bottom, y_top, M, I);
        double sigma_bottom = interpolate_stress(y_bottom, y_bottom, y_top, M, I);

        if (mesh[i].has_crack) {
            double a = mesh[i].damage * h * 1e-3;
            double f = crack_influence_factor(a, h * 1e-3);
            double a_over_h = std::min(1.0, mesh[i].damage);
            double sigma_eff_factor = 1.0 + f * std::sqrt(std::max(a_over_h, 0.0));

            double effective_strength = config_.porcelain_strength_mpa * 1e6;
            if (mesh[i].is_repaired) {
                double repair_strength = mesh[i].repair_bonding_strength * 1e6;
                effective_strength = config_.porcelain_strength_mpa * 1e6 * (1.0 - mesh[i].damage)
                    + repair_strength * mesh[i].damage;
            }

            double max_stress_allowed = effective_strength / std::max(sigma_eff_factor, 1.0);
            if (std::abs(sigma_top) > max_stress_allowed) {
                double scale = max_stress_allowed / std::max(std::abs(sigma_top), 1e-12);
                sigma_top *= scale;
                sigma_bottom *= scale;
            }
        }

        mesh[i].stress[0] = sigma_top;
        mesh[i].stress[1] = sigma_bottom;
        mesh[i].stress[2] = 0.0;
        mesh[i].stress[3] = 0.0;
        mesh[i].stress[4] = 0.0;
        mesh[i].stress[5] = 0.0;

        mesh[i].strain[0] = sigma_top / E;
        mesh[i].strain[1] = sigma_bottom / E;
        mesh[i].strain[2] = 0.0;
        mesh[i].strain[3] = 0.0;
        mesh[i].strain[4] = 0.0;
        mesh[i].strain[5] = 0.0;
    }
}

double FourPointBendingTest::compute_bending_strength(const std::vector<BendingElement>& mesh,
                                                       double bending_moment) const {
    if (mesh.empty()) return 0.0;

    double h = config_.specimen_thickness_mm * 1e-3;
    double I = moment_of_inertia();
    double y_max = h / 2.0;

    double max_stress = 0.0;
    for (const auto& elem : mesh) {
        double current_max = std::max(std::abs(elem.stress[0]), std::abs(elem.stress[1]));
        max_stress = std::max(max_stress, current_max);
    }

    if (max_stress < 1e-12) {
        return bending_moment * y_max / I / 1e6;
    }

    return max_stress / 1e6;
}

void FourPointBendingTest::update_damage(std::vector<BendingElement>& mesh, double load_n) {
    double strength_pa = config_.porcelain_strength_mpa * 1e6;

    for (auto& elem : mesh) {
        double max_stress = std::max(std::abs(elem.stress[0]), std::abs(elem.stress[1]));
        double effective_strength = strength_pa;

        if (elem.has_crack) {
            double h = config_.specimen_thickness_mm * 1e-3;
            double a = elem.damage * h;
            double f = crack_influence_factor(a, h);
            double sigma_eff_factor = std::sqrt(M_PI * std::max(a * 1e3, 1e-9)) * f;
            effective_strength = strength_pa / std::max(sigma_eff_factor, 1.0);

            if (elem.is_repaired) {
                double repair_strength = elem.repair_bonding_strength * 1e6;
                effective_strength = strength_pa * (1.0 - elem.damage)
                    + repair_strength * elem.damage * config_.repair_interface_strength_ratio;
            }
        }

        if (max_stress > effective_strength) {
            double overload = (max_stress - effective_strength) / std::max(effective_strength, 1.0);
            elem.damage = std::min(1.0, elem.damage + overload * 0.1);
        }
    }
}

void FourPointBendingTest::run_load_steps(std::vector<BendingElement>& mesh,
                                           BendingTestResult& result) {
    int load_steps = config_.load_steps;
    double max_disp = config_.max_displacement_mm;
    double L1 = config_.support_span_mm * 1e-3;
    double L2 = config_.loading_span_mm * 1e-3;
    double h = config_.specimen_thickness_mm * 1e-3;
    double b = config_.specimen_width_mm * 1e-3;
    double E = config_.porcelain_youngs_modulus_gpa * 1e9;
    double I = moment_of_inertia();

    double strength_pa = config_.porcelain_strength_mpa * 1e6;
    double y_max = h / 2.0;
    double max_moment = strength_pa * I / y_max;
    double max_load_est = 4.0 * max_moment / (L1 - L2);

    result.load_displacement_load.clear();
    result.load_displacement_disp.clear();
    failure_load_n_ = 0.0;

    for (int step = 0; step <= load_steps; ++step) {
        double load_ratio = static_cast<double>(step) / load_steps;
        double load_n = load_ratio * max_load_est * 1.5;

        double disp_mm = 0.0;
        if (I > 1e-18) {
            double a = (L1 - L2) / 2.0;
            disp_mm = (load_n * a / (24.0 * E * I)) *
                (3.0 * L1 * L1 - 4.0 * a * a) * 1e3;
        }

        apply_four_point_loads(mesh, load_n);
        update_damage(mesh, load_n);

        double max_stress = 0.0;
        for (const auto& elem : mesh) {
            double s = std::max(std::abs(elem.stress[0]), std::abs(elem.stress[1]));
            max_stress = std::max(max_stress, s);
        }

        result.load_displacement_load.push_back(load_n);
        result.load_displacement_disp.push_back(disp_mm);

        double effective_strength_limit = strength_pa;
        for (const auto& elem : mesh) {
            if (elem.has_crack) {
                double elem_strength = strength_pa;
                if (elem.is_repaired) {
                    double repair_strength = elem.repair_bonding_strength * 1e6;
                    elem_strength = strength_pa * (1.0 - elem.damage)
                        + repair_strength * elem.damage * config_.repair_interface_strength_ratio;
                } else {
                    double ac = elem.damage * h;
                    double f = crack_influence_factor(ac, h);
                    double a_over_h = std::min(1.0, elem.damage);
                    double sigma_eff_factor = 1.0 + f * std::sqrt(std::max(a_over_h, 0.0));
                    elem_strength = strength_pa / std::max(sigma_eff_factor, 1.0);
                }
                effective_strength_limit = std::min(effective_strength_limit, elem_strength);
            }
        }

        if (max_stress >= effective_strength_limit) {
            failure_load_n_ = load_n;
            break;
        }

        if (disp_mm > max_disp) {
            failure_load_n_ = load_n;
            break;
        }
    }

    if (failure_load_n_ <= 0.0 && !result.load_displacement_load.empty()) {
        failure_load_n_ = result.load_displacement_load.back();
    }
}

BendingTestResult FourPointBendingTest::simulate(int porcelain_id, int crack_id, int material_id,
                                                  const CrackInfo& crack,
                                                  const RepairMaterial& material,
                                                  bool repaired) {
    BendingTestResult result;
    result.id = 0;
    result.porcelain_id = porcelain_id;
    result.crack_id = crack_id;
    result.material_id = material_id;
    result.method = "FOUR_POINT_BENDING";
    result.created_at = std::chrono::system_clock::now();

    double h = config_.specimen_thickness_mm * 1e-3;
    double b = config_.specimen_width_mm * 1e-3;
    double I = b * h * h * h / 12.0;
    double L1 = config_.support_span_mm * 1e-3;
    double L2 = config_.loading_span_mm * 1e-3;
    double y_max = h / 2.0;
    double E = config_.porcelain_youngs_modulus_gpa * 1e9;
    double strength_pa = config_.porcelain_strength_mpa * 1e6;

    result.youngs_modulus_gpa = config_.porcelain_youngs_modulus_gpa;

    {
        double max_moment_orig = strength_pa * I / y_max;
        result.original_strength_mpa = config_.porcelain_strength_mpa;

        double a_crack = (crack.max_depth > 0) ? crack.max_depth * 1e-3 : 1e-9;
        double f = crack_influence_factor(a_crack, h);
        double a_over_h = std::min(1.0, a_crack / std::max(h, 1e-9));
        double sigma_eff_factor = 1.0 + f * std::sqrt(std::max(a_over_h, 0.0));
        double unrepaired_strength_pa = strength_pa / std::max(sigma_eff_factor, 1.0);
        result.unrepaired_strength_mpa = unrepaired_strength_pa / 1e6;

        if (repaired) {
            double bonding_strength = 0.0;
            try {
                if (material.properties.contains("bonding_strength")) {
                    bonding_strength = material.properties["bonding_strength"].get<double>();
                }
            } catch (...) {
                bonding_strength = 0.0;
            }

            double crack_depth_ratio = std::min(1.0, a_crack / h);
            double repaired_strength_pa = unrepaired_strength_pa;
            if (bonding_strength > 1.0) {
                double mix_strength = strength_pa * (1.0 - crack_depth_ratio)
                    + bonding_strength * 1e6 * crack_depth_ratio * config_.repair_interface_strength_ratio;
                repaired_strength_pa = std::max(mix_strength, unrepaired_strength_pa);
            }
            result.repaired_strength_mpa = repaired_strength_pa / 1e6;
        } else {
            result.repaired_strength_mpa = result.unrepaired_strength_mpa;
        }

        if (result.original_strength_mpa > 0.0) {
            result.strength_recovery_ratio = result.repaired_strength_mpa / result.original_strength_mpa;
        } else {
            result.strength_recovery_ratio = 0.0;
        }

        double K_IC = strength_pa * std::sqrt(M_PI * std::max(a_crack, 1e-9)) * f;
        result.fracture_toughness_mpa_m05 = K_IC / 1e6;
    }

    std::vector<BendingElement> mesh;
    build_mesh(mesh, crack, material, repaired);
    run_load_steps(mesh, result);

    json stress_dist = json::array();
    for (const auto& elem : mesh) {
        json elem_json = {
            {"x_center", elem.x_center},
            {"length", elem.length},
            {"y_min", elem.y_min},
            {"y_max", elem.y_max},
            {"stress_top", elem.stress[0] / 1e6},
            {"stress_bottom", elem.stress[1] / 1e6},
            {"strain_top", elem.strain[0]},
            {"strain_bottom", elem.strain[1]},
            {"damage", elem.damage},
            {"has_crack", elem.has_crack},
            {"is_repaired", elem.is_repaired}
        };
        stress_dist.push_back(elem_json);
    }
    result.stress_distribution = stress_dist;

    result.parameters = {
        {"support_span_mm", config_.support_span_mm},
        {"loading_span_mm", config_.loading_span_mm},
        {"specimen_width_mm", config_.specimen_width_mm},
        {"specimen_thickness_mm", config_.specimen_thickness_mm},
        {"porcelain_youngs_modulus_gpa", config_.porcelain_youngs_modulus_gpa},
        {"porcelain_poissons_ratio", config_.porcelain_poissons_ratio},
        {"porcelain_strength_mpa", config_.porcelain_strength_mpa},
        {"repair_interface_strength_ratio", config_.repair_interface_strength_ratio},
        {"mesh_elements", config_.mesh_elements},
        {"load_steps", config_.load_steps},
        {"failure_load_n", failure_load_n_}
    };

    json summary = {
        {"original_strength_mpa", result.original_strength_mpa},
        {"unrepaired_strength_mpa", result.unrepaired_strength_mpa},
        {"repaired_strength_mpa", result.repaired_strength_mpa},
        {"strength_recovery_ratio", result.strength_recovery_ratio},
        {"youngs_modulus_gpa", result.youngs_modulus_gpa},
        {"fracture_toughness_mpa_m05", result.fracture_toughness_mpa_m05},
        {"failure_load_n", failure_load_n_},
        {"load_displacement_points", static_cast<int>(result.load_displacement_load.size())}
    };
    result.result = summary;

    return result;
}

json FourPointBendingTest::result_to_json(const BendingTestResult& result) const {
    json j;
    j["id"] = result.id;
    j["porcelain_id"] = result.porcelain_id;
    j["crack_id"] = result.crack_id;
    j["material_id"] = result.material_id;
    j["method"] = result.method;
    j["parameters"] = result.parameters;
    j["original_strength_mpa"] = result.original_strength_mpa;
    j["unrepaired_strength_mpa"] = result.unrepaired_strength_mpa;
    j["repaired_strength_mpa"] = result.repaired_strength_mpa;
    j["strength_recovery_ratio"] = result.strength_recovery_ratio;
    j["youngs_modulus_gpa"] = result.youngs_modulus_gpa;
    j["fracture_toughness_mpa_m05"] = result.fracture_toughness_mpa_m05;
    j["load_displacement_load"] = result.load_displacement_load;
    j["load_displacement_disp"] = result.load_displacement_disp;
    j["stress_distribution"] = result.stress_distribution;
    j["result"] = result.result;
    return j;
}

double FourPointBendingTest::crack_influence_factor(double crack_depth, double specimen_thickness,
                                                     const std::vector<double>& poly_coeffs) const {
    if (crack_depth <= 0.0 || specimen_thickness <= 0.0 || poly_coeffs.empty()) return 1.0;
    double a_over_h = crack_depth / specimen_thickness;
    a_over_h = std::min(0.9, std::max(0.0, a_over_h));

    double f = 0.0;
    double a_pow = 1.0;
    for (size_t i = 0; i < poly_coeffs.size(); ++i) {
        f += poly_coeffs[i] * a_pow;
        a_pow *= a_over_h;
    }
    return f;
}

double FourPointBendingTest::predict_strength_recovery(const CalibrationDataset& data,
                                                       const CrackInfo& crack,
                                                       const RepairMaterial& material,
                                                       const std::vector<double>& params,
                                                       bool repaired) const {
    double h = config_.specimen_thickness_mm * 1e-3;
    double strength_pa = config_.porcelain_strength_mpa * 1e6;

    double a_crack = (crack.max_depth > 0) ? crack.max_depth * 1e-3 : 1e-9;

    std::vector<double> poly_coeffs(params.begin(), params.end() - 1);
    double interface_ratio = params.back();

    double f = crack_influence_factor(a_crack, h, poly_coeffs);
    double a_over_h = std::min(1.0, a_crack / std::max(h, 1e-9));
    double sigma_eff_factor = 1.0 + f * std::sqrt(std::max(a_over_h, 0.0));
    double unrepaired_strength_pa = strength_pa / std::max(sigma_eff_factor, 1.0);

    double repaired_strength_pa = unrepaired_strength_pa;
    if (repaired) {
        double bonding_strength = 0.0;
        try {
            if (material.properties.contains("bonding_strength")) {
                bonding_strength = material.properties["bonding_strength"].get<double>();
            }
        } catch (...) {
            bonding_strength = 0.0;
        }

        double crack_depth_ratio = std::min(1.0, a_crack / h);
        repaired_strength_pa = strength_pa * (1.0 - crack_depth_ratio)
            + bonding_strength * 1e6 * crack_depth_ratio * interface_ratio;
    }

    if (config_.porcelain_strength_mpa > 0.0) {
        return repaired_strength_pa / (strength_pa);
    }
    return 0.0;
}

double FourPointBendingTest::compute_mse(const std::vector<CalibrationDataset>& datasets,
                                         const CrackInfo& crack,
                                         const RepairMaterial& material,
                                         const std::vector<double>& params,
                                         bool repaired) const {
    if (datasets.empty()) return 0.0;

    double mse = 0.0;
    for (const auto& data : datasets) {
        double predicted = predict_strength_recovery(data, crack, material, params, repaired);
        double error = predicted - data.measured_value;
        if (data.measurement_std) {
            double weight = 1.0 / ((*data.measurement_std) * (*data.measurement_std));
            mse += weight * error * error;
        } else {
            mse += error * error;
        }
    }
    return mse / datasets.size();
}

double FourPointBendingTest::compute_r_squared(const std::vector<CalibrationDataset>& datasets,
                                               const CrackInfo& crack,
                                               const RepairMaterial& material,
                                               const std::vector<double>& params,
                                               bool repaired) const {
    if (datasets.empty()) return 0.0;

    double mean_measured = 0.0;
    for (const auto& data : datasets) {
        mean_measured += data.measured_value;
    }
    mean_measured /= datasets.size();

    double ss_tot = 0.0;
    double ss_res = 0.0;
    for (const auto& data : datasets) {
        double predicted = predict_strength_recovery(data, crack, material, params, repaired);
        ss_res += (predicted - data.measured_value) * (predicted - data.measured_value);
        ss_tot += (data.measured_value - mean_measured) * (data.measured_value - mean_measured);
    }

    if (ss_tot < 1e-12) return 1.0;
    return 1.0 - ss_res / ss_tot;
}

double FourPointBendingTest::evaluate_model_mse(const std::vector<double>& params,
                                                const std::vector<CalibrationDataset>& dataset) const {
    if (dataset.empty() || params.size() < 6) return 0.0;

    double mse = 0.0;
    for (const auto& data : dataset) {
        CrackInfo crack;
        crack.id = data.crack_id;
        crack.porcelain_id = data.porcelain_id;
        crack.max_depth = data.input_features.empty() ? 0.0 : data.input_features[0];

        RepairMaterial material;
        material.id = data.material_id;

        double predicted = predict_strength_recovery(data, crack, material, params, true);
        double error = predicted - data.measured_value;

        if (data.measurement_std && *data.measurement_std > 1e-12) {
            double weight = 1.0 / ((*data.measurement_std) * (*data.measurement_std));
            mse += weight * error * error;
        } else {
            mse += error * error;
        }
    }
    return mse / dataset.size();
}

void FourPointBendingTest::update_model_params(const std::vector<double>& params) {
    if (params.size() < 6) return;

    for (size_t i = 0; i < 5; ++i) {
        if (i < crack_influence_coeffs_.size()) {
            crack_influence_coeffs_[i] = params[i];
        } else {
            crack_influence_coeffs_.push_back(params[i]);
        }
    }
    interface_strength_ratio_ = params[5];
    config_.repair_interface_strength_ratio = params[5];
}

double FourPointBendingTest::compute_r2(const std::vector<double>& predictions,
                                        const std::vector<double>& measurements) const {
    if (predictions.empty() || measurements.empty() || predictions.size() != measurements.size()) {
        return 0.0;
    }

    double mean_measured = 0.0;
    for (double m : measurements) {
        mean_measured += m;
    }
    mean_measured /= measurements.size();

    double ss_res = 0.0;
    double ss_tot = 0.0;
    for (size_t i = 0; i < predictions.size(); ++i) {
        ss_res += (predictions[i] - measurements[i]) * (predictions[i] - measurements[i]);
        ss_tot += (measurements[i] - mean_measured) * (measurements[i] - mean_measured);
    }

    if (ss_tot < 1e-12) return 1.0;
    return 1.0 - ss_res / ss_tot;
}

FourPointBendingTest::CalibrationResult FourPointBendingTest::calibrate_model(
    const std::vector<CalibrationDataset>& dataset,
    bool update_internal_params) {

    CalibrationResult result;
    result.iterations = 0;

    if (dataset.empty()) {
        result.optimal_params = {1.12, -0.23, 10.6, -21.7, 30.4, 0.85};
        result.initial_mse = 0.0;
        result.final_mse = 0.0;
        result.initial_r2 = 0.0;
        result.final_r2 = 0.0;
        return result;
    }

    std::vector<ParameterBounds> bounds = {
        {"c0_const", 0.5, 2.0, 1.12},
        {"c1_linear", -1.0, 0.5, -0.23},
        {"c2_quad", 0.0, 30.0, 10.6},
        {"c3_cubic", -50.0, 0.0, -21.7},
        {"c4_quartic", 0.0, 60.0, 30.4},
        {"interface_strength_ratio", 0.5, 1.0, 0.85}
    };

    std::vector<double> default_params;
    for (const auto& b : bounds) {
        default_params.push_back(b.default_value);
    }

    result.initial_mse = evaluate_model_mse(default_params, dataset);

    std::vector<double> default_predictions;
    std::vector<double> measurements;
    for (const auto& data : dataset) {
        CrackInfo crack;
        crack.id = data.crack_id;
        crack.porcelain_id = data.porcelain_id;
        crack.max_depth = data.input_features.empty() ? 0.0 : data.input_features[0];
        RepairMaterial material;
        material.id = data.material_id;
        default_predictions.push_back(predict_strength_recovery(data, crack, material, default_params, true));
        measurements.push_back(data.measured_value);
    }
    result.initial_r2 = compute_r2(default_predictions, measurements);

    if (config_.enable_bayesian_calibration) {
        BayesianOptimizerConfig opt_config;
        opt_config.max_iter = config_.calibration_max_iter;
        opt_config.n_init = config_.calibration_initial_samples;
        opt_config.grid_search_points = 1000;
        opt_config.xi = config_.calibration_exploration_weight;
        opt_config.random_seed = 42;

        GPHyperparameters gp_params;
        gp_params.sigma_n = config_.calibration_noise_std;

        BayesianOptimizer optimizer(
            bounds,
            [this, &dataset](const std::vector<double>& params) {
                return evaluate_model_mse(params, dataset);
            },
            opt_config
        );

        OptimizationResult opt_result = optimizer.optimize();

        result.optimal_params = opt_result.best_params;
        result.final_mse = opt_result.best_objective;
        result.iterations = opt_result.iterations;

        for (const auto& val : opt_result.sample_values) {
            result.mse_history.push_back(val);
        }
        for (const auto& sample : opt_result.sample_points) {
            for (double p : sample) {
                result.param_history.push_back(p);
            }
        }
    } else {
        result.optimal_params = default_params;
        result.final_mse = result.initial_mse;
        result.iterations = 0;
    }

    std::vector<double> final_predictions;
    for (const auto& data : dataset) {
        CrackInfo crack;
        crack.id = data.crack_id;
        crack.porcelain_id = data.porcelain_id;
        crack.max_depth = data.input_features.empty() ? 0.0 : data.input_features[0];
        RepairMaterial material;
        material.id = data.material_id;
        final_predictions.push_back(predict_strength_recovery(data, crack, material, result.optimal_params, true));
    }
    result.final_r2 = compute_r2(final_predictions, measurements);

    if (update_internal_params) {
        update_model_params(result.optimal_params);
    }

    return result;
}

json FourPointBendingTest::CalibrationResult::to_json() const {
    json j;
    j["optimal_params"] = optimal_params;
    j["initial_mse"] = initial_mse;
    j["final_mse"] = final_mse;
    j["initial_r2"] = initial_r2;
    j["final_r2"] = final_r2;
    j["iterations"] = iterations;
    j["param_history"] = param_history;
    j["mse_history"] = mse_history;
    return j;
}

}
}
