#pragma once

#include <vector>
#include <string>
#include <array>
#include <nlohmann/json.hpp>
#include "common.h"
#include "bayesian_optimization.h"

namespace porcelain_monitor {
namespace algorithms {

using json = nlohmann::json;

struct BendingTestConfig {
    double support_span_mm = 40.0;
    double loading_span_mm = 20.0;
    double specimen_width_mm = 10.0;
    double specimen_thickness_mm = 5.0;
    double porcelain_youngs_modulus_gpa = 70.0;
    double porcelain_poissons_ratio = 0.22;
    double porcelain_strength_mpa = 120.0;
    double repair_interface_strength_ratio = 0.93;
    int mesh_elements = 100;
    int load_steps = 50;
    double max_displacement_mm = 2.0;
    bool enable_bayesian_calibration = false;
    int calibration_max_iter = 50;
    int calibration_initial_samples = 10;
    double calibration_exploration_weight = 0.01;
    double calibration_noise_std = 0.05;
};

struct BendingElement {
    double x_center;
    double length;
    double y_min, y_max;
    double stress[6];
    double strain[6];
    double damage = 0.0;
    bool has_crack = false;
    bool is_repaired = false;
    double repair_bonding_strength = 0.0;
};

class FourPointBendingTest {
public:
    FourPointBendingTest();

    void set_config(const BendingTestConfig& config);

    BendingTestResult simulate(int porcelain_id, int crack_id, int material_id,
                                const CrackInfo& crack,
                                const RepairMaterial& material,
                                bool repaired = true);

    double compute_bending_strength(const std::vector<BendingElement>& mesh,
                                     double bending_moment) const;

    double crack_influence_factor(double crack_depth, double specimen_thickness) const;

    double crack_influence_factor(double crack_depth, double specimen_thickness,
                                  const std::vector<double>& poly_coeffs) const;

    struct CalibrationResult {
        std::vector<double> optimal_params;
        double initial_mse;
        double final_mse;
        double initial_r2;
        double final_r2;
        int iterations;
        std::vector<double> param_history;
        std::vector<double> mse_history;
        json to_json() const;
    };

    CalibrationResult calibrate_model(
        const std::vector<CalibrationDataset>& dataset,
        bool update_internal_params = true);

    json result_to_json(const BendingTestResult& result) const;

    void compute_bending_moment_distribution(std::vector<double>& moments,
                                              double load_n) const;

    void set_crack_influence_coeffs(const std::vector<double>& coeffs) {
        crack_influence_coeffs_ = coeffs;
    }

    std::vector<double> get_crack_influence_coeffs() const {
        return crack_influence_coeffs_;
    }

private:
    void build_mesh(std::vector<BendingElement>& mesh, const CrackInfo& crack,
                    const RepairMaterial& material, bool repaired);
    void apply_four_point_loads(std::vector<BendingElement>& mesh, double load_n);
    void update_damage(std::vector<BendingElement>& mesh, double load_n);
    void run_load_steps(std::vector<BendingElement>& mesh,
                         BendingTestResult& result);
    double interpolate_stress(double y, double y_min, double y_max,
                               double moment, double inertia) const;
    double moment_of_inertia() const;

    double predict_strength_recovery(const CalibrationDataset& data,
                                     const CrackInfo& crack,
                                     const RepairMaterial& material,
                                     const std::vector<double>& params,
                                     bool repaired) const;

    double compute_mse(const std::vector<CalibrationDataset>& datasets,
                       const CrackInfo& crack,
                       const RepairMaterial& material,
                       const std::vector<double>& params,
                       bool repaired) const;

    double compute_r_squared(const std::vector<CalibrationDataset>& datasets,
                             const CrackInfo& crack,
                             const RepairMaterial& material,
                             const std::vector<double>& params,
                             bool repaired) const;

    double evaluate_model_mse(const std::vector<double>& params,
                             const std::vector<CalibrationDataset>& dataset) const;
    void update_model_params(const std::vector<double>& params);
    double compute_r2(const std::vector<double>& predictions,
                     const std::vector<double>& measurements) const;

    BendingTestConfig config_;
    double failure_load_n_;
    std::vector<double> crack_influence_coeffs_ = {1.12, -0.23, 10.6, -21.7, 30.4};
    double interface_strength_ratio_;
};

}
}
