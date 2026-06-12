#include "database.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace porcelain_monitor {

int64_t DatabaseManager::insert_stress_analysis(const StressAnalysisResult& result) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(result.created_at);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::string parameters_str = result.parameters.empty() ?
        "NULL" : ("'" + result.parameters.dump() + "'::jsonb");

    std::string result_str = result.result.empty() ?
        "NULL" : ("'" + result.result.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO stress_analysis_results "
          << "(porcelain_id, method, parameters, max_von_mises, avg_von_mises, "
          << "high_stress_area_ratio, result, created_at) VALUES ("
          << result.porcelain_id << ", '"
          << result.method << "', "
          << parameters_str << ", "
          << result.max_von_mises << ", "
          << result.avg_von_mises << ", "
          << result.high_stress_area_ratio << ", "
          << result_str << ", '"
          << time_ss.str() << "') RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting stress analysis: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

bool DatabaseManager::insert_stress_grid_points(int64_t analysis_id,
                                                 const std::vector<StressGridPoint>& points) {
    if (points.empty()) return true;

    auto conn = pool_->acquire();
    if (!conn->begin_transaction()) {
        std::cerr << "Failed to begin transaction for stress grid points" << std::endl;
        pool_->release(conn);
        return false;
    }

    const char* stmt_name = "stress_grid_point_insert";
    std::string prepare_sql =
        "PREPARE stress_grid_point_insert (bigint, numeric, numeric, numeric, "
        "numeric, numeric, numeric, numeric, numeric, numeric, "
        "numeric, numeric, numeric) AS "
        "INSERT INTO stress_grid_points "
        "(analysis_id, x, y, z, sigma_xx, sigma_yy, sigma_zz, "
        "tau_xy, tau_yz, tau_zx, von_mises, crack_density, principal_direction) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13)";

    PGresult* prep_res = PQexec(conn->get(), prepare_sql.c_str());
    if (!prep_res || PQresultStatus(prep_res) != PGRES_COMMAND_OK) {
        std::cerr << "Failed to prepare stress grid point statement: "
                  << PQerrorMessage(conn->get()) << std::endl;
        PQclear(prep_res);
        conn->rollback_transaction();
        pool_->release(conn);
        return false;
    }
    PQclear(prep_res);

    bool ok = true;
    const int n_params = 13;
    for (const auto& pt : points) {
        std::string analysis_id_str = std::to_string(analysis_id);
        std::string x_str = std::to_string(pt.x);
        std::string y_str = std::to_string(pt.y);
        std::string z_str = std::to_string(pt.z);
        std::string sxx_str = std::to_string(pt.stress.sigma_xx);
        std::string syy_str = std::to_string(pt.stress.sigma_yy);
        std::string szz_str = std::to_string(pt.stress.sigma_zz);
        std::string txy_str = std::to_string(pt.stress.tau_xy);
        std::string tyz_str = std::to_string(pt.stress.tau_yz);
        std::string tzx_str = std::to_string(pt.stress.tau_zx);
        std::string vm_str = std::to_string(pt.stress.von_mises);
        std::string cd_str = std::to_string(pt.crack_density);
        std::string pd_str = std::to_string(pt.principal_direction);

        const char* param_values[n_params] = {
            analysis_id_str.c_str(), x_str.c_str(), y_str.c_str(), z_str.c_str(),
            sxx_str.c_str(), syy_str.c_str(), szz_str.c_str(),
            txy_str.c_str(), tyz_str.c_str(), tzx_str.c_str(),
            vm_str.c_str(), cd_str.c_str(), pd_str.c_str()
        };

        PGresult* exec_res = PQexecPrepared(conn->get(), stmt_name, n_params,
                                             param_values, nullptr, nullptr, 0);
        if (!exec_res || PQresultStatus(exec_res) != PGRES_COMMAND_OK) {
            std::cerr << "Error inserting stress grid point: "
                      << PQerrorMessage(conn->get()) << std::endl;
            ok = false;
            PQclear(exec_res);
            break;
        }
        PQclear(exec_res);
    }

    PGresult* dealloc_res = PQexec(conn->get(), "DEALLOCATE stress_grid_point_insert");
    PQclear(dealloc_res);

    if (ok) {
        if (!conn->commit_transaction()) {
            std::cerr << "Failed to commit stress grid points transaction" << std::endl;
            conn->rollback_transaction();
            ok = false;
        }
    } else {
        conn->rollback_transaction();
    }

    pool_->release(conn);
    return ok;
}

StressAnalysisResult DatabaseManager::get_latest_stress_analysis(int porcelain_id) {
    auto conn = pool_->acquire();
    StressAnalysisResult result{};

    std::ostringstream query;
    query << "SELECT id, porcelain_id, method, parameters, max_von_mises, avg_von_mises, "
          << "high_stress_area_ratio, result, created_at "
          << "FROM stress_analysis_results WHERE porcelain_id = " << porcelain_id
          << " ORDER BY created_at DESC LIMIT 1";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        result.id = std::atoll(PQgetvalue(res, 0, 0));
        result.porcelain_id = std::atoi(PQgetvalue(res, 0, 1));
        result.method = PQgetvalue(res, 0, 2);
        if (PQgetvalue(res, 0, 3) && PQgetlength(res, 0, 3) > 0) {
            result.parameters = json::parse(PQgetvalue(res, 0, 3));
        }
        result.max_von_mises = std::atof(PQgetvalue(res, 0, 4));
        result.avg_von_mises = std::atof(PQgetvalue(res, 0, 5));
        result.high_stress_area_ratio = std::atof(PQgetvalue(res, 0, 6));
        if (PQgetvalue(res, 0, 7) && PQgetlength(res, 0, 7) > 0) {
            result.result = json::parse(PQgetvalue(res, 0, 7));
        }
    }
    PQclear(res);

    if (result.id > 0) {
        std::ostringstream gp_query;
        gp_query << "SELECT x, y, z, sigma_xx, sigma_yy, sigma_zz, "
                 << "tau_xy, tau_yz, tau_zx, von_mises, crack_density, principal_direction "
                 << "FROM stress_grid_points WHERE analysis_id = " << result.id
                 << " ORDER BY id";

        PGresult* gp_res = PQexec(conn->get(), gp_query.str().c_str());
        if (gp_res && PQresultStatus(gp_res) == PGRES_TUPLES_OK) {
            for (int i = 0; i < PQntuples(gp_res); ++i) {
                StressGridPoint pt;
                pt.x = std::atof(PQgetvalue(gp_res, i, 0));
                pt.y = std::atof(PQgetvalue(gp_res, i, 1));
                pt.z = std::atof(PQgetvalue(gp_res, i, 2));
                pt.stress.sigma_xx = std::atof(PQgetvalue(gp_res, i, 3));
                pt.stress.sigma_yy = std::atof(PQgetvalue(gp_res, i, 4));
                pt.stress.sigma_zz = std::atof(PQgetvalue(gp_res, i, 5));
                pt.stress.tau_xy = std::atof(PQgetvalue(gp_res, i, 6));
                pt.stress.tau_yz = std::atof(PQgetvalue(gp_res, i, 7));
                pt.stress.tau_zx = std::atof(PQgetvalue(gp_res, i, 8));
                pt.stress.von_mises = std::atof(PQgetvalue(gp_res, i, 9));
                pt.crack_density = std::atof(PQgetvalue(gp_res, i, 10));
                pt.principal_direction = std::atof(PQgetvalue(gp_res, i, 11));
                result.grid_points.push_back(pt);
            }
        }
        PQclear(gp_res);
    }

    pool_->release(conn);
    return result;
}

