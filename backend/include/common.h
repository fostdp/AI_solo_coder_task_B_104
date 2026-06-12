#pragma once

#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace porcelain_monitor {

using json = nlohmann::json;
using Vector3d = std::array<double, 3>;

enum class SensorType {
    LASER_CONFOCAL_MICROSCOPE,
    MICRO_VIBRATION_SENSOR
};

enum class AlertType {
    CRACK_DEPTH_EXCEEDED,
    CRACK_WIDTH_EXCEEDED,
    CRACK_PROPAGATION_RISK,
    VIBRATION_ANOMALY
};

enum class AlertStatus {
    PENDING,
    ACKNOWLEDGED,
    RESOLVED,
    IGNORED
};

enum class DynastyType {
    SONG,
    YUAN
};

enum class RepairMaterialType {
    ZIRCONIA,
    SILICA,
    COMPOSITE
};

struct Point3D {
    double x, y, z;
    double depth;
    double width;
    std::optional<Vector3d> normal;
    std::optional<double> curvature;
};

struct CrackInfo {
    int64_t id;
    int porcelain_id;
    std::string crack_code;
    double max_depth;
    double max_width;
    double total_length;
    std::string status;
    std::vector<Point3D> points;
    std::chrono::system_clock::time_point detected_at;
};

struct PorcelainInfo {
    int id;
    std::string museum_id;
    std::string name;
    DynastyType dynasty;
    int production_year;
    std::string description;
    std::string origin_location;
    json dimensions;
    std::optional<std::string> model_path;
};

struct LaserMicroscopeData {
    int64_t id;
    int sensor_id;
    int porcelain_id;
    std::chrono::system_clock::time_point measurement_time;
    std::array<double, 4> scan_area;
    double resolution;
    bool crack_detected;
    int crack_count;
    std::vector<CrackInfo> cracks;
    json processed_data;
};

struct VibrationData {
    int64_t id;
    int sensor_id;
    int porcelain_id;
    std::chrono::system_clock::time_point measurement_time;
    json frequency_spectrum;
    std::vector<double> amplitude;
    double rms_value;
    double peak_value;
    double dominant_frequency;
    double temperature;
    double humidity;
};

struct Alert {
    int64_t id;
    AlertType type;
    int porcelain_id;
    int crack_id;
    int sensor_id;
    double threshold_value;
    double actual_value;
    std::string unit;
    std::string message;
    AlertStatus status;
    bool sms_sent;
    bool websocket_sent;
    std::chrono::system_clock::time_point created_at;
};

struct CrackPrediction {
    int64_t id;
    int crack_id;
    std::string model_type;
    json parameters;
    int time_horizon_hours;
    double predicted_depth;
    double predicted_width;
    double predicted_length;
    double confidence;
    std::string risk_level;
};

struct RepairMaterial {
    int id;
    RepairMaterialType type;
    std::string name;
    std::string manufacturer;
    double particle_size_nm;
    double purity;
    double viscosity;
    double refractive_index;
    double thermal_expansion_coeff;
    json properties;
};

struct RepairSimulation {
    int64_t id;
    int crack_id;
    int material_id;
    std::string method;
    json parameters;
    int particle_count;
    double filling_rate;
    double bonding_strength;
    double surface_smoothness;
    double durability_score;
    json result;
};

struct ProfinetPacket {
    std::string source_ip;
    std::string destination_ip;
    std::string packet_type;
    uint16_t frame_id;
    std::vector<uint8_t> payload;
    std::chrono::system_clock::time_point received_at;
};

constexpr double ALERT_DEPTH_THRESHOLD = 200.0;
constexpr double ALERT_WIDTH_THRESHOLD = 50.0;

inline std::string to_string(SensorType type) {
    switch (type) {
        case SensorType::LASER_CONFOCAL_MICROSCOPE: return "LASER_CONFOCAL_MICROSCOPE";
        case SensorType::MICRO_VIBRATION_SENSOR: return "MICRO_VIBRATION_SENSOR";
    }
    return "UNKNOWN";
}

inline std::string to_string(AlertType type) {
    switch (type) {
        case AlertType::CRACK_DEPTH_EXCEEDED: return "CRACK_DEPTH_EXCEEDED";
        case AlertType::CRACK_WIDTH_EXCEEDED: return "CRACK_WIDTH_EXCEEDED";
        case AlertType::CRACK_PROPAGATION_RISK: return "CRACK_PROPAGATION_RISK";
        case AlertType::VIBRATION_ANOMALY: return "VIBRATION_ANOMALY";
    }
    return "UNKNOWN";
}

inline std::string to_string(RepairMaterialType type) {
    switch (type) {
        case RepairMaterialType::ZIRCONIA: return "ZIRCONIA";
        case RepairMaterialType::SILICA: return "SILICA";
        case RepairMaterialType::COMPOSITE: return "COMPOSITE";
    }
    return "UNKNOWN";
}

inline void to_json(json& j, const Point3D& p) {
    j = json{{"x", p.x}, {"y", p.y}, {"z", p.z}, {"depth", p.depth}, {"width", p.width}};
    if (p.normal) j["normal"] = *p.normal;
    if (p.curvature) j["curvature"] = *p.curvature;
}

