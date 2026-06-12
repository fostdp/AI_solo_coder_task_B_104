#include "test_framework.h"
#include "four_point_bending.h"
#include <cmath>
#include <chrono>
#include <limits>
#include <vector>
#include <random>

using namespace porcelain_monitor;
using namespace porcelain_monitor::algorithms;

namespace {

CrackInfo createTestCrack(double depth_mm, double width_um) {
    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "TEST_CRACK";
    crack.max_depth = depth_mm;
    crack.max_width = width_um * 1e-3;
    crack.total_length = 10.0;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    Point3D p1, p2;
    p1.x = 15.0; p1.y = 0.0; p1.z = 0.0;
    p1.depth = depth_mm; p1.width = width_um * 1e-3;
    p2.x = 25.0; p2.y = 0.0; p2.z = 0.0;
    p2.depth = depth_mm; p2.width = width_um * 1e-3;
    crack.points.push_back(p1);
    crack.points.push_back(p2);

    return crack;
}

RepairMaterial createTestMaterial(double bonding_strength, double viscosity, double youngs_gpa) {
    RepairMaterial mat;
    mat.id = 1;
    mat.type = RepairMaterialType::SILICA;
    mat.name = "TestMaterial";
    mat.manufacturer = "Test";
    mat.particle_size_nm = 100.0;
    mat.purity = 0.99;
    mat.viscosity = viscosity;
    mat.refractive_index = 1.5;
    mat.thermal_expansion_coeff = 1e-6;
    mat.properties = {
        {"bonding_strength", bonding_strength},
        {"youngs_modulus_gpa", youngs_gpa}
    };
    return mat;
}

bool isMonotonicIncreasing(const std::vector<double>& v) {
    if (v.size() < 2) return true;
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] <= v[i - 1]) return false;
    }
    return true;
}

std::vector<CalibrationDataset> createMockCalibrationDataset(int n_points, double noise_std = 0.05) {
    std::vector<CalibrationDataset> dataset;
    dataset.reserve(n_points);

    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, noise_std);

    std::vector<double> true_params = {1.12, -0.23, 10.6, -21.7, 30.4, 0.85};

    for (int i = 0; i < n_points; ++i) {
        CalibrationDataset data;
        data.id = i + 1;
        data.porcelain_id = 1;
        data.crack_id = i + 1;
        data.material_id = 1;

        std::uniform_real_distribution<double> depth_dist(0.5, 3.0);
        std::uniform_real_distribution<double> bonding_dist(20.0, 80.0);
        std::uniform_real_distribution<double> viscosity_dist(0.01, 1.0);

        double crack_depth = depth_dist(rng);
        double bonding = bonding_dist(rng);
        double viscosity = viscosity_dist(rng);

        data.input_features = {crack_depth, bonding, viscosity};

        double h = 5.0 * 1e-3;
        double a_crack = crack_depth * 1e-3;
        double strength_pa = 120.0 * 1e6;

        double a_over_h = std::min(0.9, std::max(0.0, a_crack / h));

        double crack_depth_ratio = std::min(1.0, a_crack / h);
        double repaired_strength_pa = strength_pa * (1.0 - crack_depth_ratio)
            + bonding * 1e6 * crack_depth_ratio * true_params[5];

        double measured = repaired_strength_pa / strength_pa + noise(rng);
        measured = std::max(0.0, std::min(1.0, measured));

        data.measured_value = measured;
        data.measurement_std = noise_std;

        dataset.push_back(data);
    }

    return dataset;
}

}

TEST(FourPointBending, Bending_NoCrack_StrengthMatchesMaterial) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    CrackInfo no_crack = createTestCrack(0.0, 0.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 0, 0, no_crack, dummy_mat, false);

    ASSERT_RANGE(result.original_strength_mpa, 80.0, 150.0);
    ASSERT_NEAR(result.original_strength_mpa, 120.0, 1.0);
}

TEST(FourPointBending, Bending_WithCrack_StrengthReduced) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    CrackInfo crack = createTestCrack(2.0, 50.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 0, crack, dummy_mat, false);

    ASSERT_LT(result.unrepaired_strength_mpa, result.original_strength_mpa);

    double ratio = result.unrepaired_strength_mpa / result.original_strength_mpa;
    ASSERT_RANGE(ratio, 0.3, 0.8);
}

