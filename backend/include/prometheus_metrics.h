#pragma once

#include <atomic>
#include <string>
#include <sstream>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace porcelain_monitor {
namespace metrics {

class MetricsRegistry {
public:
    static MetricsRegistry& instance() {
        static MetricsRegistry reg;
        return reg;
    }

    void increment_counter(const std::string& name, double value = 1.0,
                           const std::string& labels = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = name + labels;
        counters_[key] += value;
        counter_meta_[key] = {name, labels, "counter"};
    }

    void set_gauge(const std::string& name, double value,
                   const std::string& labels = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = name + labels;
        gauges_[key] = value;
        gauge_meta_[key] = {name, labels, "gauge"};
    }

    void observe_histogram(const std::string& name, double value,
                           const std::string& labels = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string key = name + labels;
        auto& h = histograms_[key];
        h.count++;
        h.sum += value;
        if (value <= 0.001) h.buckets[0]++;
        if (value <= 0.005) h.buckets[1]++;
        if (value <= 0.01) h.buckets[2]++;
        if (value <= 0.05) h.buckets[3]++;
        if (value <= 0.1) h.buckets[4]++;
        if (value <= 0.5) h.buckets[5]++;
        if (value <= 1.0) h.buckets[6]++;
        if (value <= 5.0) h.buckets[7]++;
        h.buckets[8]++;
        hist_meta_[key] = {name, labels, "histogram"};
    }

    std::string serialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream ss;

        for (auto& [key, meta] : counter_meta_) {
            ss << "# HELP " << meta.name << " " << meta.name << "\n";
            ss << "# TYPE " << meta.name << " counter\n";
            ss << meta.name << meta.labels << " " << std::fixed << counters_[key] << "\n";
        }

        for (auto& [key, meta] : gauge_meta_) {
            ss << "# HELP " << meta.name << " " << meta.name << "\n";
            ss << "# TYPE " << meta.name << " gauge\n";
            ss << meta.name << meta.labels << " " << std::fixed << gauges_[key] << "\n";
        }

        for (auto& [key, meta] : hist_meta_) {
            auto& h = histograms_[key];
            ss << "# HELP " << meta.name << " " << meta.name << "\n";
            ss << "# TYPE " << meta.name << " histogram\n";
            double cum = 0;
            double bounds[] = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0};
            for (int i = 0; i < 8; i++) {
                cum += h.buckets[i];
                ss << meta.name << "_bucket{le=\"" << bounds[i] << "\"" << meta.labels.substr(1) << "} " << (long long)cum << "\n";
            }
            ss << meta.name << "_bucket{le=\"+Inf\"" << meta.labels.substr(1) << "} " << (long long)h.count << "\n";
            ss << meta.name << "_sum" << meta.labels << " " << std::fixed << h.sum << "\n";
            ss << meta.name << "_count" << meta.labels << " " << (long long)h.count << "\n";
        }

        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        ss << "# EOF ts=" << ts << "\n";

        return ss.str();
    }

private:
    MetricsRegistry() = default;

    struct Meta { std::string name, labels, type; };

    struct HistogramData {
        long long count = 0;
        double sum = 0.0;
        double buckets[9] = {};
    };

    std::mutex mutex_;
    std::unordered_map<std::string, double> counters_;
    std::unordered_map<std::string, Meta> counter_meta_;
    std::unordered_map<std::string, double> gauges_;
    std::unordered_map<std::string, Meta> gauge_meta_;
    std::unordered_map<std::string, HistogramData> histograms_;
    std::unordered_map<std::string, Meta> hist_meta_;
};

inline void record_laser_data(int sensor_id, int porcelain_id, bool crack_detected) {
    auto& m = MetricsRegistry::instance();
    m.increment_counter("pm_laser_data_total", 1.0);
    if (crack_detected) {
        m.increment_counter("pm_crack_detected_total", 1.0);
    }
    m.set_gauge("pm_last_laser_sensor_id", sensor_id);
}

inline void record_vibration_data(int sensor_id, double rms) {
    auto& m = MetricsRegistry::instance();
    m.increment_counter("pm_vibration_data_total", 1.0);
    m.observe_histogram("pm_vibration_rms", rms);
}

inline void record_alert(const std::string& alert_type) {
    MetricsRegistry::instance().increment_counter("pm_alerts_total", 1.0,
        "{type=\"" + alert_type + "\"}");
}

inline void record_module_processed(const std::string& module_name) {
    MetricsRegistry::instance().increment_counter("pm_module_processed_total", 1.0,
        "{module=\"" + module_name + "\"}");
}

inline void record_db_write(double duration_sec) {
    MetricsRegistry::instance().observe_histogram("pm_db_write_duration_seconds", duration_sec);
}

}
}
