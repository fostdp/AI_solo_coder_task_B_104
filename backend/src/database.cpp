#include "database.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace porcelain_monitor {

DatabaseConnection::DatabaseConnection(const std::string& conninfo)
    : conninfo_(conninfo) {}

DatabaseConnection::~DatabaseConnection() {
    disconnect();
}

bool DatabaseConnection::connect() {
    conn_ = PQconnectdb(conninfo_.c_str());
    return is_connected();
}

void DatabaseConnection::disconnect() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool DatabaseConnection::is_connected() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

bool DatabaseConnection::begin_transaction() {
    if (!is_connected()) return false;
    PGresult* res = PQexec(conn_, "BEGIN");
    bool success = res && PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return success;
}

bool DatabaseConnection::commit_transaction() {
    if (!is_connected()) return false;
    PGresult* res = PQexec(conn_, "COMMIT");
    bool success = res && PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return success;
}

bool DatabaseConnection::rollback_transaction() {
    if (!is_connected()) return false;
    PGresult* res = PQexec(conn_, "ROLLBACK");
    bool success = res && PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return success;
}

DatabasePool::DatabasePool(const std::string& conninfo, int pool_size)
    : conninfo_(conninfo) {
    for (int i = 0; i < pool_size; ++i) {
        auto conn = std::make_shared<DatabaseConnection>(conninfo_);
        if (conn->connect()) {
            pool_.push_back(conn);
            available_.push_back(conn);
        }
    }
}

DatabasePool::~DatabasePool() {
    pool_.clear();
    available_.clear();
}

std::shared_ptr<DatabaseConnection> DatabasePool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty(); });
    auto conn = available_.back();
    available_.pop_back();
    return conn;
}

void DatabasePool::release(std::shared_ptr<DatabaseConnection> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    available_.push_back(conn);
    cv_.notify_one();
}

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager instance;
    return instance;
}

std::string DatabaseManager::build_conninfo(const std::string& host, uint16_t port,
                                             const std::string& dbname, const std::string& user,
                                             const std::string& password) {
    std::ostringstream oss;
    oss << "host=" << host << " port=" << port
        << " dbname=" << dbname << " user=" << user
        << " password=" << password;
    return oss.str();
}

void DatabaseManager::init(const std::string& host, uint16_t port,
                           const std::string& dbname, const std::string& user,
                           const std::string& password, int pool_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string conninfo = build_conninfo(host, port, dbname, user, password);
    pool_ = std::make_shared<DatabasePool>(conninfo, pool_size);
    std::cout << "Database pool initialized with " << pool_size << " connections" << std::endl;
}

