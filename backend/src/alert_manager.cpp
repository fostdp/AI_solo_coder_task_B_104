#include "alert_manager.h"
#include "websocket_server.h"
#include "database.h"
#include <iostream>
#include <sstream>

namespace porcelain_monitor {

AlertManager::AlertManager()
    : depth_threshold_(200.0),
      width_threshold_(50.0),
      sms_enabled_(true),
      websocket_enabled_(true) {}

AlertManager::~AlertManager() {
    stop();
}

void AlertManager::init(double depth_threshold, double width_threshold,
                        bool sms_enabled, bool websocket_enabled,
                        const std::string& sms_gateway, const std::string& phone) {
    depth_threshold_ = depth_threshold;
    width_threshold_ = width_threshold;
    sms_enabled_ = sms_enabled;
    websocket_enabled_ = websocket_enabled;
    sms_gateway_url_ = sms_gateway;
    alert_phone_number_ = phone;
}

void AlertManager::start() {
    running_ = true;
    worker_thread_ = std::thread(&AlertManager::process_alerts, this);
    std::cout << "Alert manager started" << std::endl;
}

void AlertManager::stop() {
    running_ = false;
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void AlertManager::enqueue_alert(const Alert& alert) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        alert_queue_.push(alert);
    }
    cv_.notify_one();
}

std::vector<Alert> AlertManager::check_crack_thresholds(const CrackInfo& crack) {
    std::vector<Alert> alerts;

    if (crack.max_depth > depth_threshold_) {
        Alert alert;
        alert.type = AlertType::CRACK_DEPTH_EXCEEDED;
        alert.porcelain_id = crack.porcelain_id;
        alert.crack_id = static_cast<int>(crack.id);
        alert.threshold_value = depth_threshold_;
        alert.actual_value = crack.max_depth;
        alert.unit = "μm";
        std::ostringstream oss;
        oss << "瓷器ID " << crack.porcelain_id << " 裂纹深度 " << crack.max_depth
            << "μm 超过阈值 " << depth_threshold_ << "μm";
        alert.message = oss.str();
        alert.status = AlertStatus::PENDING;
        alert.sms_sent = false;
        alert.websocket_sent = false;
        alert.created_at = std::chrono::system_clock::now();
        alerts.push_back(alert);
    }

    if (crack.max_width > width_threshold_) {
        Alert alert;
        alert.type = AlertType::CRACK_WIDTH_EXCEEDED;
        alert.porcelain_id = crack.porcelain_id;
        alert.crack_id = static_cast<int>(crack.id);
        alert.threshold_value = width_threshold_;
        alert.actual_value = crack.max_width;
        alert.unit = "μm";
        std::ostringstream oss;
        oss << "瓷器ID " << crack.porcelain_id << " 裂纹宽度 " << crack.max_width
            << "μm 超过阈值 " << width_threshold_ << "μm";
        alert.message = oss.str();
        alert.status = AlertStatus::PENDING;
        alert.sms_sent = false;
        alert.websocket_sent = false;
        alert.created_at = std::chrono::system_clock::now();
        alerts.push_back(alert);
    }

    return alerts;
}

void AlertManager::check_laser_data(const LaserMicroscopeData& data) {
    if (!data.crack_detected) return;

    for (const auto& crack : data.cracks) {
        auto alerts = check_crack_thresholds(crack);
        for (const auto& alert : alerts) {
            Alert saved_alert = alert;
            saved_alert.sensor_id = data.sensor_id;
            int64_t id = DatabaseManager::instance().insert_alert(saved_alert);
            if (id > 0) {
                saved_alert.id = id;
                enqueue_alert(saved_alert);
            }
        }
    }
}

void AlertManager::check_vibration_data(const VibrationData& data) {
    static const double VIBRATION_RMS_THRESHOLD = 1.0e-6;
    static const double VIBRATION_PEAK_THRESHOLD = 5.0e-6;

    if (data.rms_value > VIBRATION_RMS_THRESHOLD || data.peak_value > VIBRATION_PEAK_THRESHOLD) {
        Alert alert;
        alert.type = AlertType::VIBRATION_ANOMALY;
        alert.porcelain_id = data.porcelain_id;
        alert.sensor_id = data.sensor_id;
        alert.threshold_value = VIBRATION_RMS_THRESHOLD;
        alert.actual_value = data.rms_value;
        alert.unit = "m/s²";
        std::ostringstream oss;
        oss << "瓷器ID " << data.porcelain_id << " 振动异常 - RMS: "
            << data.rms_value << " m/s², 峰值: " << data.peak_value << " m/s²";
        alert.message = oss.str();
        alert.status = AlertStatus::PENDING;
        alert.sms_sent = false;
        alert.websocket_sent = false;
        alert.created_at = std::chrono::system_clock::now();

        int64_t id = DatabaseManager::instance().insert_alert(alert);
        if (id > 0) {
            alert.id = id;
            enqueue_alert(alert);
        }
    }
}

void AlertManager::check_prediction(const CrackPrediction& prediction) {
    if (prediction.risk_level == "CRITICAL" || prediction.risk_level == "HIGH") {
        Alert alert;
        alert.type = AlertType::CRACK_PROPAGATION_RISK;
        alert.crack_id = prediction.crack_id;
        alert.threshold_value = 0;
        alert.actual_value = prediction.predicted_depth_720h;
        alert.unit = "μm";
        std::ostringstream oss;
        oss << "裂纹ID " << prediction.crack_id << " 扩展风险等级: " << prediction.risk_level
            << "，预测720小时后深度: " << prediction.predicted_depth_720h << "μm";
        alert.message = oss.str();
        alert.status = AlertStatus::PENDING;
        alert.sms_sent = false;
        alert.websocket_sent = false;
        alert.created_at = std::chrono::system_clock::now();

        int64_t id = DatabaseManager::instance().insert_alert(alert);
        if (id > 0) {
            alert.id = id;
            enqueue_alert(alert);
        }
    }
}

void AlertManager::send_sms(const Alert& alert) {
    std::cout << "[SMS] 发送短信到 " << alert_phone_number_ << ": "
              << alert.message << std::endl;
}

void AlertManager::send_websocket(const Alert& alert) {
    if (!ws_server_) return;

    nlohmann::json j;
    j["type"] = "alert";
    j["data"] = {
        {"id", alert.id},
        {"alert_type", to_string(alert.type)},
        {"porcelain_id", alert.porcelain_id},
        {"crack_id", alert.crack_id},
        {"sensor_id", alert.sensor_id},
        {"threshold", alert.threshold_value},
        {"actual", alert.actual_value},
        {"unit", alert.unit},
        {"message", alert.message},
        {"status", "PENDING"}
    };

    ws_server_->broadcast_json(j);
    std::cout << "[WebSocket] 告警推送已发送: " << alert.message << std::endl;
}

void AlertManager::process_alerts() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] { return !alert_queue_.empty() || !running_; });

        while (!alert_queue_.empty()) {
            Alert alert = alert_queue_.front();
            alert_queue_.pop();
            lock.unlock();

            try {
                if (sms_enabled_) {
                    send_sms(alert);
                }

                if (websocket_enabled_) {
                    send_websocket(alert);
                }

                for (const auto& cb : callbacks_) {
                    cb(alert);
                }

            } catch (const std::exception& e) {
                std::cerr << "Error processing alert: " << e.what() << std::endl;
            }

            lock.lock();
        }
    }
}

}