inline void from_json(const json& j, Point3D& p) {
    j.at("x").get_to(p.x);
    j.at("y").get_to(p.y);
    j.at("z").get_to(p.z);
    j.at("depth").get_to(p.depth);
    j.at("width").get_to(p.width);
}

enum class PipelineMsgType {
    RAW_PROFINET,
    PARSED_LASER,
    PARSED_VIBRATION,
    CRACK_DETECTION,
    FATIGUE_PREDICTION,
    DEM_SIMULATION,
    ALERT_NOTIFY,
    WS_BROADCAST
};

struct ParsedLaserMessage {
    ProfinetPacket packet;
    LaserMicroscopeData data;
};

struct ParsedVibrationMessage {
    ProfinetPacket packet;
    VibrationData data;
};

struct CrackDetectionMessage {
    LaserMicroscopeData laser_data;
    int porcelain_id;
    std::vector<CrackInfo> cracks;
    std::vector<std::vector<Point3D>> crack_points;
};

struct FatiguePredictionMessage {
    CrackInfo crack;
    int porcelain_id;
    CrackPrediction prediction;
};

struct DemSimulationMessage {
    CrackInfo crack;
    int porcelain_id;
    RepairMaterial material;
    RepairSimulation result;
};

struct AlertNotifyMessage {
    Alert alert;
};

struct WsBroadcastMessage {
    json payload;
};

struct StressTensor {
    double sigma_xx;
    double sigma_yy;
    double sigma_zz;
    double tau_xy;
    double tau_yz;
    double tau_zx;
    double von_mises;
};

struct StressGridPoint {
    double x, y, z;
    StressTensor stress;
    double crack_density;
    double principal_direction;
    int refinement_level;
};

struct StressAnalysisResult {
    int64_t id;
    int porcelain_id;
    std::string method;
    json parameters;
    std::vector<StressGridPoint> grid_points;
    double max_von_mises;
    double avg_von_mises;
    double high_stress_area_ratio;
    json result;
    std::chrono::system_clock::time_point created_at;
};

struct PenetrationPrediction {
    int64_t id = 0;
    int crack_id = 0;
    int material_id = 0;
    std::string method;
    json parameters;
    bool success = true;
    double target_depth_um = 0.0;
    double viscosity_pa_s = 0.0;
    double surface_tension_n_m = 0.0;
    double contact_angle_deg = 0.0;
    double crack_width_um = 0.0;
    double predicted_time_s = 0.0;
    double penetration_rate_um_s = 0.0;
    std::vector<double> time_series;
    std::vector<double> depth_series;
    json result;
    std::chrono::system_clock::time_point created_at;
    double wall_roughness_ra_um = 0.0;
    double roughness_factor = 0.0;
    double wenzel_contact_angle = 0.0;
    double effective_radius_um = 0.0;
    double effective_tortuosity = 0.0;
    bool roughness_correction_applied = false;
};

struct BendingTestResult {
    int64_t id = 0;
    int porcelain_id = 0;
    int crack_id = 0;
    int material_id = 0;
    std::string method;
    json parameters;
    bool success = true;
    double original_strength_mpa = 0.0;
    double unrepaired_strength_mpa = 0.0;
    double repaired_strength_mpa = 0.0;
    double strength_recovery_ratio = 0.0;
    double youngs_modulus_gpa = 0.0;
    double fracture_toughness_mpa_m05 = 0.0;
    std::vector<double> load_displacement_load;
    std::vector<double> load_displacement_disp;
    json stress_distribution;
    json result;
    std::chrono::system_clock::time_point created_at;
};

struct VirtualRepairResult {
    int porcelain_id;
    int crack_id;
    std::vector<int> repaired_point_indices;
    double repair_radius;
    double estimated_closure_ratio;
    json animation_data;
};

struct StressAnalysisMessage {
    int porcelain_id;
    std::vector<CrackInfo> cracks;
    StressAnalysisResult result;
};

struct PenetrationMessage {
    CrackInfo crack;
    RepairMaterial material;
    PenetrationPrediction prediction;
};

struct BendingTestMessage {
    int porcelain_id;
    CrackInfo crack;
    RepairMaterial material;
    BendingTestResult result;
};

inline void to_json(json& j, const StressTensor& s) {
    j = json{
        {"sigma_xx", s.sigma_xx},
        {"sigma_yy", s.sigma_yy},
        {"sigma_zz", s.sigma_zz},
        {"tau_xy", s.tau_xy},
        {"tau_yz", s.tau_yz},
        {"tau_zx", s.tau_zx},
        {"von_mises", s.von_mises}
    };
}

inline void from_json(const json& j, StressTensor& s) {
    j.at("sigma_xx").get_to(s.sigma_xx);
    j.at("sigma_yy").get_to(s.sigma_yy);
    j.at("sigma_zz").get_to(s.sigma_zz);
    j.at("tau_xy").get_to(s.tau_xy);
    j.at("tau_yz").get_to(s.tau_yz);
    j.at("tau_zx").get_to(s.tau_zx);
    j.at("von_mises").get_to(s.von_mises);
}

inline void to_json(json& j, const StressGridPoint& p) {
    j = json{
        {"x", p.x}, {"y", p.y}, {"z", p.z},
        {"stress", p.stress},
        {"crack_density", p.crack_density},
        {"principal_direction", p.principal_direction},
        {"refinement_level", p.refinement_level}
    };
}

}