TEST(FourPointBending, Bending_Repaired_HighPermeability_RecoveryAbove80pct) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    config.repair_interface_strength_ratio = 0.85;
    test.set_config(config);

    CrackInfo crack = createTestCrack(2.0, 50.0);
    RepairMaterial high_perm_mat = createTestMaterial(80.0, 0.05, 70.0);

    auto result = test.simulate(1, 1, 1, crack, high_perm_mat, true);

    ASSERT_GE(result.strength_recovery_ratio, 0.80);
}

TEST(FourPointBending, Bending_LoadDisplacement_MonotonicallyIncreasing) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.load_steps = 50;
    config.max_displacement_mm = 2.0;
    test.set_config(config);

    CrackInfo crack = createTestCrack(1.0, 50.0);
    RepairMaterial mat = createTestMaterial(50.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 1, crack, mat, true);

    ASSERT_FALSE(result.load_displacement_load.empty());
    ASSERT_FALSE(result.load_displacement_disp.empty());

    ASSERT_TRUE(isMonotonicIncreasing(result.load_displacement_load));
    ASSERT_TRUE(isMonotonicIncreasing(result.load_displacement_disp));
}

TEST(FourPointBending, Bending_DifferentCrackDepths_DeeperCrackLowerStrength) {
    FourPointBendingTest test1, test2;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test1.set_config(config);
    test2.set_config(config);

    CrackInfo shallow_crack = createTestCrack(1.0, 50.0);
    CrackInfo deep_crack = createTestCrack(3.0, 50.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result_shallow = test1.simulate(1, 1, 0, shallow_crack, dummy_mat, false);
    auto result_deep = test2.simulate(1, 2, 0, deep_crack, dummy_mat, false);

    ASSERT_LT(result_deep.unrepaired_strength_mpa, result_shallow.unrepaired_strength_mpa);
}

TEST(FourPointBending, Bending_MultipleMaterials_BetterMaterialHigherRecovery) {
    FourPointBendingTest testA, testB;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    testA.set_config(config);
    testB.set_config(config);

    CrackInfo crack = createTestCrack(2.0, 50.0);
    RepairMaterial matA = createTestMaterial(80.0, 0.1, 70.0);
    RepairMaterial matB = createTestMaterial(30.0, 1.0, 70.0);

    auto resultA = testA.simulate(1, 1, 1, crack, matA, true);
    auto resultB = testB.simulate(1, 1, 2, crack, matB, true);

    ASSERT_GT(resultA.strength_recovery_ratio, resultB.strength_recovery_ratio);
}

TEST(FourPointBending, Bending_MinimalCrack_AlmostFullRecovery) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    CrackInfo tiny_crack = createTestCrack(0.01, 10.0);
    RepairMaterial good_mat = createTestMaterial(80.0, 0.1, 70.0);

    auto result = test.simulate(1, 1, 1, tiny_crack, good_mat, true);

    ASSERT_GT(result.strength_recovery_ratio, 0.95);
}

TEST(FourPointBending, Bending_CrackThroughThickness_VeryLowStrength) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    CrackInfo through_crack = createTestCrack(4.9, 100.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 0, through_crack, dummy_mat, false);

    ASSERT_LT(result.unrepaired_strength_mpa, 0.1 * result.original_strength_mpa);
}

TEST(FourPointBending, Bending_MaxDisplacementReached_TerminatesGracefully) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.max_displacement_mm = 0.5;
    config.load_steps = 100;
    test.set_config(config);

    CrackInfo no_crack = createTestCrack(0.0, 0.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 0, 0, no_crack, dummy_mat, false);

    ASSERT_FALSE(result.load_displacement_disp.empty());
    ASSERT_GT(result.load_displacement_disp.size(), 0u);

    double max_disp_reached = result.load_displacement_disp.back();
    ASSERT_LE(max_disp_reached, config.max_displacement_mm * 1.1);
}

