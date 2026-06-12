#include "test_framework.h"
#include "washburn_model.h"
#include <cmath>
#include <chrono>
#include <limits>

using namespace porcelain_monitor;
using namespace porcelain_monitor::algorithms;

namespace {

constexpr double DEFAULT_GAMMA = 0.072;
constexpr double DEFAULT_THETA_DEG = 30.0;
constexpr double DEFAULT_ETA = 1.0;
constexpr double DEFAULT_CRACK_WIDTH_UM = 10.0;
constexpr double DEFAULT_TORTUOSITY = 1.5;
constexpr double DEFAULT_ROUGHNESS = 1.2;
constexpr double MIN_ROUGHNESS_FACTOR = 0.1;
constexpr double MAX_ROUGHNESS_FACTOR = 3.0;

class WashburnModelTestAccessor {
public:
    explicit WashburnModelTestAccessor(const WashburnPenetrationModel& model) : model_(model) {}

    double wenzel_contact_angle(double theta_0_deg, double roughness_factor) const {
        return model_.wenzel_contact_angle(theta_0_deg, roughness_factor);
    }

    double roughness_tortuosity(double ra_um) const {
        return model_.roughness_tortuosity(ra_um);
    }

    double roughness_effective_radius(double base_radius, double ra_um) const {
        return model_.roughness_effective_radius(base_radius, ra_um);
    }

    double dynamic_viscosity_correction(double viscosity, double shear_rate) const {
        return model_.dynamic_viscosity_correction(viscosity, shear_rate);
    }

private:
    const WashburnPenetrationModel& model_;
};

double computeEffectiveRadius(double crack_width_um) {
    double crack_width_m = crack_width_um * 1e-6;
    double r_parallel_plate = crack_width_m / 4.0;
    return r_parallel_plate / DEFAULT_ROUGHNESS;
}

double computeAnalyticalDepth(double gamma, double theta_deg, double crack_width_um,
                              double eta, double tau, double t) {
    if (t <= 0.0) return 0.0;
    double r = computeEffectiveRadius(crack_width_um);
    double theta_rad = theta_deg * M_PI / 180.0;
    double cos_theta = std::cos(theta_rad);
    double numerator = gamma * cos_theta * r * t;
    double denominator = 2.0 * eta * tau;
    if (denominator < 1e-20 || numerator <= 0.0) return 0.0;
    double h_m = std::sqrt(numerator / denominator);
    return h_m * 1e6;
}

CrackInfo createTestCrack(double max_width_m) {
    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "TEST_CRACK";
    crack.max_depth = 0.001;
    crack.max_width = max_width_m;
    crack.total_length = 0.01;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();
    return crack;
}

RepairMaterial createTestMaterial(double viscosity, double surface_tension, double contact_angle) {
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
        {"surface_tension_n_m", surface_tension},
        {"contact_angle_deg", contact_angle}
    };
    return mat;
}

}

TEST(Washburn, StandardParams_DepthMatchesAnalytical) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.default_surface_tension_n_m = DEFAULT_GAMMA;
    cfg.default_contact_angle_deg = DEFAULT_THETA_DEG;
    cfg.default_viscosity_pa_s = DEFAULT_ETA;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t = 0.1;
    double h_pred = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double h_analytical = computeAnalyticalDepth(
        DEFAULT_GAMMA, DEFAULT_THETA_DEG, DEFAULT_CRACK_WIDTH_UM,
        DEFAULT_ETA, DEFAULT_TORTUOSITY, t
    );

    ASSERT_GT(h_pred, 0.0);
    ASSERT_NEAR(h_pred, h_analytical, 5.0);
}