int64_t DatabaseManager::insert_laser_data(const LaserMicroscopeData& data) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(data.measurement_time);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream scan_area_ss;
    scan_area_ss << "ARRAY[" << data.scan_area[0] << "," << data.scan_area[1]
                 << "," << data.scan_area[2] << "," << data.scan_area[3] << "]::numeric[]";

    std::string processed_data_str = data.processed_data.empty() ?
        "NULL" : ("'" + data.processed_data.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO laser_microscope_data "
          << "(sensor_id, porcelain_id, measurement_time, scan_area, resolution, "
          << "crack_detected, crack_count, processed_data) VALUES ("
          << data.sensor_id << ", "
          << data.porcelain_id << ", '"
          << time_ss.str() << "', "
          << scan_area_ss.str() << ", "
          << data.resolution << ", "
          << (data.crack_detected ? "TRUE" : "FALSE") << ", "
          << data.crack_count << ", "
          << processed_data_str << ") RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting laser data: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

int64_t DatabaseManager::insert_vibration_data(const VibrationData& data) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(data.measurement_time);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream amp_ss;
    amp_ss << "ARRAY[";
    for (size_t i = 0; i < data.amplitude.size(); ++i) {
        if (i > 0) amp_ss << ",";
        amp_ss << data.amplitude[i];
    }
    amp_ss << "]::numeric[]";

    std::string freq_spec_str = data.frequency_spectrum.empty() ?
        "NULL" : ("'" + data.frequency_spectrum.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO vibration_data "
          << "(sensor_id, porcelain_id, measurement_time, frequency_spectrum, amplitude, "
          << "rms_value, peak_value, dominant_frequency, temperature, humidity) VALUES ("
          << data.sensor_id << ", "
          << data.porcelain_id << ", '"
          << time_ss.str() << "', "
          << freq_spec_str << ", "
          << amp_ss.str() << ", "
          << data.rms_value << ", "
          << data.peak_value << ", "
          << data.dominant_frequency << ", "
          << data.temperature << ", "
          << data.humidity << ") RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting vibration data: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

int64_t DatabaseManager::insert_crack(const CrackInfo& crack, int porcelain_id) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(crack.detected_at);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream query;
    query << "INSERT INTO cracks "
          << "(porcelain_id, crack_code, detected_date, max_depth, max_width, total_length) VALUES ("
          << porcelain_id << ", '"
          << crack.crack_code << "', '"
          << time_ss.str() << "', "
          << crack.max_depth << ", "
          << crack.max_width << ", "
          << crack.total_length << ") RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting crack: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

int64_t DatabaseManager::insert_crack_points(int64_t crack_id, const std::vector<Point3D>& points) {
    auto conn = pool_->acquire();

    if (!conn->begin_transaction()) {
        std::cerr << "Failed to begin transaction" << std::endl;
        pool_->release(conn);
        return -1;
    }

    int64_t last_id = -1;
    for (const auto& point : points) {
        std::ostringstream point_ss, normal_ss;
        point_ss << "ARRAY[" << point.x << "," << point.y << "," << point.z << "]::numeric[]";
        if (point.normal) {
            normal_ss << "ARRAY[" << (*point.normal)[0] << "," << (*point.normal)[1]
                      << "," << (*point.normal)[2] << "]::numeric[]";
        } else {
            normal_ss << "NULL";
        }

        std::ostringstream query;
        query << "INSERT INTO crack_points "
              << "(crack_id, point_3d, normal_vector, depth, width, curvature) VALUES ("
              << crack_id << ", "
              << point_ss.str() << ", "
              << normal_ss.str() << ", "
              << point.depth << ", "
              << point.width << ", "
              << (point.curvature ? std::to_string(*point.curvature) : "NULL")
              << ") RETURNING id";

        PGresult* res = PQexec(conn->get(), query.str().c_str());
        if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            last_id = std::atoll(PQgetvalue(res, 0, 0));
        }
        PQclear(res);
    }

    if (!conn->commit_transaction()) {
        std::cerr << "Failed to commit transaction" << std::endl;
        conn->rollback_transaction();
        pool_->release(conn);
        return -1;
    }

    pool_->release(conn);
    return last_id;
}

int64_t DatabaseManager::insert_alert(const Alert& alert) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(alert.created_at);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream query;
    query << "INSERT INTO alerts "
          << "(alert_type, porcelain_id, crack_id, sensor_id, threshold_value, "
          << "actual_value, unit, message, status, sms_sent, websocket_sent) VALUES ("
          << "'" << to_string(alert.type) << "'::alert_type, "
          << alert.porcelain_id << ", "
          << alert.crack_id << ", "
          << alert.sensor_id << ", "
          << alert.threshold_value << ", "
          << alert.actual_value << ", '"
          << alert.unit << "', '"
          << alert.message << "', 'PENDING'::alert_status, "
          << (alert.sms_sent ? "TRUE" : "FALSE") << ", "
          << (alert.websocket_sent ? "TRUE" : "FALSE")
          << ") RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting alert: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

int64_t DatabaseManager::insert_prediction(const CrackPrediction& prediction) {
    auto conn = pool_->acquire();

    std::ostringstream query;
    query << "INSERT INTO crack_propagation_predictions "
          << "(crack_id, model_type, parameters, time_horizon_hours, "
          << "predicted_depth, predicted_width, predicted_length, confidence, risk_level) VALUES ("
          << prediction.crack_id << ", '"
          << prediction.model_type << "', '"
          << prediction.parameters.dump() << "'::jsonb, "
          << prediction.time_horizon_hours << ", "
          << prediction.predicted_depth << ", "
          << prediction.predicted_width << ", "
          << prediction.predicted_length << ", "
          << prediction.confidence << ", '"
          << prediction.risk_level << "') RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting prediction: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

int64_t DatabaseManager::insert_simulation(const RepairSimulation& simulation) {
    auto conn = pool_->acquire();

    std::ostringstream query;
    query << "INSERT INTO repair_simulations "
          << "(crack_id, material_id, method, parameters, particle_count, "
          << "filling_rate, bonding_strength, surface_smoothness, durability_score, simulation_result) VALUES ("
          << simulation.crack_id << ", "
          << simulation.material_id << ", '"
          << simulation.method << "', '"
          << simulation.parameters.dump() << "'::jsonb, "
          << simulation.particle_count << ", "
          << simulation.filling_rate << ", "
          << simulation.bonding_strength << ", "
          << simulation.surface_smoothness << ", "
          << simulation.durability_score << ", '"
          << simulation.result.dump() << "'::jsonb) RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting simulation: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

void DatabaseManager::log_profinet_packet(const std::string& source_ip,
                                           const std::string& dest_ip,
                                           uint16_t frame_id,
                                           const std::vector<uint8_t>& payload) {
    auto conn = pool_->acquire();

    json payload_json;
    payload_json["raw"] = std::string(payload.begin(), payload.end());
    payload_json["size"] = payload.size();

    std::ostringstream query;
    query << "INSERT INTO profinet_packets "
          << "(source_ip, destination_ip, frame_id, payload_length, payload) VALUES ("
          << "'" << source_ip << "'::inet, '"
          << dest_ip << "'::inet, "
          << frame_id << ", "
          << payload.size() << ", '"
          << payload_json.dump() << "'::jsonb)";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::cerr << "Error logging PROFINET packet: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
}

std::vector<PorcelainInfo> DatabaseManager::get_all_porcelains() {
    auto conn = pool_->acquire();
    std::vector<PorcelainInfo> result;

    std::string query = "SELECT id, museum_id, name, dynasty, production_year, "
                        "description, origin_location, dimensions, model_path "
                        "FROM porcelains ORDER BY id";

    PGresult* res = PQexec(conn->get(), query.c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            PorcelainInfo p;
            p.id = std::atoi(PQgetvalue(res, i, 0));
            p.museum_id = PQgetvalue(res, i, 1);
            p.name = PQgetvalue(res, i, 2);
            p.dynasty = std::string(PQgetvalue(res, i, 3)) == "SONG" ?
                DynastyType::SONG : DynastyType::YUAN;
            p.production_year = std::atoi(PQgetvalue(res, i, 4));
            p.description = PQgetvalue(res, i, 5);
            p.origin_location = PQgetvalue(res, i, 6);
            if (PQgetvalue(res, i, 7) && PQgetlength(res, i, 7) > 0) {
                p.dimensions = json::parse(PQgetvalue(res, i, 7));
            }
            if (PQgetvalue(res, i, 8) && PQgetlength(res, i, 8) > 0) {
                p.model_path = PQgetvalue(res, i, 8);
            }
            result.push_back(p);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

PorcelainInfo DatabaseManager::get_porcelain(int id) {
    auto conn = pool_->acquire();
    PorcelainInfo result{};

    std::ostringstream query;
    query << "SELECT id, museum_id, name, dynasty, production_year, "
          << "description, origin_location, dimensions, model_path "
          << "FROM porcelains WHERE id = " << id;

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        result.id = std::atoi(PQgetvalue(res, 0, 0));
        result.museum_id = PQgetvalue(res, 0, 1);
        result.name = PQgetvalue(res, 0, 2);
        result.dynasty = std::string(PQgetvalue(res, 0, 3)) == "SONG" ?
            DynastyType::SONG : DynastyType::YUAN;
        result.production_year = std::atoi(PQgetvalue(res, 0, 4));
        result.description = PQgetvalue(res, 0, 5);
        result.origin_location = PQgetvalue(res, 0, 6);
        if (PQgetvalue(res, 0, 7) && PQgetlength(res, 0, 7) > 0) {
            result.dimensions = json::parse(PQgetvalue(res, 0, 7));
        }
        if (PQgetvalue(res, 0, 8) && PQgetlength(res, 0, 8) > 0) {
            result.model_path = PQgetvalue(res, 0, 8);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

std::vector<CrackInfo> DatabaseManager::get_cracks_by_porcelain(int porcelain_id, int limit) {
    auto conn = pool_->acquire();
    std::vector<CrackInfo> result;

    std::ostringstream query;
    query << "SELECT id, porcelain_id, crack_code, max_depth, max_width, total_length, "
          << "status, detected_date FROM cracks "
          << "WHERE porcelain_id = " << porcelain_id
          << " ORDER BY detected_date DESC LIMIT " << limit;

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            CrackInfo c;
            c.id = std::atoll(PQgetvalue(res, i, 0));
            c.porcelain_id = std::atoi(PQgetvalue(res, i, 1));
            c.crack_code = PQgetvalue(res, i, 2);
            c.max_depth = std::atof(PQgetvalue(res, i, 3));
            c.max_width = std::atof(PQgetvalue(res, i, 4));
            c.total_length = std::atof(PQgetvalue(res, i, 5));
            c.status = PQgetvalue(res, i, 6);
            std::string date_str = PQgetvalue(res, i, 7);
            result.push_back(c);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

CrackInfo DatabaseManager::get_crack(int crack_id) {
    auto conn = pool_->acquire();
    CrackInfo result{};

    std::ostringstream query;
    query << "SELECT id, porcelain_id, crack_code, max_depth, max_width, total_length, "
          << "status, detected_date FROM cracks WHERE id = " << crack_id;

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        result.id = std::atoll(PQgetvalue(res, 0, 0));
        result.porcelain_id = std::atoi(PQgetvalue(res, 0, 1));
        result.crack_code = PQgetvalue(res, 0, 2);
        result.max_depth = std::atof(PQgetvalue(res, 0, 3));
        result.max_width = std::atof(PQgetvalue(res, 0, 4));
        result.total_length = std::atof(PQgetvalue(res, 0, 5));
        result.status = PQgetvalue(res, 0, 6);
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

std::vector<Point3D> DatabaseManager::get_crack_points(int crack_id) {
    auto conn = pool_->acquire();
    std::vector<Point3D> result;

    std::ostringstream query;
    query << "SELECT point_3d[1], point_3d[2], point_3d[3], depth, width, "
          << "normal_vector[1], normal_vector[2], normal_vector[3], curvature "
          << "FROM crack_points WHERE crack_id = " << crack_id
          << " ORDER BY id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            Point3D p;
            p.x = std::atof(PQgetvalue(res, i, 0));
            p.y = std::atof(PQgetvalue(res, i, 1));
            p.z = std::atof(PQgetvalue(res, i, 2));
            p.depth = std::atof(PQgetvalue(res, i, 3));
            p.width = std::atof(PQgetvalue(res, i, 4));
            if (PQgetvalue(res, i, 5) && PQgetlength(res, i, 5) > 0) {
                p.normal = std::array<double, 3>{
                    std::atof(PQgetvalue(res, i, 5)),
                    std::atof(PQgetvalue(res, i, 6)),
                    std::atof(PQgetvalue(res, i, 7))
                };
            }
            if (PQgetvalue(res, i, 8) && PQgetlength(res, i, 8) > 0) {
                p.curvature = std::atof(PQgetvalue(res, i, 8));
            }
            result.push_back(p);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

std::vector<Alert> DatabaseManager::get_active_alerts() {
    auto conn = pool_->acquire();
    std::vector<Alert> result;

    std::string query = "SELECT id, alert_type, porcelain_id, crack_id, sensor_id, "
                        "threshold_value, actual_value, unit, message, status, created_at "
                        "FROM active_alerts ORDER BY created_at DESC LIMIT 100";

    PGresult* res = PQexec(conn->get(), query.c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            Alert a;
            a.id = std::atoll(PQgetvalue(res, i, 0));
            std::string type_str = PQgetvalue(res, i, 1);
            if (type_str == "CRACK_DEPTH_EXCEEDED") a.type = AlertType::CRACK_DEPTH_EXCEEDED;
            else if (type_str == "CRACK_WIDTH_EXCEEDED") a.type = AlertType::CRACK_WIDTH_EXCEEDED;
            else if (type_str == "CRACK_PROPAGATION_RISK") a.type = AlertType::CRACK_PROPAGATION_RISK;
            else a.type = AlertType::VIBRATION_ANOMALY;
            a.porcelain_id = std::atoi(PQgetvalue(res, i, 2));
            a.crack_id = std::atoi(PQgetvalue(res, i, 3));
            a.sensor_id = std::atoi(PQgetvalue(res, i, 4));
            a.threshold_value = std::atof(PQgetvalue(res, i, 5));
            a.actual_value = std::atof(PQgetvalue(res, i, 6));
            a.unit = PQgetvalue(res, i, 7);
            a.message = PQgetvalue(res, i, 8);
            a.status = AlertStatus::PENDING;
            result.push_back(a);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

std::vector<RepairMaterial> DatabaseManager::get_repair_materials() {
    auto conn = pool_->acquire();
    std::vector<RepairMaterial> result;

    std::string query = "SELECT id, material_type, name, manufacturer, particle_size_nm, "
                        "purity, viscosity, refractive_index, thermal_expansion_coeff, properties "
                        "FROM repair_materials ORDER BY id";

    PGresult* res = PQexec(conn->get(), query.c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            RepairMaterial m;
            m.id = std::atoi(PQgetvalue(res, i, 0));
            std::string type_str = PQgetvalue(res, i, 1);
            if (type_str == "ZIRCONIA") m.type = RepairMaterialType::ZIRCONIA;
            else if (type_str == "SILICA") m.type = RepairMaterialType::SILICA;
            else m.type = RepairMaterialType::COMPOSITE;
            m.name = PQgetvalue(res, i, 2);
            m.manufacturer = PQgetvalue(res, i, 3);
            m.particle_size_nm = std::atof(PQgetvalue(res, i, 4));
            m.purity = std::atof(PQgetvalue(res, i, 5));
            m.viscosity = std::atof(PQgetvalue(res, i, 6));
            m.refractive_index = std::atof(PQgetvalue(res, i, 7));
            m.thermal_expansion_coeff = std::atof(PQgetvalue(res, i, 8));
            if (PQgetvalue(res, i, 9) && PQgetlength(res, i, 9) > 0) {
                m.properties = json::parse(PQgetvalue(res, i, 9));
            }
            result.push_back(m);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

bool DatabaseManager::update_alert_status(int64_t alert_id, AlertStatus status) {
    auto conn = pool_->acquire();
    std::string status_str;
    switch (status) {
        case AlertStatus::ACKNOWLEDGED: status_str = "ACKNOWLEDGED"; break;
        case AlertStatus::RESOLVED: status_str = "RESOLVED"; break;
        case AlertStatus::IGNORED: status_str = "IGNORED"; break;
        default: status_str = "PENDING";
    }

    std::ostringstream query;
    query << "UPDATE alerts SET status = '" << status_str << "'::alert_status, "
          << "updated_at = CURRENT_TIMESTAMP ";
    if (status == AlertStatus::ACKNOWLEDGED) {
        query << ", acknowledged_at = CURRENT_TIMESTAMP ";
    } else if (status == AlertStatus::RESOLVED) {
        query << ", resolved_at = CURRENT_TIMESTAMP ";
    }
    query << "WHERE id = " << alert_id;

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    bool success = res && PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    pool_->release(conn);
    return success;
}

bool DatabaseManager::batch_insert_laser_data(const std::vector<LaserMicroscopeData>& data_list) {
    if (data_list.empty()) return true;

    auto conn = pool_->acquire();
    if (!conn->begin_transaction()) {
        pool_->release(conn);
        return false;
    }

    bool ok = true;
    for (const auto& data : data_list) {
        auto ts = std::chrono::system_clock::to_time_t(data.measurement_time);
        std::tm tm_buf;
#ifdef _WIN32
        gmtime_s(&tm_buf, &ts);
#else
        gmtime_r(&tm_buf, &ts);
#endif
        std::ostringstream time_ss;
        time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

        std::ostringstream scan_area_ss;
        scan_area_ss << "ARRAY[" << data.scan_area[0] << "," << data.scan_area[1]
                     << "," << data.scan_area[2] << "," << data.scan_area[3] << "]::numeric[]";

        std::string processed_data_str = data.processed_data.empty() ?
            "NULL" : ("'" + data.processed_data.dump() + "'::jsonb");

        std::ostringstream query;
        query << "INSERT INTO laser_microscope_data "
              << "(sensor_id, porcelain_id, measurement_time, scan_area, resolution, "
              << "crack_detected, crack_count, processed_data) VALUES ("
              << data.sensor_id << ", "
              << data.porcelain_id << ", '"
              << time_ss.str() << "', "
              << scan_area_ss.str() << ", "
              << data.resolution << ", "
              << (data.crack_detected ? "TRUE" : "FALSE") << ", "
              << data.crack_count << ", "
              << processed_data_str << ")";

        PGresult* res = PQexec(conn->get(), query.str().c_str());
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Batch insert laser data error: " << PQerrorMessage(conn->get()) << std::endl;
            ok = false;
            PQclear(res);
            break;
        }
        PQclear(res);
    }

    if (ok) {
        conn->commit_transaction();
    } else {
        conn->rollback_transaction();
    }
    pool_->release(conn);
    return ok;
}

bool DatabaseManager::batch_insert_vibration_data(const std::vector<VibrationData>& data_list) {
    if (data_list.empty()) return true;

    auto conn = pool_->acquire();
    if (!conn->begin_transaction()) {
        pool_->release(conn);
        return false;
    }

    bool ok = true;
    for (const auto& data : data_list) {
        auto ts = std::chrono::system_clock::to_time_t(data.measurement_time);
        std::tm tm_buf;
#ifdef _WIN32
        gmtime_s(&tm_buf, &ts);
#else
        gmtime_r(&tm_buf, &ts);
#endif
        std::ostringstream time_ss;
        time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

        std::ostringstream amp_ss;
        amp_ss << "ARRAY[";
        for (size_t i = 0; i < data.amplitude.size(); ++i) {
            if (i > 0) amp_ss << ",";
            amp_ss << data.amplitude[i];
        }
        amp_ss << "]::numeric[]";

        std::string freq_spec_str = data.frequency_spectrum.empty() ?
            "NULL" : ("'" + data.frequency_spectrum.dump() + "'::jsonb");

        std::ostringstream query;
        query << "INSERT INTO vibration_data "
              << "(sensor_id, porcelain_id, measurement_time, frequency_spectrum, amplitude, "
              << "rms_value, peak_value, dominant_frequency, temperature, humidity) VALUES ("
              << data.sensor_id << ", "
              << data.porcelain_id << ", '"
              << time_ss.str() << "', "
              << freq_spec_str << ", "
              << amp_ss.str() << ", "
              << data.rms_value << ", "
              << data.peak_value << ", "
              << data.dominant_frequency << ", "
              << data.temperature << ", "
              << data.humidity << ")";

        PGresult* res = PQexec(conn->get(), query.str().c_str());
        if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
            std::cerr << "Batch insert vibration data error: " << PQerrorMessage(conn->get()) << std::endl;
            ok = false;
            PQclear(res);
            break;
        }
        PQclear(res);
    }

    if (ok) {
        conn->commit_transaction();
    } else {
        conn->rollback_transaction();
    }
    pool_->release(conn);
    return ok;
}

bool DatabaseManager::batch_insert_crack_detections(
    const LaserMicroscopeData& laser_data,
    const std::vector<CrackInfo>& cracks,
    const std::vector<std::vector<Point3D>>& crack_points,
    int porcelain_id) {

    auto conn = pool_->acquire();
    if (!conn->begin_transaction()) {
        pool_->release(conn);
        return false;
    }

    int64_t laser_id = -1;
    {
        auto ts = std::chrono::system_clock::to_time_t(laser_data.measurement_time);
        std::tm tm_buf;
#ifdef _WIN32
        gmtime_s(&tm_buf, &ts);
#else
        gmtime_r(&tm_buf, &ts);
#endif
        std::ostringstream time_ss;
        time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

        std::ostringstream scan_area_ss;
        scan_area_ss << "ARRAY[" << laser_data.scan_area[0] << "," << laser_data.scan_area[1]
                     << "," << laser_data.scan_area[2] << "," << laser_data.scan_area[3] << "]::numeric[]";

        std::string processed_data_str = laser_data.processed_data.empty() ?
            "NULL" : ("'" + laser_data.processed_data.dump() + "'::jsonb");

        std::ostringstream query;
        query << "INSERT INTO laser_microscope_data "
              << "(sensor_id, porcelain_id, measurement_time, scan_area, resolution, "
              << "crack_detected, crack_count, processed_data) VALUES ("
              << laser_data.sensor_id << ", "
              << laser_data.porcelain_id << ", '"
              << time_ss.str() << "', "
              << scan_area_ss.str() << ", "
              << laser_data.resolution << ", "
              << (laser_data.crack_detected ? "TRUE" : "FALSE") << ", "
              << laser_data.crack_count << ", "
              << processed_data_str << ") RETURNING id";

        PGresult* res = PQexec(conn->get(), query.str().c_str());
        if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            laser_id = std::atoll(PQgetvalue(res, 0, 0));
        } else {
            std::cerr << "Batch: laser insert failed: " << PQerrorMessage(conn->get()) << std::endl;
            PQclear(res);
            conn->rollback_transaction();
            pool_->release(conn);
            return false;
        }
        PQclear(res);
    }

    for (size_t ci = 0; ci < cracks.size() && ci < crack_points.size(); ++ci) {
        const auto& crack = cracks[ci];
        int64_t crack_id = -1;

        {
            auto ts = std::chrono::system_clock::to_time_t(crack.detected_at);
            std::tm tm_buf;
#ifdef _WIN32
            gmtime_s(&tm_buf, &ts);
#else
            gmtime_r(&tm_buf, &ts);
#endif
            std::ostringstream time_ss;
            time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

            std::ostringstream query;
            query << "INSERT INTO cracks "
                  << "(porcelain_id, crack_code, detected_date, max_depth, max_width, total_length) VALUES ("
                  << porcelain_id << ", '"
                  << crack.crack_code << "', '"
                  << time_ss.str() << "', "
                  << crack.max_depth << ", "
                  << crack.max_width << ", "
                  << crack.total_length << ") RETURNING id";

            PGresult* res = PQexec(conn->get(), query.str().c_str());
            if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                crack_id = std::atoll(PQgetvalue(res, 0, 0));
            } else {
                std::cerr << "Batch: crack insert failed: " << PQerrorMessage(conn->get()) << std::endl;
                PQclear(res);
                conn->rollback_transaction();
                pool_->release(conn);
                return false;
            }
            PQclear(res);
        }

        for (const auto& point : crack_points[ci]) {
            std::ostringstream point_ss, normal_ss;
            point_ss << "ARRAY[" << point.x << "," << point.y << "," << point.z << "]::numeric[]";
            if (point.normal) {
                normal_ss << "ARRAY[" << (*point.normal)[0] << "," << (*point.normal)[1]
                          << "," << (*point.normal)[2] << "]::numeric[]";
            } else {
                normal_ss << "NULL";
            }

            std::ostringstream query;
            query << "INSERT INTO crack_points "
                  << "(crack_id, point_3d, normal_vector, depth, width, curvature) VALUES ("
                  << crack_id << ", "
                  << point_ss.str() << ", "
                  << normal_ss.str() << ", "
                  << point.depth << ", "
                  << point.width << ", "
                  << (point.curvature ? std::to_string(*point.curvature) : "NULL")
                  << ")";

            PGresult* res = PQexec(conn->get(), query.str().c_str());
            if (!res || PQresultStatus(res) != PGRES_COMMAND_OK) {
                std::cerr << "Batch: crack point insert failed" << std::endl;
                PQclear(res);
                conn->rollback_transaction();
                pool_->release(conn);
                return false;
            }
            PQclear(res);
        }
    }

    if (!conn->commit_transaction()) {
        std::cerr << "Batch: commit failed" << std::endl;
        conn->rollback_transaction();
        pool_->release(conn);
        return false;
    }

    pool_->release(conn);
    return true;
}

}