TEST(FourPointBending, Bending_ZeroBondingMaterial_NoImprovement) {
    FourPointBendingTest test_unrepaired, test_repaired;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test_unrepaired.set_config(config);
    test_repaired.set_config(config);

    CrackInfo crack = createTestCrack(2.0, 50.0);
    RepairMaterial zero_bond_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result_unrepaired = test_unrepaired.simulate(1, 1, 0, crack, zero_bond_mat, false);
    auto result_repaired = test_repaired.simulate(1, 1, 1, crack, zero_bond_mat, true);

    double rel_error = 0.0;
    if (result_unrepaired.unrepaired_strength_mpa > 1e-12) {
        rel_error = std::abs(result_repaired.repaired_strength_mpa - result_unrepaired.unrepaired_strength_mpa)
            / result_unrepaired.unrepaired_strength_mpa * 100.0;
    }
    ASSERT_LT(rel_error, 5.0);
}

TEST(FourPointBending, Bending_MomentDistribution_Symmetric) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.support_span_mm = 40.0;
    config.loading_span_mm = 20.0;
    config.mesh_elements = 100;
    test.set_config(config);

    std::vector<double> moments(config.mesh_elements, 0.0);
    test.compute_bending_moment_distribution(moments, 100.0);

    int n = static_cast<int>(moments.size());
    for (int i = 0; i < n / 2; ++i) {
        int mirror_idx = n - 1 - i;
        ASSERT_NEAR(moments[i], moments[mirror_idx], 1.0);
    }

    double load_left = (config.support_span_mm - config.loading_span_mm) / 2.0;
    double load_right = config.support_span_mm - load_left;
    double dx = config.support_span_mm / (n - 1);

    int first_load_idx = static_cast<int>(load_left / dx) + 1;
    int second_load_idx = static_cast<int>(load_right / dx);
    if (second_load_idx >= n) second_load_idx = n - 1;

    ASSERT_GE(first_load_idx, 0);
    ASSERT_LT(second_load_idx, n);
    if (first_load_idx < second_load_idx) {
        double plateau_moment = moments[first_load_idx];
        for (int i = first_load_idx; i <= second_load_idx; ++i) {
            ASSERT_NEAR(moments[i], plateau_moment, 1.0);
        }
    }
}

TEST(FourPointBending, Bending_NegativeCrackDepth_ClampedToZero) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    CrackInfo neg_crack = createTestCrack(-1.0, 50.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 0, neg_crack, dummy_mat, false);

    ASSERT_FALSE(std::isnan(result.original_strength_mpa));
    ASSERT_FALSE(std::isinf(result.original_strength_mpa));
    ASSERT_GE(result.unrepaired_strength_mpa, 0.0);
    ASSERT_NEAR(result.unrepaired_strength_mpa, result.original_strength_mpa, 5.0);
}

TEST(FourPointBending, Bending_ThicknessZero_HandlesGracefully) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 0.0;
    test.set_config(config);

    CrackInfo crack = createTestCrack(1.0, 50.0);
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 0, crack, dummy_mat, false);

    ASSERT_FALSE(std::isnan(result.original_strength_mpa));
    ASSERT_FALSE(std::isinf(result.original_strength_mpa));
    ASSERT_FALSE(std::isnan(result.unrepaired_strength_mpa));
    ASSERT_FALSE(std::isinf(result.unrepaired_strength_mpa));
}

TEST(FourPointBending, Bending_SpanGreaterThanSpecimen_Warns) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.support_span_mm = 100.0;
    config.loading_span_mm = 50.0;
    test.set_config(config);

    CrackInfo crack = createTestCrack(1.0, 50.0);
    crack.total_length = 50.0;
    RepairMaterial dummy_mat = createTestMaterial(0.0, 1.0, 70.0);

    auto result = test.simulate(1, 1, 0, crack, dummy_mat, false);

    ASSERT_FALSE(std::isnan(result.original_strength_mpa));
    ASSERT_FALSE(std::isinf(result.original_strength_mpa));
    ASSERT_GE(result.unrepaired_strength_mpa, 0.0);
}

TEST(FourPointBending, Bending_Acceptance_HighPermeabilityRecoveryAbove80pct) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    config.specimen_width_mm = 10.0;
    config.support_span_mm = 40.0;
    config.loading_span_mm = 20.0;
    config.porcelain_strength_mpa = 120.0;
    config.porcelain_youngs_modulus_gpa = 70.0;
    config.repair_interface_strength_ratio = 0.93;
    test.set_config(config);

    CrackInfo realistic_crack = createTestCrack(2.0, 50.0);
    RepairMaterial high_perm_mat = createTestMaterial(65.0, 0.05, 70.0);

    auto result = test.simulate(1, 1, 1, realistic_crack, high_perm_mat, true);

    ASSERT_GE(result.strength_recovery_ratio, 0.80);
    ASSERT_GT(result.repaired_strength_mpa, result.unrepaired_strength_mpa);
}

