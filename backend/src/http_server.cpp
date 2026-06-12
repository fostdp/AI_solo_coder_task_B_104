#include "http_server.h"
#include "database.h"
#include "crack_propagation.h"
#include "dem_simulation.h"
#include "stress_analysis.h"
#include "washburn_model.h"
#include "four_point_bending.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <regex>

namespace porcelain_monitor {

HttpServer::HttpServer(asio::io_context& ioc, uint16_t port, const std::string& doc_root)
    : ioc_(ioc),
      acceptor_(ioc, {tcp::v4(), port}),
      doc_root_(doc_root) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    register_api_routes();
    do_accept();
    std::cout << "HTTP server started on port "
              << acceptor_.local_endpoint().port() << std::endl;
}

void HttpServer::stop() {
    beast::error_code ec;
    acceptor_.close(ec);
}

void HttpServer::add_route(const std::string& pattern, http::verb method, HttpHandler handler) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    Route r;
    r.pattern = std::regex(pattern);
    r.method = method;
    r.handler = std::move(handler);
    routes_.push_back(std::move(r));
}

void HttpServer::register_api_routes() {
    std::lock_guard<std::mutex> lock(routes_mutex_);

    Route r1;
    r1.pattern = std::regex(R"(^/api/porcelains$)");
    r1.method = http::verb::get;
    r1.handler = [this](const http::request<http::string_body>&, const std::smatch&) {
        auto porcelains = DatabaseManager::instance().get_all_porcelains();
        json j = json::array();
        for (const auto& p : porcelains) {
            j.push_back({
                {"id", p.id},
                {"museum_id", p.museum_id},
                {"name", p.name},
                {"dynasty", p.dynasty == DynastyType::SONG ? "SONG" : "YUAN"},
                {"production_year", p.production_year},
                {"description", p.description},
                {"origin_location", p.origin_location},
                {"dimensions", p.dimensions}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r1));

    Route r2;
    r2.pattern = std::regex(R"(^/api/porcelains/(\d+)$)");
    r2.method = http::verb::get;
    r2.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int id = std::stoi(matches[1].str());
        auto p = DatabaseManager::instance().get_porcelain(id);
        json j = {
            {"id", p.id},
            {"museum_id", p.museum_id},
            {"name", p.name},
            {"dynasty", p.dynasty == DynastyType::SONG ? "SONG" : "YUAN"},
            {"production_year", p.production_year},
            {"description", p.description},
            {"origin_location", p.origin_location},
            {"dimensions", p.dimensions}
        };
        return json_response(j);
    };
    routes_.push_back(std::move(r2));

    Route r3;
    r3.pattern = std::regex(R"(^/api/porcelains/(\d+)/cracks$)");
    r3.method = http::verb::get;
    r3.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int id = std::stoi(matches[1].str());
        auto cracks = DatabaseManager::instance().get_cracks_by_porcelain(id);
        json j = json::array();
        for (const auto& c : cracks) {
            auto points = DatabaseManager::instance().get_crack_points(static_cast<int>(c.id));
            json points_json = json::array();
            for (const auto& pt : points) {
                points_json.push_back({
                    {"x", pt.x}, {"y", pt.y}, {"z", pt.z},
                    {"depth", pt.depth}, {"width", pt.width}
                });
            }
            j.push_back({
                {"id", c.id},
                {"crack_code", c.crack_code},
                {"max_depth", c.max_depth},
                {"max_width", c.max_width},
                {"total_length", c.total_length},
                {"status", c.status},
                {"points", points_json}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r3));

    Route r4;
    r4.pattern = std::regex(R"(^/api/cracks/(\d+)/points$)");
    r4.method = http::verb::get;
    r4.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int id = std::stoi(matches[1].str());
        auto points = DatabaseManager::instance().get_crack_points(id);
        json j = json::array();
        for (const auto& pt : points) {
            j.push_back({
                {"x", pt.x}, {"y", pt.y}, {"z", pt.z},
                {"depth", pt.depth}, {"width", pt.width}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r4));

    Route r5;
    r5.pattern = std::regex(R"(^/api/cracks/(\d+)/predict$)");
    r5.method = http::verb::post;
    r5.handler = [this](const http::request<http::string_body>& req, const std::smatch& matches) {
        int crack_id = std::stoi(matches[1].str());
        auto crack = DatabaseManager::instance().get_crack(crack_id);
        auto points = DatabaseManager::instance().get_crack_points(crack_id);
        crack.points = points;

        algorithms::CrackPropagationModel model;
        auto result = model.predict(crack, 720, 100);

        CrackPrediction prediction;
        prediction.crack_id = crack_id;
        prediction.model_type = "PARIS_LAW";
        prediction.parameters = model.result_to_json(result)["parameters"];
        prediction.time_horizon_hours = 720;
        prediction.predicted_depth = result.predicted_depth_720h;
        prediction.predicted_width = result.predicted_width_720h;
        prediction.predicted_length = result.predicted_length_720h;
        prediction.confidence = result.confidence;
        prediction.risk_level = result.risk_level;

        DatabaseManager::instance().insert_prediction(prediction);

        return json_response(model.result_to_json(result));
    };
    routes_.push_back(std::move(r5));

    Route r6;
    r6.pattern = std::regex(R"(^/api/cracks/(\d+)/simulate/(\d+)$)");
    r6.method = http::verb::post;
    r6.handler = [this](const http::request<http::string_body>& req, const std::smatch& matches) {
        int crack_id = std::stoi(matches[1].str());
        int material_id = std::stoi(matches[2].str());

        auto crack = DatabaseManager::instance().get_crack(crack_id);
        auto points = DatabaseManager::instance().get_crack_points(crack_id);
        crack.points = points;

        auto materials = DatabaseManager::instance().get_repair_materials();
        RepairMaterial material;
        for (const auto& m : materials) {
            if (m.id == material_id) {
                material = m;
                break;
            }
        }

        algorithms::DEMSimulation dem;
        dem.set_crack_geometry(crack);
        dem.set_material_properties(material);
        dem.generate_particles(1000);
        dem.generate_boundary();

        auto result = dem.run(1000);

        RepairSimulation sim;
        sim.crack_id = crack_id;
        sim.material_id = material_id;
        sim.method = "DEM";
        sim.parameters = dem.result_to_json(result)["parameters"];
        sim.particle_count = result.particle_count;
        sim.filling_rate = result.filling_rate;
        sim.bonding_strength = result.bonding_strength;
        sim.surface_smoothness = result.surface_smoothness;
        sim.durability_score = result.durability_score;
        sim.result = dem.result_to_json(result);

        DatabaseManager::instance().insert_simulation(sim);

        return json_response(dem.result_to_json(result));
    };
    routes_.push_back(std::move(r6));

    Route r7;
    r7.pattern = std::regex(R"(^/api/alerts$)");
    r7.method = http::verb::get;
    r7.handler = [this](const http::request<http::string_body>&, const std::smatch&) {
        auto alerts = DatabaseManager::instance().get_active_alerts();
        json j = json::array();
        for (const auto& a : alerts) {
            j.push_back({
                {"id", a.id},
                {"type", to_string(a.type)},
                {"porcelain_id", a.porcelain_id},
                {"crack_id", a.crack_id},
                {"sensor_id", a.sensor_id},
                {"threshold", a.threshold_value},
                {"actual", a.actual_value},
                {"unit", a.unit},
                {"message", a.message},
                {"status", "PENDING"}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r7));

    Route r8;
    r8.pattern = std::regex(R"(^/api/alerts/(\d+)/acknowledge$)");
    r8.method = http::verb::post;
    r8.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int64_t id = std::stoll(matches[1].str());
        bool success = DatabaseManager::instance().update_alert_status(id, AlertStatus::ACKNOWLEDGED);
        return json_response({{"success", success}});
    };
    routes_.push_back(std::move(r8));

    Route r9;
    r9.pattern = std::regex(R"(^/api/materials$)");
    r9.method = http::verb::get;
    r9.handler = [this](const http::request<http::string_body>&, const std::smatch&) {
        auto materials = DatabaseManager::instance().get_repair_materials();
        json j = json::array();
        for (const auto& m : materials) {
            j.push_back({
                {"id", m.id},
                {"type", to_string(m.type)},
                {"name", m.name},
                {"manufacturer", m.manufacturer},
                {"particle_size_nm", m.particle_size_nm},
                {"purity", m.purity},
                {"viscosity", m.viscosity},
                {"refractive_index", m.refractive_index},
                {"thermal_expansion_coeff", m.thermal_expansion_coeff},
                {"properties", m.properties}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r9));

    Route r10;
    r10.pattern = std::regex(R"(^/api/porcelains/(\d+)/laser-data$)");
    r10.method = http::verb::get;
    r10.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int id = std::stoi(matches[1].str());
        auto data = DatabaseManager::instance().get_laser_data(id, 24);
        json j = json::array();
        for (const auto& d : data) {
            j.push_back({
                {"id", d.id},
                {"sensor_id", d.sensor_id},
                {"crack_detected", d.crack_detected},
                {"crack_count", d.crack_count},
                {"resolution", d.resolution}
            });
        }
        return json_response(j);
    };
    routes_.push_back(std::move(r10));

    Route r11;
    r11.pattern = std::regex(R"(^/api/porcelains/(\d+)/stress-analysis$)");
    r11.method = http::verb::post;
    r11.handler = [this](const http::request<http::string_body>& req, const std::smatch& matches) {
        int porcelain_id = std::stoi(matches[1].str());
        auto cracks = DatabaseManager::instance().get_cracks_by_porcelain(porcelain_id);
        for (auto& c : cracks) {
            auto points = DatabaseManager::instance().get_crack_points(static_cast<int>(c.id));
            c.points = points;
        }

        algorithms::StressAnalysisFEM fem;
        auto result = fem.analyze(porcelain_id, cracks);

        int64_t analysis_id = DatabaseManager::instance().insert_stress_analysis(result);
        DatabaseManager::instance().insert_stress_grid_points(analysis_id, result.grid_points);

        return json_response(fem.result_to_json(result));
    };
    routes_.push_back(std::move(r11));

    Route r12;
    r12.pattern = std::regex(R"(^/api/porcelains/(\d+)/stress-analysis$)");
    r12.method = http::verb::get;
    r12.handler = [this](const http::request<http::string_body>&, const std::smatch& matches) {
        int porcelain_id = std::stoi(matches[1].str());
        auto result = DatabaseManager::instance().get_latest_stress_analysis(porcelain_id);
        json grid_points_json = json::array();
        for (const auto& gp : result.grid_points) {
            grid_points_json.push_back({
                {"x", gp.x}, {"y", gp.y}, {"z", gp.z},
                {"stress", gp.stress},
                {"crack_density", gp.crack_density},
                {"principal_direction", gp.principal_direction}
            });
        }
        json j = {
            {"grid_points", grid_points_json},
            {"max_von_mises", result.max_von_mises},
            {"avg_von_mises", result.avg_von_mises},
            {"high_stress_area_ratio", result.high_stress_area_ratio}
        };
        return json_response(j);
    };
    routes_.push_back(std::move(r12));

    Route r13;
    r13.pattern = std::regex(R"(^/api/cracks/(\d+)/penetration/(\d+)$)");
    r13.method = http::verb::post;
    r13.handler = [this](const http::request<http::string_body>& req, const std::smatch& matches) {
        int crack_id = std::stoi(matches[1].str());
        int material_id = std::stoi(matches[2].str());

        auto crack = DatabaseManager::instance().get_crack(crack_id);
        auto points = DatabaseManager::instance().get_crack_points(crack_id);
        crack.points = points;

        auto materials = DatabaseManager::instance().get_repair_materials();
        RepairMaterial material;
        for (const auto& m : materials) {
            if (m.id == material_id) {
                material = m;
                break;
            }
        }

        double target_depth_um = crack.max_depth;
        if (!req.body().empty()) {
            try {
                json body = json::parse(req.body());
                if (body.contains("target_depth_um")) {
                    target_depth_um = body["target_depth_um"].get<double>();
                }
            } catch (...) {
            }
        }

        algorithms::WashburnPenetrationModel model;
        auto result = model.predict(crack_id, material_id, crack, material, target_depth_um);

        DatabaseManager::instance().insert_penetration_prediction(result);

        json j = model.result_to_json(result);
        j["predicted_time_s"] = result.predicted_time_s;
        j["penetration_rate_um_s"] = result.penetration_rate_um_s;
        j["time_series"] = result.time_series;
        j["depth_series"] = result.depth_series;
        return json_response(j);
    };
    routes_.push_back(std::move(r13));

    Route r14;
    r14.pattern = std::regex(R"(^/api/cracks/(\d+)/bending-test/(\d+)$)");
    r14.method = http::verb::post;
    r14.handler = [this](const http::request<http::string_body>& req, const std::smatch& matches) {
        int crack_id = std::stoi(matches[1].str());
        int material_id = std::stoi(matches[2].str());

        auto crack = DatabaseManager::instance().get_crack(crack_id);
        auto points = DatabaseManager::instance().get_crack_points(crack_id);
        crack.points = points;

        auto materials = DatabaseManager::instance().get_repair_materials();
        RepairMaterial material;
        for (const auto& m : materials) {
            if (m.id == material_id) {
                material = m;
                break;
            }
        }

        int porcelain_id = crack.porcelain_id;
        if (!req.body().empty()) {
            try {
                json body = json::parse(req.body());
                if (body.contains("porcelain_id")) {
                    porcelain_id = body["porcelain_id"].get<int>();
                }
            } catch (...) {
            }
        }

        algorithms::FourPointBendingTest test;
        auto result_repaired = test.simulate(porcelain_id, crack_id, material_id, crack, material, true);
        auto result_unrepaired = test.simulate(porcelain_id, crack_id, material_id, crack, material, false);

        BendingTestResult final_result;
        final_result.porcelain_id = porcelain_id;
        final_result.crack_id = crack_id;
        final_result.material_id = material_id;
        final_result.original_strength_mpa = result_repaired.original_strength_mpa;
        final_result.unrepaired_strength_mpa = result_unrepaired.repaired_strength_mpa;
        final_result.repaired_strength_mpa = result_repaired.repaired_strength_mpa;
        final_result.strength_recovery_ratio = final_result.original_strength_mpa > 0
            ? final_result.repaired_strength_mpa / final_result.original_strength_mpa
            : 0.0;
        final_result.load_displacement_load = result_repaired.load_displacement_load;
        final_result.load_displacement_disp = result_repaired.load_displacement_disp;

        DatabaseManager::instance().insert_bending_test_result(final_result);

        json j = test.result_to_json(final_result);
        j["original_strength_mpa"] = final_result.original_strength_mpa;
        j["unrepaired_strength_mpa"] = final_result.unrepaired_strength_mpa;
        j["repaired_strength_mpa"] = final_result.repaired_strength_mpa;
        j["strength_recovery_ratio"] = final_result.strength_recovery_ratio;
        json ld_curve = json::array();
        for (size_t i = 0; i < final_result.load_displacement_load.size() && i < final_result.load_displacement_disp.size(); ++i) {
            ld_curve.push_back({
                {"displacement_mm", final_result.load_displacement_disp[i]},
                {"load_n", final_result.load_displacement_load[i]}
            });
        }
        j["load_displacement_curve"] = ld_curve;
        return json_response(j);
    };
    routes_.push_back(std::move(r14));
}

void HttpServer::do_accept() {
    acceptor_.async_accept(
        asio::make_strand(ioc_),
        beast::bind_front_handler(
            &HttpServer::handle_accept,
            shared_from_this()));
}

void HttpServer::handle_accept(beast::error_code ec, tcp::socket socket) {
    if (!ec) {
        std::make_shared<HttpServer::Connection>(std::move(socket), *this)->start();
    }
    do_accept();
}

void HttpServer::handle_request(tcp::socket socket, http::request<http::string_body> req) {
    auto response = route_request(req);
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(response));
    http::async_write(socket, *sp,
        [sp](beast::error_code, std::size_t) {});
}

http::response<http::string_body> HttpServer::route_request(const http::request<http::string_body>& req) {
    std::string path = std::string(req.target());

    if (path == "/" || path == "/index.html") {
        return serve_file("/index.html");
    }

    if (path.rfind("/api/", 0) == 0) {
        std::lock_guard<std::mutex> lock(routes_mutex_);
        for (const auto& route : routes_) {
            std::smatch matches;
            if (std::regex_match(path, matches, route.pattern) &&
                req.method() == route.method) {
                return route.handler(req, matches);
            }
        }
        return error_response(404, "API endpoint not found");
    }

    return serve_file(path);
}

http::response<http::string_body> HttpServer::serve_file(const std::string& path) {
    std::string full_path = doc_root_ + path;
    std::ifstream file(full_path, std::ios::binary);

    if (!file.is_open()) {
        return not_found(path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    http::response<http::string_body> res{http::status::ok, 11};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type,
        path.ends_with(".html") ? "text/html" :
        path.ends_with(".css") ? "text/css" :
        path.ends_with(".js") ? "application/javascript" :
        "application/octet-stream");
    res.body() = std::move(content);
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::not_found(const std::string& target) {
    http::response<http::string_body> res{http::status::not_found, 11};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/plain");
    res.body() = "The resource '" + target + "' was not found.";
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::json_response(const nlohmann::json& j, unsigned status) {
    http::response<http::string_body> res{
        static_cast<http::status>(status), 11};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::access_control_allow_origin, "*");
    res.body() = j.dump();
    res.prepare_payload();
    return res;
}

http::response<http::string_body> HttpServer::error_response(unsigned status, const std::string& message) {
    return json_response({{"error", message}}, status);
}

}
