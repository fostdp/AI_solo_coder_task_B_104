#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <random>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <boost/asio.hpp>
#include "../backend/include/profinet_parser.h"
#include "../backend/include/common.h"

namespace porcelain_monitor {
namespace simulator {

using boost::asio::ip::tcp;

struct SensorConfig {
    int id;
    std::string name;
    int porcelain_id;
    std::string type;
    std::string server_ip;
    uint16_t server_port;
    int interval_ms;
};

class ShockVibrationInjector {
public:
    ShockVibrationInjector(boost::asio::io_context& ioc,
                           std::function<void()> inject_fn)
        : ioc_(ioc), inject_fn_(std::move(inject_fn)), timer_(ioc),
          running_(false), enabled_(true), rng_(std::random_device{}()) {}

    void start() {
        running_ = true;
        schedule_next();
    }

    void stop() {
        running_ = false;
        timer_.cancel();
    }

    void set_enabled(bool e) { enabled_ = e; }

private:
    void schedule_next() {
        if (!running_ || !enabled_) return;

        std::uniform_int_distribution<int> dist(600, 7200);
        int delay_sec = dist(rng_);

        std::cout << "[ShockInjector] 下次冲击振动注入: "
                  << delay_sec << "秒后" << std::endl;

        timer_.expires_after(std::chrono::seconds(delay_sec));
        timer_.async_wait(
            [this](boost::system::error_code ec) {
                if (!ec && running_ && enabled_) {
                    std::cout << "[ShockInjector] *** 注入冲击振动 ***" << std::endl;
                    inject_fn_();
                    schedule_next();
                }
            });
    }

    boost::asio::io_context& ioc_;
    std::function<void()> inject_fn_;
    boost::asio::steady_timer timer_;
    std::atomic<bool> running_;
    std::atomic<bool> enabled_;
    std::mt19937 rng_;
};

class ProfinetClient {
public:
    ProfinetClient(boost::asio::io_context& ioc, const SensorConfig& config)
        : ioc_(ioc),
          socket_(ioc),
          config_(config),
          cycle_counter_(0),
          running_(false),
          reconnect_timer_(ioc),
          disconnect_sim_timer_(ioc),
          connected_(false),
          reconnect_count_(0),
          max_reconnect_attempts_(10),
          shock_mode_(false) {}

    void start() {
        running_ = true;
        connect();
    }

    void stop() {
        running_ = false;
        boost::system::error_code ec;
        socket_.close(ec);
        if (timer_) timer_->cancel();
        reconnect_timer_.cancel();
        disconnect_sim_timer_.cancel();
    }

    void set_shock_mode(bool mode) { shock_mode_ = mode; }

private:
    void connect() {
        tcp::endpoint endpoint(boost::asio::ip::make_address(config_.server_ip),
                               config_.server_port);
        socket_.async_connect(endpoint,
            [this](boost::system::error_code ec) {
                if (!ec) {
                    connected_ = true;
                    reconnect_count_ = 0;
                    std::cout << "[" << config_.name << "] 连接到服务器成功" << std::endl;
                    start_sending();
                    schedule_random_disconnect();
                } else {
                    connected_ = false;
                    std::cerr << "[" << config_.name << "] 连接失败: " << ec.message() << std::endl;
                    if (running_) schedule_reconnect();
                }
            });
    }

    void schedule_reconnect() {
        if (reconnect_count_ >= max_reconnect_attempts_) {
            std::cerr << "[" << config_.name << "] 达到最大重连次数" << std::endl;
            return;
        }
        reconnect_count_++;
        int delay_sec = std::min(30, 2 * reconnect_count_);
        std::cout << "[" << config_.name << "] " << delay_sec
                  << "秒后重连 (第" << reconnect_count_ << "次)" << std::endl;
        reconnect_timer_.expires_after(std::chrono::seconds(delay_sec));
        reconnect_timer_.async_wait(
            [this](boost::system::error_code ec) {
                if (!ec && running_) {
                    boost::system::error_code sock_ec;
                    socket_.close(sock_ec);
                    connect();
                }
            });
    }

