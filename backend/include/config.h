#pragma once

#include <string>
#include <cstdint>
#include <cstdlib>

namespace porcelain_monitor {
namespace config {

struct DatabaseConfig {
    std::string host = "localhost";
    uint16_t port = 5432;
    std::string name = "porcelain_monitor";
    std::string user = "postgres";
    std::string password = "postgres";
    int pool_size = 10;
    int max_pool_size = 50;
    int connection_timeout_ms = 5000;

    void apply_env() {
        const char* env = std::getenv("PM_DB_HOST");
        if (env) host = env;
        env = std::getenv("PM_DB_PORT");
        if (env) port = static_cast<uint16_t>(std::atoi(env));
        env = std::getenv("PM_DB_NAME");
        if (env) name = env;
        env = std::getenv("PM_DB_USER");
        if (env) user = env;
        env = std::getenv("PM_DB_PASSWORD");
        if (env) password = env;
        env = std::getenv("PM_DB_POOL_SIZE");
        if (env) {
            pool_size = std::atoi(env);
            pool_size = std::max(1, std::min(pool_size, max_pool_size));
        }
    }
};

struct ServerConfig {
    uint16_t profinet_port = 34964;
    uint16_t http_port = 8080;
    uint16_t websocket_port = 8081;
    std::string bind_address = "0.0.0.0";
    int thread_pool_size = 8;

    void apply_env() {
        const char* env = std::getenv("PM_PROFINET_PORT");
        if (env) profinet_port = static_cast<uint16_t>(std::atoi(env));
        env = std::getenv("PM_HTTP_PORT");
        if (env) http_port = static_cast<uint16_t>(std::atoi(env));
        env = std::getenv("PM_WS_PORT");
        if (env) websocket_port = static_cast<uint16_t>(std::atoi(env));
        env = std::getenv("PM_THREAD_POOL");
        if (env) thread_pool_size = std::max(1, std::atoi(env));
    }
};

struct AlertConfig {
    double depth_threshold = 200.0;
    double width_threshold = 50.0;
    bool sms_enabled = true;
    bool websocket_enabled = true;
    std::string sms_gateway_url = "http://sms-gateway.example.com/send";
    std::string alert_phone_number = "+8613800138000";

    void apply_env() {
        const char* env = std::getenv("PM_DEPTH_THRESHOLD");
        if (env) depth_threshold = std::atof(env);
        env = std::getenv("PM_WIDTH_THRESHOLD");
        if (env) width_threshold = std::atof(env);
        env = std::getenv("PM_SMS_ENABLED");
        if (env) sms_enabled = (std::string(env) == "1" || std::string(env) == "true");
    }
};

struct StressFEMConfig {
    int grid_resolution = 50;
    double youngs_modulus_gpa = 70.0;
    double poissons_ratio = 0.22;
    double crack_density_sensitivity = 50.0;
    double max_stress_mpa = 300.0;
    bool use_adaptive_mesh = true;
    int max_refinement_level = 3;
    double refinement_stress_gradient_threshold = 20.0;
    double refinement_crack_density_threshold = 0.3;
    double coarsening_stress_gradient_threshold = 5.0;
    int min_grid_resolution = 20;
    int max_total_nodes = 500000;
};

struct WashburnConfig {
    double default_surface_tension_n_m = 0.072;
    double default_contact_angle_deg = 30.0;
    double tortuosity_factor = 1.5;
    double surface_roughness_factor = 1.2;
    int time_series_points = 100;
    double wall_roughness_ra_um = 0.5;
    bool wenzel_roughness_correction = true;
    double roughness_tortuosity_coeff = 0.3;
    double roughness_radius_coeff = 0.15;
    double min_roughness_factor = 0.1;
    double max_roughness_factor = 3.0;
};

struct BendingTestConfig {
    double support_span_mm = 40.0;
    double loading_span_mm = 20.0;
    double specimen_width_mm = 10.0;
    double specimen_thickness_mm = 5.0;
    double porcelain_strength_mpa = 120.0;
    double repair_interface_strength_ratio = 0.93;
    int load_steps = 50;
    bool enable_bayesian_calibration = false;
    int calibration_max_iter = 50;
    int calibration_initial_samples = 10;
    double calibration_exploration_weight = 0.01;
    double calibration_noise_std = 0.05;
};

struct AlgorithmConfig {
    struct ParisLaw {
        double default_C = 1.5e-10;
        double default_m = 3.0;
        double stress_ratio = 0.1;
        int prediction_horizon_hours = 720;
    } paris_law;

    struct DEM {
        double particle_radius_nm = 25.0;
        double youngs_modulus = 70e9;
        double poissons_ratio = 0.22;
        double density = 3950.0;
        int max_particles = 10000;
        int simulation_steps = 1000;
        double time_step = 1e-9;
    } dem;

    StressFEMConfig stress_fem;
    WashburnConfig washburn;
    BendingTestConfig bending_test;
};

struct Config {
    DatabaseConfig database;
    ServerConfig server;
    AlertConfig alerts;
    AlgorithmConfig algorithms;

    void apply_env() {
        database.apply_env();
        server.apply_env();
        alerts.apply_env();
    }
};

inline Config& get_config() {
    static Config config;
    return config;
}

}
}
