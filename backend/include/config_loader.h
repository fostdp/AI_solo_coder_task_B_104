#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
#include "config.h"

namespace porcelain_monitor {
namespace config {

class ConfigLoader {
public:
    static Config load_from_file(const std::string& path) {
        Config cfg;
        cfg.apply_env();

        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            std::cerr << "[ConfigLoader] File not found: " << path
                      << ", using defaults + env vars" << std::endl;
            return cfg;
        }

        try {
            nlohmann::json j;
            ifs >> j;

            if (j.contains("database")) {
                const auto& d = j["database"];
                if (d.contains("host")) d.at("host").get_to(cfg.database.host);
                if (d.contains("port")) d.at("port").get_to(cfg.database.port);
                if (d.contains("name")) d.at("name").get_to(cfg.database.name);
                if (d.contains("user")) d.at("user").get_to(cfg.database.user);
                if (d.contains("password")) d.at("password").get_to(cfg.database.password);
                if (d.contains("pool_size")) d.at("pool_size").get_to(cfg.database.pool_size);
            }

            if (j.contains("server")) {
                const auto& s = j["server"];
                if (s.contains("profinet_port")) s.at("profinet_port").get_to(cfg.server.profinet_port);
                if (s.contains("http_port")) s.at("http_port").get_to(cfg.server.http_port);
                if (s.contains("websocket_port")) s.at("websocket_port").get_to(cfg.server.websocket_port);
                if (s.contains("bind_address")) s.at("bind_address").get_to(cfg.server.bind_address);
                if (s.contains("thread_pool_size")) s.at("thread_pool_size").get_to(cfg.server.thread_pool_size);
            }

            if (j.contains("alerts")) {
                const auto& a = j["alerts"];
                if (a.contains("depth_threshold")) a.at("depth_threshold").get_to(cfg.alerts.depth_threshold);
                if (a.contains("width_threshold")) a.at("width_threshold").get_to(cfg.alerts.width_threshold);
                if (a.contains("sms_enabled")) a.at("sms_enabled").get_to(cfg.alerts.sms_enabled);
                if (a.contains("websocket_enabled")) a.at("websocket_enabled").get_to(cfg.alerts.websocket_enabled);
                if (a.contains("sms_gateway_url")) a.at("sms_gateway_url").get_to(cfg.alerts.sms_gateway_url);
                if (a.contains("alert_phone_number")) a.at("alert_phone_number").get_to(cfg.alerts.alert_phone_number);
            }

            if (j.contains("algorithms")) {
                const auto& alg = j["algorithms"];
                if (alg.contains("paris_law")) {
                    const auto& p = alg["paris_law"];
                    if (p.contains("default_C")) p.at("default_C").get_to(cfg.algorithms.paris_law.default_C);
                    if (p.contains("default_m")) p.at("default_m").get_to(cfg.algorithms.paris_law.default_m);
                    if (p.contains("stress_ratio")) p.at("stress_ratio").get_to(cfg.algorithms.paris_law.stress_ratio);
                    if (p.contains("prediction_horizon_hours"))
                        p.at("prediction_horizon_hours").get_to(cfg.algorithms.paris_law.prediction_horizon_hours);
                }
                if (alg.contains("dem")) {
                    const auto& d = alg["dem"];
                    if (d.contains("particle_radius_nm")) d.at("particle_radius_nm").get_to(cfg.algorithms.dem.particle_radius_nm);
                    if (d.contains("youngs_modulus")) d.at("youngs_modulus").get_to(cfg.algorithms.dem.youngs_modulus);
                    if (d.contains("poissons_ratio")) d.at("poissons_ratio").get_to(cfg.algorithms.dem.poissons_ratio);
                    if (d.contains("density")) d.at("density").get_to(cfg.algorithms.dem.density);
                    if (d.contains("max_particles")) d.at("max_particles").get_to(cfg.algorithms.dem.max_particles);
                    if (d.contains("simulation_steps")) d.at("simulation_steps").get_to(cfg.algorithms.dem.simulation_steps);
                    if (d.contains("time_step")) d.at("time_step").get_to(cfg.algorithms.dem.time_step);
                }
            }

            std::cout << "[ConfigLoader] Loaded: " << path << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ConfigLoader] Parse error: " << e.what()
                      << ", using defaults + env vars" << std::endl;
        }

        return cfg;
    }

    static Config load_default() {
        return load_from_file("config.json");
    }
};

}
}