    void schedule_random_disconnect() {
        std::uniform_int_distribution<int> dist(180, 1800);
        int disconnect_after_sec = dist(rng_);
        disconnect_sim_timer_.expires_after(std::chrono::seconds(disconnect_after_sec));
        disconnect_sim_timer_.async_wait(
            [this](boost::system::error_code ec) {
                if (!ec && running_ && connected_) {
                    std::cout << "[" << config_.name << "] *** 模拟断线 ***" << std::endl;
                    connected_ = false;
                    boost::system::error_code sock_ec;
                    socket_.close(sock_ec);
                    if (timer_) timer_->cancel();
                    schedule_reconnect();
                }
            });
    }

    void start_sending() {
        timer_ = std::make_unique<boost::asio::steady_timer>(ioc_);
        schedule_next_send();
    }

    void schedule_next_send() {
        if (!running_) return;
        timer_->expires_after(std::chrono::milliseconds(config_.interval_ms));
        timer_->async_wait(
            [this](boost::system::error_code ec) {
                if (!ec && running_) {
                    send_data();
                    schedule_next_send();
                }
            });
    }

    void send_data() {
        cycle_counter_++;
        std::vector<uint8_t> payload;
        if (config_.type == "LASER") {
            payload = build_laser_payload();
        } else {
            payload = build_vibration_payload();
        }
        std::vector<uint8_t> packet = build_profinet_packet(
            config_.type == "LASER" ? 0x8001 : 0x8002, payload);
        boost::asio::async_write(socket_,
            boost::asio::buffer(packet),
            [this](boost::system::error_code ec, std::size_t) {
                if (ec) {
                    std::cerr << "[" << config_.name << "] 发送失败: " << ec.message() << std::endl;
                    connected_ = false;
                    if (running_) schedule_reconnect();
                }
            });
        if (cycle_counter_ % 10 == 0) {
            std::cout << "[" << config_.name << "] 已发送 " << cycle_counter_
                      << " 个数据包" << std::endl;
        }
    }

    std::vector<uint8_t> build_laser_payload() {
        std::vector<uint8_t> payload;
        std::mt19937 rng(std::random_device{}());
        std::normal_distribution<> depth_dist(shock_mode_ ? 250 : 150, shock_mode_ ? 80 : 50);
        std::normal_distribution<> width_dist(shock_mode_ ? 60 : 40, shock_mode_ ? 25 : 15);
        std::uniform_real_distribution<> pos_dist(-10.0, 10.0);

        uint32_t sensor_id = static_cast<uint32_t>(config_.id);
        uint32_t porcelain_id = static_cast<uint32_t>(config_.porcelain_id);

        append_u32(payload, sensor_id);
        append_u32(payload, porcelain_id);

        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        append_u64(payload, timestamp);

        append_double(payload, 0.0);
        append_double(payload, 0.0);
        append_double(payload, 20.0);
        append_double(payload, 20.0);
        append_double(payload, 0.1e-6);

        bool crack_detected = shock_mode_ || (cycle_counter_ % 3 == 0) || (depth_dist(rng) > 200);
        payload.push_back(crack_detected ? 1 : 0);

        uint32_t crack_count = crack_detected ?
            (shock_mode_ ? std::uniform_int_distribution<>(3, 6)(rng)
                         : std::uniform_int_distribution<>(1, 3)(rng)) : 0;
        append_u32(payload, crack_count);

        for (uint32_t i = 0; i < crack_count; ++i) {
            double max_depth = std::abs(depth_dist(rng));
            double max_width = std::abs(width_dist(rng));
            double total_length = 5.0 + std::abs(std::normal_distribution<>(shock_mode_ ? 15.0 : 10.0, 5.0)(rng));

            append_double(payload, max_depth);
            append_double(payload, max_width);
            append_double(payload, total_length);

            uint32_t point_count = 20 + std::uniform_int_distribution<>(0, 30)(rng);
            append_u32(payload, point_count);

            for (uint32_t j = 0; j < point_count; ++j) {
                double t = static_cast<double>(j) / (point_count - 1);
                double x = pos_dist(rng);
                double y = pos_dist(rng);
                double z = pos_dist(rng);
                double depth = max_depth * (0.3 + 0.7 * std::sin(M_PI * t));
                double width = max_width * (0.5 + 0.5 * std::sin(M_PI * t));

                append_double(payload, x);
                append_double(payload, y);
                append_double(payload, z);
                append_double(payload, depth);
                append_double(payload, width);
                append_double(payload, 0.0);
                append_double(payload, 0.0);
                append_double(payload, 1.0);
                append_double(payload, 0.01);
            }
        }

        json processed;
        processed["scan_quality"] = shock_mode_ ? 0.75 : 0.95;
        processed["noise_level"] = shock_mode_ ? 0.15 : 0.02;
        processed["shock_vibration"] = shock_mode_;
        std::string json_str = processed.dump();
        append_u32(payload, static_cast<uint32_t>(json_str.size()));
        payload.insert(payload.end(), json_str.begin(), json_str.end());

        return payload;
    }

