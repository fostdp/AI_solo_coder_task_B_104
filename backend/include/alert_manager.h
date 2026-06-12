#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include "common.h"

namespace porcelain_monitor {

class WebSocketServer;

class AlertManager {
public:
    using AlertCallback = std::function<void(const Alert&)>;

    AlertManager();
    ~AlertManager();

    void init(double depth_threshold, double width_threshold,
              bool sms_enabled, bool websocket_enabled,
              const std::string& sms_gateway, const std::string& phone);

    void set_websocket_server(WebSocketServer* ws_server) { ws_server_ = ws_server; }
    void add_callback(AlertCallback cb) { callbacks_.push_back(std::move(cb)); }

    void check_laser_data(const LaserMicroscopeData& data);
    void check_vibration_data(const VibrationData& data);
    void check_prediction(const CrackPrediction& prediction);

    std::vector<Alert> check_crack_thresholds(const CrackInfo& crack);

    void process_alerts();
    void start();
    void stop();

private:
    void send_sms(const Alert& alert);
    void send_websocket(const Alert& alert);
    void enqueue_alert(const Alert& alert);

    double depth_threshold_;
    double width_threshold_;
    bool sms_enabled_;
    bool websocket_enabled_;
    std::string sms_gateway_url_;
    std::string alert_phone_number_;

    WebSocketServer* ws_server_ = nullptr;
    std::vector<AlertCallback> callbacks_;

    std::queue<Alert> alert_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

}