TEST(Washburn, TimeReachDepth_MatchesDepthFormula) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t_input = 0.1;
    double h = model.penetration_depth_at_time(
        t_input, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_GT(h, 0.0);

    double t_recovered = model.time_to_reach_depth(
        h, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(t_recovered, 0.0);
    ASSERT_NEAR(t_recovered, t_input, 5.0);
}

TEST(Washburn, PenetrationRate_DecreasesWithDepth) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double rate_shallow = model.penetration_rate(
        50.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double rate_deep = model.penetration_rate(
        200.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(rate_shallow, 0.0);
    ASSERT_GT(rate_deep, 0.0);
    ASSERT_GT(rate_shallow, rate_deep);
}

TEST(Washburn, DifferentViscosities_LowerViscosityFaster) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t = 100.0;
    double h_low_eta = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, 0.5, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double h_high_eta = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, 2.0, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(h_low_eta, 0.0);
    ASSERT_GT(h_high_eta, 0.0);
    ASSERT_GT(h_low_eta, h_high_eta);

    double ratio = h_low_eta / h_high_eta;
    ASSERT_RANGE(ratio, 1.7, 2.3);
}

TEST(Washburn, DifferentContactAngles_SmallerAngleBetter) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t = 100.0;
    double h_10deg = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 10.0
    );
    double h_60deg = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 60.0
    );
    double h_90deg = model.penetration_depth_at_time(
        t, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 90.0
    );

    ASSERT_GT(h_10deg, 0.0);
    ASSERT_GT(h_60deg, 0.0);
    ASSERT_GT(h_10deg, h_60deg);
    ASSERT_LT(h_90deg, 0.01);
}

TEST(Washburn, FullPredict_ValidOutputFields) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.time_series_points = 100;
    model.set_config(cfg);

    CrackInfo crack = createTestCrack(50e-6);
    RepairMaterial mat = createTestMaterial(DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG);

    auto result = model.predict(1, 1, crack, mat, 200.0);

    ASSERT_GT(result.predicted_time_s, 0.0);
    ASSERT_GT(result.penetration_rate_um_s, 0.0);
    ASSERT_EQ(result.time_series.size(), static_cast<size_t>(101));
    ASSERT_EQ(result.depth_series.size(), static_cast<size_t>(101));

    for (size_t i = 1; i < result.time_series.size(); ++i) {
        ASSERT_GT(result.time_series[i], result.time_series[i - 1]);
    }
    for (size_t i = 1; i < result.depth_series.size(); ++i) {
        ASSERT_GE(result.depth_series[i], result.depth_series[i - 1]);
    }
}