    std::vector<uint8_t> build_vibration_payload() {
        std::vector<uint8_t> payload;
        std::mt19937 rng(std::random_device{}());

        double shock_mult = shock_mode_ ? 50.0 : 1.0;
        std::normal_distribution<> rms_dist(1.0e-7 * shock_mult, 5.0e-8 * shock_mult);
        std::normal_distribution<> peak_dist(5.0e-7 * shock_mult, 2.0e-7 * shock_mult);
        std::normal_distribution<> temp_dist(22.0, 1.0);
        std::normal_distribution<> hum_dist(50.0, 5.0);

        uint32_t sensor_id = static_cast<uint32_t>(config_.id + 20);
        uint32_t porcelain_id = static_cast<uint32_t>(config_.porcelain_id);

        append_u32(payload, sensor_id);
        append_u32(payload, porcelain_id);

        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        append_u64(payload, timestamp);

        double rms = std::abs(rms_dist(rng));
        double peak = std::abs(peak_dist(rng));
        double dom_freq = shock_mode_
            ? 200.0 + std::normal_distribution<>(0, 20.0)(rng)
            : 50.0 + std::normal_distribution<>(0, 5.0)(rng);

        append_double(payload, rms);
        append_double(payload, peak);
        append_double(payload, dom_freq);

        float temp = static_cast<float>(temp_dist(rng));
        float hum = static_cast<float>(hum_dist(rng));
        append_float(payload, temp);
        append_float(payload, hum);

        uint32_t amp_count = 100;
        append_u32(payload, amp_count);
        for (uint32_t i = 0; i < amp_count; ++i) {
            double freq = i * 10.0;
            double base = rms * std::exp(-freq / 500.0);
            double shock_amp = shock_mode_ ? base * (1.0 + 5.0 * std::exp(-std::abs(freq - dom_freq) / 50.0)) : base;
            double amp = shock_amp * (1.0 + 0.1 * std::normal_distribution<>()(rng));
            append_double(payload, amp);
        }

        json spectrum;
        spectrum["peak_frequencies"] = shock_mode_
            ? json{dom_freq, dom_freq * 2.0, dom_freq * 3.0}
            : json{50.0, 150.0, 250.0};
        spectrum["harmonic_distortion"] = shock_mode_ ? 0.35 : 0.05;
        spectrum["shock_vibration"] = shock_mode_;
        std::string json_str = spectrum.dump();
        append_u32(payload, static_cast<uint32_t>(json_str.size()));
        payload.insert(payload.end(), json_str.begin(), json_str.end());

        return payload;
    }

