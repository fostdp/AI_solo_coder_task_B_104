#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "common.h"
#include "alert_manager.h"
#include "websocket_server.h"
#include "database.h"
#include "lockfree_queue.h"
#include "module_base.h"

namespace porcelain_monitor {
namespace modules {

class AlertWsModule : public SinkModuleBase {
public:
    using LaserInput = concurrency::SharedQueue<ParsedLaserMessage>;
    using VibrationInput = concurrency::SharedQueue<ParsedVibrationMessage>;
    using PredictionInput = concurrency::SharedQueue<FatiguePredictionMessage>;
    using DemInput = concurrency::SharedQueue<DemSimulationMessage>;

    AlertWsModule(boost::asio::io_context& ioc, uint16_t ws_port)
        : ws_server_(std::make_shared<WebSocketServer>(ioc, ws_port)),
          alert_manager_(std::make_unique<AlertManager>()),
          laser_in_(concurrency::make_queue<ParsedLaserMessage>()),
          vibration_in_(concurrency::make_queue<ParsedVibrationMessage>()),
          prediction_in_(concurrency::make_queue<FatiguePredictionMessage>()),
          dem_in_(concurrency::make_queue<DemSimulationMessage>()) {}

    void init(double depth_threshold, double width_threshold,
              bool sms_enabled, bool websocket_enabled,
              const std::string& sms_gateway, const std::string& phone) {
        alert_manager_->init(depth_threshold, width_threshold, sms_enabled,
                             websocket_enabled, sms_gateway, phone);
        alert_manager_->set_websocket_server(ws_server_.get());
    }

    void set_laser_input(LaserInput q) { laser_in_ = std::move(q); }
    void set_vibration_input(VibrationInput q) { vibration_in_ = std::move(q); }
    void set_prediction_input(PredictionInput q) { prediction_in_ = std::move(q); }
    void set_dem_input(DemInput q) { dem_in_ = std::move(q); }

    std::shared_ptr<WebSocketServer> ws_server() { return ws_server_; }

    void start() override {
        ws_server_->start();
        alert_manager_->start();
        running_.store(true);

        worker_ = std::thread([this]() {
            while (running_.load()) {
                ParsedLaserMessage laser_msg;
                if (laser_in_ && laser_in_->try_pop(laser_msg)) {
                    handle_laser(laser_msg);
                    continue;
                }

                ParsedVibrationMessage vib_msg;
                if (vibration_in_ && vibration_in_->try_pop(vib_msg)) {
                    handle_vibration(vib_msg);
                    continue;
                }

                FatiguePredictionMessage pred_msg;
                if (prediction_in_ && prediction_in_->try_pop(pred_msg)) {
                    handle_prediction(pred_msg);
                    continue;
                }

                DemSimulationMessage dem_msg;
                if (dem_in_ && dem_in_->try_pop(dem_msg)) {
                    handle_dem(dem_msg);
                    continue;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    void stop() override {
        running_.store(false);
        if (worker_.joinable()) worker_.join();
        alert_manager_->stop();
        ws_server_->stop();
    }

    uint64_t processed_count() const { return processed_.load(); }

private:
    void handle_laser(const ParsedLaserMessage& msg) {
        try {
            DatabaseManager::instance().log_profinet_packet(
                msg.packet.source_ip, msg.packet.destination_ip,
                msg.packet.frame_id, msg.packet.payload);

            auto& cracks = msg.data.cracks;
            auto laser_with_id = msg.data;
            int64_t data_id = DatabaseManager::instance().insert_laser_data(laser_with_id);

            if (laser_with_id.crack_detected) {
                std::vector<std::vector<Point3D>> all_points;
                for (auto& crack : cracks) {
                    crack.crack_code = "CRK-" + std::to_string(laser_with_id.porcelain_id) +
                        "-" + std::to_string(std::chrono::system_clock::to_time_t(
                            std::chrono::system_clock::now()));
                    crack.detected_at = laser_with_id.measurement_time;

                    int64_t crack_id = DatabaseManager::instance().insert_crack(
                        crack, laser_with_id.porcelain_id);
                    if (crack_id > 0) {
                        DatabaseManager::instance().insert_crack_points(crack_id, crack.points);
                    }
                    all_points.push_back(crack.points);
                }
            }

            alert_manager_->check_laser_data(laser_with_id);

            json j;
            j["type"] = "laser_data";
            j["data"] = {
                {"sensor_id", laser_with_id.sensor_id},
                {"porcelain_id", laser_with_id.porcelain_id},
                {"crack_detected", laser_with_id.crack_detected},
                {"crack_count", laser_with_id.crack_count}
            };
            ws_server_->broadcast_json(j);

            processed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            std::cerr << "[AlertWs] laser error: " << e.what() << std::endl;
        }
    }

    void handle_vibration(const ParsedVibrationMessage& msg) {
        try {
            DatabaseManager::instance().log_profinet_packet(
                msg.packet.source_ip, msg.packet.destination_ip,
                msg.packet.frame_id, msg.packet.payload);

            DatabaseManager::instance().insert_vibration_data(msg.data);
            alert_manager_->check_vibration_data(msg.data);

            json j;
            j["type"] = "vibration_data";
            j["data"] = {
                {"sensor_id", msg.data.sensor_id},
                {"porcelain_id", msg.data.porcelain_id},
                {"rms_value", msg.data.rms_value},
                {"peak_value", msg.data.peak_value},
                {"temperature", msg.data.temperature},
                {"humidity", msg.data.humidity}
            };
            ws_server_->broadcast_json(j);

            processed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            std::cerr << "[AlertWs] vibration error: " << e.what() << std::endl;
        }
    }

    void handle_prediction(const FatiguePredictionMessage& msg) {
        try {
            alert_manager_->check_prediction(msg.prediction);

            json j;
            j["type"] = "fatigue_prediction";
            j["data"] = {
                {"crack_id", msg.prediction.crack_id},
                {"porcelain_id", msg.porcelain_id},
                {"risk_level", msg.prediction.risk_level},
                {"confidence", msg.prediction.confidence},
                {"predicted_length_720h", msg.prediction.predicted_length}
            };
            ws_server_->broadcast_json(j);

            processed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            std::cerr << "[AlertWs] prediction error: " << e.what() << std::endl;
        }
    }

    void handle_dem(const DemSimulationMessage& msg) {
        try {
            json j;
            j["type"] = "dem_simulation";
            j["data"] = {
                {"crack_id", msg.result.crack_id},
                {"porcelain_id", msg.porcelain_id},
                {"filling_rate", msg.result.filling_rate},
                {"durability_score", msg.result.durability_score},
                {"particle_count", msg.result.particle_count}
            };
            ws_server_->broadcast_json(j);

            processed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            std::cerr << "[AlertWs] dem error: " << e.what() << std::endl;
        }
    }

    std::shared_ptr<WebSocketServer> ws_server_;
    std::unique_ptr<AlertManager> alert_manager_;

    LaserInput laser_in_;
    VibrationInput vibration_in_;
    PredictionInput prediction_in_;
    DemInput dem_in_;

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> processed_{0};
    std::thread worker_;
};

}
}