TEST(Washburn, ZeroTime_DepthIsZero) {
    WashburnPenetrationModel model;
    double h = model.penetration_depth_at_time(
        0.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_NEAR(h, 0.0, 1e-10);
}

TEST(Washburn, ZeroViscosity_InvalidButNoCrash) {
    WashburnPenetrationModel model;
    double h = model.penetration_depth_at_time(
        100.0, DEFAULT_CRACK_WIDTH_UM, 0.0, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(h));
    ASSERT_FALSE(std::isinf(h));
    ASSERT_GE(h, 0.0);
}

TEST(Washburn, 90DegreeContact_NoPenetration) {
    WashburnPenetrationModel model;
    double h1 = model.penetration_depth_at_time(
        10.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 90.0
    );
    double h2 = model.penetration_depth_at_time(
        1000.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 90.0
    );
    ASSERT_LT(h1, 0.01);
    ASSERT_LT(h2, 0.01);
}

TEST(Washburn, VeryThinCrack_SlowerPenetration) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t = 100.0;
    double h_thin = model.penetration_depth_at_time(
        t, 0.5, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double h_wide = model.penetration_depth_at_time(
        t, 50.0, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(h_thin, 0.0);
    ASSERT_GT(h_wide, 0.0);
    ASSERT_LT(h_thin, h_wide);
}

TEST(Washburn, LargeTime_DepthSaturationByGravity) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    cfg.gravity_m_s2 = 9.81;
    model.set_config(cfg);

    double t_short = 10.0;
    double t_long = 1e6;
    double h_short = model.penetration_depth_at_time(
        t_short, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double h_long = model.penetration_depth_at_time(
        t_long, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(h_short, 0.0);
    ASSERT_GT(h_long, 0.0);
    ASSERT_GT(h_long, h_short);

    double h_ideal_long = computeAnalyticalDepth(
        DEFAULT_GAMMA, DEFAULT_THETA_DEG, DEFAULT_CRACK_WIDTH_UM,
        DEFAULT_ETA, DEFAULT_TORTUOSITY, t_long
    );

    double deviation_pct = std::abs(h_long - h_ideal_long) / std::max(h_ideal_long, 1.0) * 100.0;
    ASSERT_GT(deviation_pct, 0.0);
}

TEST(Washburn, NegativeTime_HandlesGracefully) {
    WashburnPenetrationModel model;
    double h = model.penetration_depth_at_time(
        -10.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(h));
    ASSERT_FALSE(std::isinf(h));
    ASSERT_NEAR(h, 0.0, 1e-10);
}

TEST(Washburn, NegativeViscosity_NoCrash) {
    WashburnPenetrationModel model;
    double h = model.penetration_depth_at_time(
        100.0, DEFAULT_CRACK_WIDTH_UM, -1.0, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(h));
    ASSERT_FALSE(std::isinf(h));
    ASSERT_GE(h, 0.0);

    double t = model.time_to_reach_depth(
        100.0, DEFAULT_CRACK_WIDTH_UM, -1.0, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(t));
    ASSERT_FALSE(std::isinf(t));

    double rate = model.penetration_rate(
        50.0, DEFAULT_CRACK_WIDTH_UM, -1.0, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(rate));
    ASSERT_FALSE(std::isinf(rate));
}

TEST(Washburn, 180DegreeContact_Repulsive) {
    WashburnPenetrationModel model;
    double h = model.penetration_depth_at_time(
        100.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 180.0
    );
    ASSERT_FALSE(std::isnan(h));
    ASSERT_FALSE(std::isinf(h));
    ASSERT_LT(h, 0.01);

    double h2 = model.penetration_depth_at_time(
        100.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, 120.0
    );
    ASSERT_FALSE(std::isnan(h2));
    ASSERT_FALSE(std::isinf(h2));
    ASSERT_LT(h2, 0.01);
}

TEST(Washburn, Acceptance_PredictionErrorLessThan15pct) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.default_surface_tension_n_m = 0.072;
    cfg.default_contact_angle_deg = 30.0;
    cfg.default_viscosity_pa_s = 1.0;
    cfg.tortuosity_factor = DEFAULT_TORTUOSITY;
    cfg.surface_roughness_factor = DEFAULT_ROUGHNESS;
    model.set_config(cfg);

    double t = 1.0;
    double crack_width_um = 0.5;
    double eta_pa_s = 1.0;
    double gamma_n_m = 0.072;
    double theta_deg = 30.0;

    double h_pred = model.penetration_depth_at_time(
        t, crack_width_um, eta_pa_s, gamma_n_m, theta_deg
    );

    double expected_um = computeAnalyticalDepth(
        gamma_n_m, theta_deg, crack_width_um, eta_pa_s, DEFAULT_TORTUOSITY, t
    );
    ASSERT_GT(h_pred, 0.0);
    ASSERT_NEAR(h_pred, expected_um, 15.0);
}

TEST(WashburnModelTests, RoughnessFactor_ValidRange) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    model.set_config(cfg);

    double r_s_low = model.get_roughness_factor(0.1);
    ASSERT_GT(r_s_low, 1.0);
    ASSERT_LT(r_s_low, 1.2);

    double r_s_high = model.get_roughness_factor(5.0);
    ASSERT_GT(r_s_high, 1.0);

    ASSERT_GE(r_s_low, MIN_ROUGHNESS_FACTOR);
    ASSERT_LE(r_s_low, MAX_ROUGHNESS_FACTOR);
    ASSERT_GE(r_s_high, MIN_ROUGHNESS_FACTOR);
    ASSERT_LE(r_s_high, MAX_ROUGHNESS_FACTOR);

    double r_s_mid = model.get_roughness_factor(2.0);
    ASSERT_GE(r_s_mid, MIN_ROUGHNESS_FACTOR);
    ASSERT_LE(r_s_mid, MAX_ROUGHNESS_FACTOR);
}

TEST(WashburnModelTests, WenzelContactAngle_Corrected) {
    WashburnPenetrationModel model;
    WashburnModelTestAccessor accessor(model);

    double r_s = model.get_roughness_factor(2.0);
    ASSERT_GT(r_s, 1.0);

    double theta_w_30 = accessor.wenzel_contact_angle(30.0, r_s);
    ASSERT_LT(theta_w_30, 30.0);
    ASSERT_GT(theta_w_30, 0.0);

    double theta_w_90 = accessor.wenzel_contact_angle(90.0, r_s);
    ASSERT_NEAR(theta_w_90, 90.0, 1.0);

    double theta_w_120 = accessor.wenzel_contact_angle(120.0, r_s);
    ASSERT_GT(theta_w_120, 120.0);
    ASSERT_LT(theta_w_120, 180.0);
}

TEST(WashburnModelTests, Roughness_IncreasesTortuosity) {
    WashburnPenetrationModel model;
    WashburnModelTestAccessor accessor(model);

    double tau_eff1 = accessor.roughness_tortuosity(0.1);
    double tau_eff2 = accessor.roughness_tortuosity(5.0);

    ASSERT_GT(tau_eff2, tau_eff1);
    ASSERT_GT(tau_eff1, 0.0);
    ASSERT_GT(tau_eff2, 0.0);
}

TEST(WashburnModelTests, Roughness_ReducesEffectiveRadius) {
    WashburnPenetrationModel model;
    WashburnModelTestAccessor accessor(model);

    double base_r = 10.0;
    double r_eff1 = accessor.roughness_effective_radius(base_r, 0.1);
    double r_eff2 = accessor.roughness_effective_radius(base_r, 5.0);

    ASSERT_LT(r_eff2, r_eff1);
    ASSERT_GT(r_eff1, 0.0);
    ASSERT_GT(r_eff2, 0.0);
    ASSERT_LT(r_eff1, base_r);
    ASSERT_LT(r_eff2, base_r);
}

TEST(WashburnModelTests, PenetrationDepth_RoughnessCorrectionApplied) {
    WashburnPenetrationModel model_corrected;
    WashburnConfig cfg_corrected;
    cfg_corrected.wall_roughness_ra_um = 2.0;
    cfg_corrected.wenzel_roughness_correction = true;
    model_corrected.set_config(cfg_corrected);

    WashburnPenetrationModel model_uncorrected;
    WashburnConfig cfg_uncorrected;
    cfg_uncorrected.wall_roughness_ra_um = 2.0;
    cfg_uncorrected.wenzel_roughness_correction = false;
    model_uncorrected.set_config(cfg_uncorrected);

    double crack_width = 50.0;
    double t = 100.0;
    double eta = 1.0;

    double h_corrected = model_corrected.penetration_depth_at_time(
        t, crack_width, eta, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    double h_uncorrected = model_uncorrected.penetration_depth_at_time(
        t, crack_width, eta, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );

    ASSERT_GT(h_corrected, 0.0);
    ASSERT_GT(h_uncorrected, 0.0);
    ASSERT_FALSE(std::isnan(h_corrected));
    ASSERT_FALSE(std::isnan(h_uncorrected));

    CrackInfo crack = createTestCrack(crack_width * 1e-6);
    RepairMaterial mat = createTestMaterial(eta, DEFAULT_GAMMA, DEFAULT_THETA_DEG);

    auto result_corrected = model_corrected.predict(1, 1, crack, mat, 100.0);
    ASSERT_TRUE(result_corrected.roughness_correction_applied);

    auto result_uncorrected = model_uncorrected.predict(1, 1, crack, mat, 100.0);
    ASSERT_FALSE(result_uncorrected.roughness_correction_applied);
}

TEST(WashburnModelTests, PenetrationRate_WithRoughness) {
    WashburnPenetrationModel model_corrected;
    WashburnConfig cfg_corrected;
    cfg_corrected.wall_roughness_ra_um = 3.0;
    cfg_corrected.wenzel_roughness_correction = true;
    model_corrected.set_config(cfg_corrected);

    WashburnPenetrationModel model_uncorrected;
    WashburnConfig cfg_uncorrected;
    cfg_uncorrected.wall_roughness_ra_um = 3.0;
    cfg_uncorrected.wenzel_roughness_correction = false;
    model_uncorrected.set_config(cfg_uncorrected);

    double crack_width = 50.0;
    double eta = 1.0;
    double theta = 30.0;
    double h = 50.0;

    double rate_corrected = model_corrected.penetration_rate(
        h, crack_width, eta, DEFAULT_GAMMA, theta
    );
    double rate_uncorrected = model_uncorrected.penetration_rate(
        h, crack_width, eta, DEFAULT_GAMMA, theta
    );

    ASSERT_GT(rate_corrected, 0.0);
    ASSERT_GT(rate_uncorrected, 0.0);
    ASSERT_LT(rate_corrected, rate_uncorrected);
}

TEST(WashburnModelTests, RoughnessExtremes_Handled) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    model.set_config(cfg);

    WashburnModelTestAccessor accessor(model);

    double r_s_smooth = model.get_roughness_factor(0.0);
    ASSERT_NEAR(r_s_smooth, 1.0, 1e-10);

    double r_s_extreme = model.get_roughness_factor(10.0);
    ASSERT_FALSE(std::isnan(r_s_extreme));
    ASSERT_FALSE(std::isinf(r_s_extreme));
    ASSERT_GE(r_s_extreme, MIN_ROUGHNESS_FACTOR);
    ASSERT_LE(r_s_extreme, MAX_ROUGHNESS_FACTOR);

    double r_s_negative = model.get_roughness_factor(-1.0);
    ASSERT_FALSE(std::isnan(r_s_negative));
    ASSERT_FALSE(std::isinf(r_s_negative));
    ASSERT_NEAR(r_s_negative, 1.0, 1e-10);

    double h_smooth = model.penetration_depth_at_time(
        100.0, DEFAULT_CRACK_WIDTH_UM, DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG
    );
    ASSERT_FALSE(std::isnan(h_smooth));
    ASSERT_FALSE(std::isinf(h_smooth));
    ASSERT_GT(h_smooth, 0.0);

    double tau_extreme = accessor.roughness_tortuosity(10.0);
    ASSERT_FALSE(std::isnan(tau_extreme));
    ASSERT_FALSE(std::isinf(tau_extreme));
    ASSERT_GT(tau_extreme, 0.0);

    double r_extreme = accessor.roughness_effective_radius(10.0, 10.0);
    ASSERT_FALSE(std::isnan(r_extreme));
    ASSERT_FALSE(std::isinf(r_extreme));
    ASSERT_GT(r_extreme, 0.0);
}

TEST(WashburnModelTests, DynamicViscosityCorrection_ShearThinning) {
    WashburnPenetrationModel model;
    WashburnModelTestAccessor accessor(model);

    double eta0 = 1.0;

    double eta_low = accessor.dynamic_viscosity_correction(eta0, 1.0);
    ASSERT_GT(eta_low, 0.0);
    ASSERT_LE(eta_low, eta0);

    double eta_high = accessor.dynamic_viscosity_correction(eta0, 1e6);
    ASSERT_GT(eta_high, 0.0);
    ASSERT_LT(eta_high, eta0);
    ASSERT_LT(eta_high, eta_low);

    double eta_zero = accessor.dynamic_viscosity_correction(eta0, 0.0);
    ASSERT_NEAR(eta_zero, eta0, 1e-10);

    double eta_negative = accessor.dynamic_viscosity_correction(eta0, -1.0);
    ASSERT_NEAR(eta_negative, eta0, 1e-10);

    double eta_invalid = accessor.dynamic_viscosity_correction(-1.0, 1.0);
    ASSERT_NEAR(eta_invalid, 0.0, 1e-10);
}

TEST(WashburnModelTests, Predict_OutputsRoughnessParams) {
    WashburnPenetrationModel model;
    WashburnConfig cfg;
    cfg.wall_roughness_ra_um = 2.0;
    cfg.wenzel_roughness_correction = true;
    model.set_config(cfg);

    CrackInfo crack = createTestCrack(50e-6);
    RepairMaterial mat = createTestMaterial(DEFAULT_ETA, DEFAULT_GAMMA, DEFAULT_THETA_DEG);

    auto result = model.predict(1, 1, crack, mat, 200.0);

    ASSERT_GT(result.wall_roughness_ra_um, 0.0);
    ASSERT_GT(result.roughness_factor, 1.0);
    ASSERT_GT(result.wenzel_contact_angle, 0.0);
    ASSERT_LT(result.wenzel_contact_angle, 180.0);
    ASSERT_GT(result.effective_radius_um, 0.0);
    ASSERT_GT(result.effective_tortuosity, 1.0);
    ASSERT_TRUE(result.roughness_correction_applied);
}

#ifndef PORCELAIN_TESTS_COMBINED
int main() {
    return RUN_ALL_TESTS();
}
#endif