    std::vector<uint8_t> build_profinet_packet(uint16_t frame_id,
                                                const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> packet;
        append_u16(packet, frame_id);
        packet.push_back(0x01);
        packet.push_back(0x01);
        append_u32(packet, cycle_counter_);
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        append_u64(packet, timestamp);
        append_u16(packet, 0x0001);
        append_u16(packet, static_cast<uint16_t>(payload.size()));
        packet.push_back(0x00);
        packet.push_back(0x00);
        packet.push_back(0x00);
        packet.push_back(0x00);
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    void append_u16(std::vector<uint8_t>& buf, uint16_t value) {
        buf.push_back(static_cast<uint8_t>(value & 0xFF));
        buf.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void append_u32(std::vector<uint8_t>& buf, uint32_t value) {
        for (int i = 0; i < 4; ++i)
            buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }

    void append_u64(std::vector<uint8_t>& buf, uint64_t value) {
        for (int i = 0; i < 8; ++i)
            buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }

    void append_float(std::vector<uint8_t>& buf, float value) {
        uint32_t int_val;
        std::memcpy(&int_val, &value, sizeof(float));
        append_u32(buf, int_val);
    }

    void append_double(std::vector<uint8_t>& buf, double value) {
        uint64_t int_val;
        std::memcpy(&int_val, &value, sizeof(double));
        append_u64(buf, int_val);
    }

    boost::asio::io_context& ioc_;
    tcp::socket socket_;
    SensorConfig config_;
    uint32_t cycle_counter_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<bool> shock_mode_;
    int reconnect_count_;
    int max_reconnect_attempts_;
    std::unique_ptr<boost::asio::steady_timer> timer_;
    boost::asio::steady_timer reconnect_timer_;
    boost::asio::steady_timer disconnect_sim_timer_;
    std::mt19937 rng_{std::random_device{}()};
};

}
}

int main(int argc, char* argv[]) {
    using namespace porcelain_monitor::simulator;

    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 34964;
    int interval_ms = 10800000;
    int laser_count = 20;
    int vibration_count = 40;
    bool shock_enabled = true;

    const char* env;
    if ((env = std::getenv("SIM_SERVER_IP"))) server_ip = env;
    if ((env = std::getenv("SIM_SERVER_PORT"))) server_port = static_cast<uint16_t>(std::atoi(env));
    if ((env = std::getenv("SIM_INTERVAL_MS"))) interval_ms = std::atoi(env);
    if ((env = std::getenv("SIM_LASER_COUNT"))) laser_count = std::atoi(env);
    if ((env = std::getenv("SIM_VIBRATION_COUNT"))) vibration_count = std::atoi(env);
    if ((env = std::getenv("SIM_SHOCK_ENABLED"))) shock_enabled = (std::string(env) == "1" || std::string(env) == "true");

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) server_port = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc >= 4) interval_ms = std::atoi(argv[3]);

    std::cout << "========================================" << std::endl;
    std::cout << "  PROFINET 传感器模拟器 (工程化版)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "服务器地址: " << server_ip << ":" << server_port << std::endl;
    std::cout << "发送间隔: " << interval_ms << "ms ("
              << interval_ms / 1000 << "秒)" << std::endl;
    std::cout << "显微镜数量: " << laser_count << std::endl;
    std::cout << "振动传感器: " << vibration_count << std::endl;
    std::cout << "冲击振动注入: " << (shock_enabled ? "启用" : "禁用") << std::endl;

    boost::asio::io_context ioc;
    std::vector<std::unique_ptr<ProfinetClient>> clients;

    for (int i = 1; i <= laser_count; ++i) {
        SensorConfig cfg;
        cfg.id = i;
        cfg.name = "激光共聚焦显微镜 #" + std::to_string(i);
        cfg.porcelain_id = i;
        cfg.type = "LASER";
        cfg.server_ip = server_ip;
        cfg.server_port = server_port;
        cfg.interval_ms = interval_ms + (i % 5) * 1000;

        auto client = std::make_unique<ProfinetClient>(ioc, cfg);
        client->start();
        clients.push_back(std::move(client));
    }

    for (int i = 1; i <= vibration_count; ++i) {
        SensorConfig cfg;
        cfg.id = i;
        cfg.name = "微振动传感器 #" + std::to_string(i);
        cfg.porcelain_id = ((i - 1) % 200) + 1;
        cfg.type = "VIBRATION";
        cfg.server_ip = server_ip;
        cfg.server_port = server_port;
        cfg.interval_ms = interval_ms + (i % 5) * 1000;

        auto client = std::make_unique<ProfinetClient>(ioc, cfg);
        client->start();
        clients.push_back(std::move(client));
    }

    std::unique_ptr<ShockVibrationInjector> shock_injector;
    if (shock_enabled) {
        shock_injector = std::make_unique<ShockVibrationInjector>(ioc,
            [&clients]() {
                for (auto& c : clients) {
                    c->set_shock_mode(true);
                }
                std::cout << "[ShockInjector] 所有传感器已切换到冲击模式" << std::endl;

                std::thread([&clients]() {
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    for (auto& c : clients) {
                        c->set_shock_mode(false);
                    }
                    std::cout << "[ShockInjector] 冲击模式已结束，恢复正常" << std::endl;
                }).detach();
            });
        shock_injector->start();
    }

    std::cout << "========================================" << std::endl;
    std::cout << "按 Ctrl+C 停止..." << std::endl;

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&ioc]() { ioc.run(); });
    }

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}