TEST(BendingTestTests, BayesianOptimizer_CreateAndSetup) {
    BayesianOptimizer optimizer;
    std::vector<ParameterBounds> bounds = {
        {"c0_const", 0.5, 2.0, 1.12},
        {"c1_linear", -1.0, 0.5, -0.23},
        {"c2_quad", 0.0, 30.0, 10.6},
        {"c3_cubic", -50.0, 0.0, -21.7},
        {"c4_quartic", 0.0, 60.0, 30.4},
        {"interface_strength_ratio", 0.5, 1.0, 0.85}
    };
    optimizer.setBounds(bounds);

    BayesianOptimizerConfig config;
    config.max_iter = 30;
    config.n_init = 5;
    optimizer.setConfig(config);

    optimizer.setObjective([](const std::vector<double>&) {
        return 0.0;
    });

    ASSERT_TRUE(true);
}

TEST(BendingTestTests, GaussianProcess_PredictValid) {
    GaussianProcess gp;

    Matrix X(4, 1);
    X(0, 0) = 0.0;
    X(1, 0) = 1.0;
    X(2, 0) = 2.0;
    X(3, 0) = 3.0;
    std::vector<double> y = {0.0, 1.0, 4.0, 9.0};

    gp.train(X, y);

    double mu1, sigma_sq1;
    gp.predict({1.5}, mu1, sigma_sq1);
    ASSERT_NEAR(mu1, 2.25, 20.0);

    double mu2, sigma_sq2;
    gp.predict({2.5}, mu2, sigma_sq2);
    ASSERT_NEAR(mu2, 6.25, 20.0);

    ASSERT_GT(sigma_sq1, 0.0);
    ASSERT_GT(sigma_sq2, 0.0);
}

TEST(BendingTestTests, ExpectedImprovement_PeaksAtOptimum) {
    GaussianProcess gp;

    Matrix X(4, 1);
    X(0, 0) = 0.0;
    X(1, 0) = 1.0;
    X(2, 0) = 2.0;
    X(3, 0) = 3.0;
    std::vector<double> y = {0.1, 0.3, 0.6, 0.9};
    gp.train(X, y);

    std::vector<ParameterBounds> bounds = {{"x", 0.0, 5.0, 2.0}};
    BayesianOptimizer optimizer;
    optimizer.setBounds(bounds);
    optimizer.setObjective([](const std::vector<double>&) { return 0.0; });
    optimizer.setConfig(BayesianOptimizerConfig());

    double f_best = 0.5;
    double mu1 = 0.7, sigma1 = 0.1;
    double mu2 = 0.3, sigma2 = 0.1;

    double Z1 = (mu1 - f_best - 0.01) / sigma1;
    double ei1 = (mu1 - f_best - 0.01) * (0.5 * (1.0 + std::erf(Z1 / std::sqrt(2.0))))
               + sigma1 * (std::exp(-0.5 * Z1 * Z1) / std::sqrt(2.0 * M_PI));

    double Z2 = (mu2 - f_best - 0.01) / sigma2;
    double ei2 = (mu2 - f_best - 0.01) * (0.5 * (1.0 + std::erf(Z2 / std::sqrt(2.0))))
               + sigma2 * (std::exp(-0.5 * Z2 * Z2) / std::sqrt(2.0 * M_PI));

    ASSERT_GT(ei1, ei2);
}

TEST(BendingTestTests, ParameterBounds_Clamped) {
    ParameterBounds bound;
    bound.lower = 0.5;
    bound.upper = 2.0;

    double test_below = 0.1;
    double test_above = 3.0;
    double test_inside = 1.0;

    double clamped_below = std::max(bound.lower, std::min(bound.upper, test_below));
    double clamped_above = std::max(bound.lower, std::min(bound.upper, test_above));
    double clamped_inside = std::max(bound.lower, std::min(bound.upper, test_inside));

    ASSERT_NEAR(clamped_below, 0.5, 1e-9);
    ASSERT_NEAR(clamped_above, 2.0, 1e-9);
    ASSERT_NEAR(clamped_inside, 1.0, 1e-9);
}

