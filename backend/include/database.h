#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <libpq-fe.h>
#include "common.h"

namespace porcelain_monitor {

class DatabaseConnection {
public:
    DatabaseConnection(const std::string& conninfo);
    ~DatabaseConnection();

    bool connect();
    void disconnect();
    bool is_connected() const;
    PGconn* get() { return conn_; }

    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

private:
    std::string conninfo_;
    PGconn* conn_ = nullptr;
};

class DatabasePool {
public:
    DatabasePool(const std::string& conninfo, int pool_size = 10);
    ~DatabasePool();

    std::shared_ptr<DatabaseConnection> acquire();
    void release(std::shared_ptr<DatabaseConnection> conn);

private:
    std::string conninfo_;
    std::vector<std::shared_ptr<DatabaseConnection>> pool_;
    std::vector<std::shared_ptr<DatabaseConnection>> available_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

class DatabaseManager {
public:
    static DatabaseManager& instance();

    void init(const std::string& host, uint16_t port,
              const std::string& dbname, const std::string& user,
              const std::string& password, int pool_size = 10);

    std::shared_ptr<DatabasePool> pool() { return pool_; }

    int64_t insert_laser_data(const LaserMicroscopeData& data);
    int64_t insert_vibration_data(const VibrationData& data);
    int64_t insert_crack(const CrackInfo& crack, int porcelain_id);
    int64_t insert_crack_points(int64_t crack_id, const std::vector<Point3D>& points);
    int64_t insert_alert(const Alert& alert);
    int64_t insert_prediction(const CrackPrediction& prediction);
    int64_t insert_simulation(const RepairSimulation& simulation);
    void insert_profinet_packet(const ProfinetPacket& packet);

    bool batch_insert_laser_data(const std::vector<LaserMicroscopeData>& data_list);
    bool batch_insert_vibration_data(const std::vector<VibrationData>& data_list);
    bool batch_insert_crack_detections(const LaserMicroscopeData& laser_data,
                                        const std::vector<CrackInfo>& cracks,
                                        const std::vector<std::vector<Point3D>>& crack_points,
                                        int porcelain_id);

    std::vector<PorcelainInfo> get_all_porcelains();
    PorcelainInfo get_porcelain(int id);
    std::vector<CrackInfo> get_cracks_by_porcelain(int porcelain_id, int limit = 100);
    CrackInfo get_crack(int crack_id);
    std::vector<Point3D> get_crack_points(int crack_id);
    std::vector<LaserMicroscopeData> get_laser_data(int porcelain_id, int hours = 24);
    std::vector<VibrationData> get_vibration_data(int porcelain_id, int hours = 24);
    std::vector<Alert> get_active_alerts();
    std::vector<CrackPrediction> get_predictions(int crack_id);
    std::vector<RepairSimulation> get_simulations(int crack_id);
    std::vector<RepairMaterial> get_repair_materials();

    bool update_alert_status(int64_t alert_id, AlertStatus status);

    void log_profinet_packet(const std::string& source_ip,
                             const std::string& dest_ip,
                             uint16_t frame_id,
                             const std::vector<uint8_t>& payload);

    int64_t insert_stress_analysis(const StressAnalysisResult& result);
    bool insert_stress_grid_points(int64_t analysis_id,
                                    const std::vector<StressGridPoint>& points);
    StressAnalysisResult get_latest_stress_analysis(int porcelain_id);
    std::vector<StressAnalysisResult> get_stress_analysis_history(int porcelain_id, int limit = 10);

    int64_t insert_penetration_prediction(const PenetrationPrediction& prediction);
    std::vector<PenetrationPrediction> get_penetration_predictions(int crack_id);

    int64_t insert_bending_test_result(const BendingTestResult& result);
    std::vector<BendingTestResult> get_bending_test_results(int porcelain_id, int crack_id = 0);

    int64_t insert_virtual_repair_record(int porcelain_id, int crack_id,
                                          const json& repaired_region,
                                          double repair_radius,
                                          double closure_ratio,
                                          const json& animation_data);

private:
    DatabaseManager() = default;
    ~DatabaseManager() = default;

    std::string build_conninfo(const std::string& host, uint16_t port,
                               const std::string& dbname, const std::string& user,
                               const std::string& password);

    std::shared_ptr<DatabasePool> pool_;
    std::mutex mutex_;
};

}