std::vector<StressAnalysisResult> DatabaseManager::get_stress_analysis_history(int porcelain_id, int limit) {
    auto conn = pool_->acquire();
    std::vector<StressAnalysisResult> result;

    std::ostringstream query;
    query << "SELECT id, porcelain_id, method, parameters, max_von_mises, avg_von_mises, "
          << "high_stress_area_ratio, result, created_at "
          << "FROM stress_analysis_results WHERE porcelain_id = " << porcelain_id
          << " ORDER BY created_at DESC LIMIT " << limit;

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            StressAnalysisResult sar;
            sar.id = std::atoll(PQgetvalue(res, i, 0));
            sar.porcelain_id = std::atoi(PQgetvalue(res, i, 1));
            sar.method = PQgetvalue(res, i, 2);
            if (PQgetvalue(res, i, 3) && PQgetlength(res, i, 3) > 0) {
                sar.parameters = json::parse(PQgetvalue(res, i, 3));
            }
            sar.max_von_mises = std::atof(PQgetvalue(res, i, 4));
            sar.avg_von_mises = std::atof(PQgetvalue(res, i, 5));
            sar.high_stress_area_ratio = std::atof(PQgetvalue(res, i, 6));
            if (PQgetvalue(res, i, 7) && PQgetlength(res, i, 7) > 0) {
                sar.result = json::parse(PQgetvalue(res, i, 7));
            }
            result.push_back(sar);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

int64_t DatabaseManager::insert_penetration_prediction(const PenetrationPrediction& prediction) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(prediction.created_at);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream time_series_ss;
    time_series_ss << "ARRAY[";
    for (size_t i = 0; i < prediction.time_series.size(); ++i) {
        if (i > 0) time_series_ss << ",";
        time_series_ss << prediction.time_series[i];
    }
    time_series_ss << "]::numeric[]";

    std::ostringstream depth_series_ss;
    depth_series_ss << "ARRAY[";
    for (size_t i = 0; i < prediction.depth_series.size(); ++i) {
        if (i > 0) depth_series_ss << ",";
        depth_series_ss << prediction.depth_series[i];
    }
    depth_series_ss << "]::numeric[]";

    std::string parameters_str = prediction.parameters.empty() ?
        "NULL" : ("'" + prediction.parameters.dump() + "'::jsonb");

    std::string result_str = prediction.result.empty() ?
        "NULL" : ("'" + prediction.result.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO penetration_predictions "
          << "(crack_id, material_id, method, parameters, target_depth_um, viscosity_pa_s, "
          << "surface_tension_n_m, contact_angle_deg, crack_width_um, predicted_time_s, "
          << "penetration_rate_um_s, time_series, depth_series, result, created_at) VALUES ("
          << prediction.crack_id << ", "
          << prediction.material_id << ", '"
          << prediction.method << "', "
          << parameters_str << ", "
          << prediction.target_depth_um << ", "
          << prediction.viscosity_pa_s << ", "
          << prediction.surface_tension_n_m << ", "
          << prediction.contact_angle_deg << ", "
          << prediction.crack_width_um << ", "
          << prediction.predicted_time_s << ", "
          << prediction.penetration_rate_um_s << ", "
          << time_series_ss.str() << ", "
          << depth_series_ss.str() << ", "
          << result_str << ", '"
          << time_ss.str() << "') RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting penetration prediction: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

std::vector<PenetrationPrediction> DatabaseManager::get_penetration_predictions(int crack_id) {
    auto conn = pool_->acquire();
    std::vector<PenetrationPrediction> result;

    std::ostringstream query;
    query << "SELECT id, crack_id, material_id, method, parameters, target_depth_um, "
          << "viscosity_pa_s, surface_tension_n_m, contact_angle_deg, crack_width_um, "
          << "predicted_time_s, penetration_rate_um_s, result, created_at "
          << "FROM penetration_predictions WHERE crack_id = " << crack_id
          << " ORDER BY created_at DESC";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            PenetrationPrediction pp;
            pp.id = std::atoll(PQgetvalue(res, i, 0));
            pp.crack_id = std::atoi(PQgetvalue(res, i, 1));
            pp.material_id = std::atoi(PQgetvalue(res, i, 2));
            pp.method = PQgetvalue(res, i, 3);
            if (PQgetvalue(res, i, 4) && PQgetlength(res, i, 4) > 0) {
                pp.parameters = json::parse(PQgetvalue(res, i, 4));
            }
            pp.target_depth_um = std::atof(PQgetvalue(res, i, 5));
            pp.viscosity_pa_s = std::atof(PQgetvalue(res, i, 6));
            pp.surface_tension_n_m = std::atof(PQgetvalue(res, i, 7));
            pp.contact_angle_deg = std::atof(PQgetvalue(res, i, 8));
            pp.crack_width_um = std::atof(PQgetvalue(res, i, 9));
            pp.predicted_time_s = std::atof(PQgetvalue(res, i, 10));
            pp.penetration_rate_um_s = std::atof(PQgetvalue(res, i, 11));
            if (PQgetvalue(res, i, 12) && PQgetlength(res, i, 12) > 0) {
                pp.result = json::parse(PQgetvalue(res, i, 12));
            }
            result.push_back(pp);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

int64_t DatabaseManager::insert_bending_test_result(const BendingTestResult& result) {
    auto conn = pool_->acquire();

    auto ts = std::chrono::system_clock::to_time_t(result.created_at);
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &ts);
#else
    gmtime_r(&ts, &tm_buf);
#endif
    std::ostringstream time_ss;
    time_ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    std::ostringstream load_ss;
    load_ss << "ARRAY[";
    for (size_t i = 0; i < result.load_displacement_load.size(); ++i) {
        if (i > 0) load_ss << ",";
        load_ss << result.load_displacement_load[i];
    }
    load_ss << "]::numeric[]";

    std::ostringstream disp_ss;
    disp_ss << "ARRAY[";
    for (size_t i = 0; i < result.load_displacement_disp.size(); ++i) {
        if (i > 0) disp_ss << ",";
        disp_ss << result.load_displacement_disp[i];
    }
    disp_ss << "]::numeric[]";

    std::string parameters_str = result.parameters.empty() ?
        "NULL" : ("'" + result.parameters.dump() + "'::jsonb");

    std::string stress_dist_str = result.stress_distribution.empty() ?
        "NULL" : ("'" + result.stress_distribution.dump() + "'::jsonb");

    std::string result_str = result.result.empty() ?
        "NULL" : ("'" + result.result.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO bending_test_results "
          << "(porcelain_id, crack_id, material_id, method, parameters, "
          << "original_strength_mpa, unrepaired_strength_mpa, repaired_strength_mpa, "
          << "strength_recovery_ratio, youngs_modulus_gpa, fracture_toughness_mpa_m05, "
          << "load_series, displacement_series, stress_distribution, result, created_at) VALUES ("
          << result.porcelain_id << ", "
          << result.crack_id << ", "
          << result.material_id << ", '"
          << result.method << "', "
          << parameters_str << ", "
          << result.original_strength_mpa << ", "
          << result.unrepaired_strength_mpa << ", "
          << result.repaired_strength_mpa << ", "
          << result.strength_recovery_ratio << ", "
          << result.youngs_modulus_gpa << ", "
          << result.fracture_toughness_mpa_m05 << ", "
          << load_ss.str() << ", "
          << disp_ss.str() << ", "
          << stress_dist_str << ", "
          << result_str << ", '"
          << time_ss.str() << "') RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting bending test result: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

std::vector<BendingTestResult> DatabaseManager::get_bending_test_results(int porcelain_id, int crack_id) {
    auto conn = pool_->acquire();
    std::vector<BendingTestResult> result;

    std::ostringstream query;
    query << "SELECT id, porcelain_id, crack_id, material_id, method, parameters, "
          << "original_strength_mpa, unrepaired_strength_mpa, repaired_strength_mpa, "
          << "strength_recovery_ratio, youngs_modulus_gpa, fracture_toughness_mpa_m05, "
          << "result, created_at "
          << "FROM bending_test_results WHERE porcelain_id = " << porcelain_id;
    if (crack_id > 0) {
        query << " AND crack_id = " << crack_id;
    }
    query << " ORDER BY created_at DESC";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK) {
        for (int i = 0; i < PQntuples(res); ++i) {
            BendingTestResult btr;
            btr.id = std::atoll(PQgetvalue(res, i, 0));
            btr.porcelain_id = std::atoi(PQgetvalue(res, i, 1));
            btr.crack_id = std::atoi(PQgetvalue(res, i, 2));
            btr.material_id = std::atoi(PQgetvalue(res, i, 3));
            btr.method = PQgetvalue(res, i, 4);
            if (PQgetvalue(res, i, 5) && PQgetlength(res, i, 5) > 0) {
                btr.parameters = json::parse(PQgetvalue(res, i, 5));
            }
            btr.original_strength_mpa = std::atof(PQgetvalue(res, i, 6));
            btr.unrepaired_strength_mpa = std::atof(PQgetvalue(res, i, 7));
            btr.repaired_strength_mpa = std::atof(PQgetvalue(res, i, 8));
            btr.strength_recovery_ratio = std::atof(PQgetvalue(res, i, 9));
            btr.youngs_modulus_gpa = std::atof(PQgetvalue(res, i, 10));
            btr.fracture_toughness_mpa_m05 = std::atof(PQgetvalue(res, i, 11));
            if (PQgetvalue(res, i, 12) && PQgetlength(res, i, 12) > 0) {
                btr.result = json::parse(PQgetvalue(res, i, 12));
            }
            result.push_back(btr);
        }
    }
    PQclear(res);
    pool_->release(conn);
    return result;
}

int64_t DatabaseManager::insert_virtual_repair_record(int porcelain_id, int crack_id,
                                                       const json& repaired_region,
                                                       double repair_radius,
                                                       double closure_ratio,
                                                       const json& animation_data) {
    auto conn = pool_->acquire();

    std::string region_str = repaired_region.empty() ?
        "NULL" : ("'" + repaired_region.dump() + "'::jsonb");

    std::string anim_str = animation_data.empty() ?
        "NULL" : ("'" + animation_data.dump() + "'::jsonb");

    std::ostringstream query;
    query << "INSERT INTO virtual_repair_records "
          << "(porcelain_id, crack_id, repaired_region, repair_radius, "
          << "closure_ratio, animation_data) VALUES ("
          << porcelain_id << ", "
          << crack_id << ", "
          << region_str << ", "
          << repair_radius << ", "
          << closure_ratio << ", "
          << anim_str << ") RETURNING id";

    PGresult* res = PQexec(conn->get(), query.str().c_str());
    int64_t id = -1;
    if (res && PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        id = std::atoll(PQgetvalue(res, 0, 0));
    } else {
        std::cerr << "Error inserting virtual repair record: " << PQerrorMessage(conn->get()) << std::endl;
    }
    PQclear(res);
    pool_->release(conn);
    return id;
}

}