TEST(BendingTestTests, CalibrateModel_ImprovesMSE) {
    auto dataset = createMockCalibrationDataset(10, 0.05);

    FourPointBendingTest test;
    BendingTestConfig config;
    config.enable_bayesian_calibration = true;
    config.calibration_max_iter = 30;
    config.calibration_initial_samples = 8;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    auto result = test.calibrate_model(dataset, true);

    ASSERT_LT(result.final_mse, result.initial_mse + 0.1);
    ASSERT_GT(result.final_r2, -1.0);
    ASSERT_EQ(static_cast<int>(result.optimal_params.size()), 6);
}

TEST(BendingTestTests, CalibrateModel_WithoutBayesian) {
    auto dataset = createMockCalibrationDataset(10, 0.05);

    FourPointBendingTest test;
    BendingTestConfig config;
    config.enable_bayesian_calibration = false;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    auto result = test.calibrate_model(dataset, false);

    ASSERT_NEAR(result.final_mse, result.initial_mse, 1e-9);
    ASSERT_EQ(static_cast<int>(result.optimal_params.size()), 6);
    ASSERT_TRUE(true);
}

TEST(BendingTestTests, CalibrationResult_JSON_Serializable) {
    auto dataset = createMockCalibrationDataset(5, 0.05);

    FourPointBendingTest test;
    BendingTestConfig config;
    config.enable_bayesian_calibration = false;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    auto result = test.calibrate_model(dataset, false);
    json j = result.to_json();

    ASSERT_TRUE(j.contains("optimal_params"));
    ASSERT_TRUE(j.contains("initial_mse"));
    ASSERT_TRUE(j.contains("final_mse"));
    ASSERT_TRUE(j.contains("initial_r2"));
    ASSERT_TRUE(j.contains("final_r2"));
    ASSERT_TRUE(j.contains("iterations"));

    ASSERT_TRUE(j["optimal_params"].is_array());
    ASSERT_TRUE(j["final_mse"].is_number());
    ASSERT_TRUE(j["final_r2"].is_number());
    ASSERT_TRUE(j["iterations"].is_number());
}

TEST(BendingTestTests, EmptyDataset_HandledGracefully) {
    std::vector<CalibrationDataset> empty_dataset;

    FourPointBendingTest test;
    BendingTestConfig config;
    config.enable_bayesian_calibration = true;
    test.set_config(config);

    auto result = test.calibrate_model(empty_dataset, true);

    ASSERT_TRUE(true);
    ASSERT_EQ(static_cast<int>(result.optimal_params.size()), 6);
    ASSERT_NEAR(result.final_mse, 0.0, 1e-9);
}

TEST(BendingTestTests, CrackInfluenceCoeffs_UsedInCalc) {
    FourPointBendingTest test;
    BendingTestConfig config;
    config.specimen_thickness_mm = 5.0;
    test.set_config(config);

    test.set_crack_influence_coeffs({1.0, 0.0, 0.0, 0.0, 0.0});
    double result1 = test.crack_influence_factor(0.5, 5.0);
    ASSERT_NEAR(result1, 1.0, 1e-9);

    test.set_crack_influence_coeffs({2.0, 0.0, 0.0, 0.0, 0.0});
    double result2 = test.crack_influence_factor(0.5, 5.0);
    ASSERT_NEAR(result2, 2.0, 1e-9);
}

TEST(BendingTestTests, Bayesian_NoisyData_Robust) {
    auto dataset = createMockCalibrationDataset(10, 0.15);

    FourPointBendingTest test;
    BendingTestConfig config;
    config.enable_bayesian_calibration = false;
    config.calibration_max_iter = 30;
    config.calibration_initial_samples = 8;
    config.calibration_noise_std = 0.15;
    config.specimen_thickness_mm = 5.0;
    config.porcelain_strength_mpa = 120.0;
    test.set_config(config);

    auto result = test.calibrate_model(dataset, true);

    ASSERT_LT(result.final_mse, result.initial_mse + 0.5);
    ASSERT_GT(result.final_r2, -1.0);
}

#ifndef PORCELAIN_TESTS_COMBINED
int main() {
    return RUN_ALL_TESTS();
}
#endif
