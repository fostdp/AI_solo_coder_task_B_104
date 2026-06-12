#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <string>
#include "common.h"
#include "tcp_server.h"
#include "lockfree_queue.h"
#include "module_base.h"

namespace porcelain_monitor {
namespace modules {

class ProfinetParserModule : public SourceModuleBase {
public:
    using LaserQueue = concurrency::SharedQueue<ParsedLaserMessage>;
    using VibrationQueue = concurrency::SharedQueue<ParsedVibrationMessage>;

    ProfinetParserModule(boost::asio::io_context& ioc, uint16_t port)
        : server_(std::make_unique<ProfinetServer>(ioc, port)),
          laser_out_(concurrency::make_queue<ParsedLaserMessage>()),
          vibration_out_(concurrency::make_queue<ParsedVibrationMessage>()) {}

    void set_laser_output(LaserQueue q) { laser_out_ = std::move(q); }
    void set_vibration_output(VibrationQueue q) { vibration_out_ = std::move(q); }

    LaserQueue laser_output() const { return laser_out_; }
    VibrationQueue vibration_output() const { return vibration_out_; }

    void start() override {
        server_->set_laser_callback(
            [this](const ProfinetPacket& packet, const LaserMicroscopeData& data) {
                ParsedLaserMessage msg{packet, data};
                laser_out_->push(std::move(msg));
            });
        server_->set_vibration_callback(
            [this](const ProfinetPacket& packet, const VibrationData& data) {
                ParsedVibrationMessage msg{packet, data};
                vibration_out_->push(std::move(msg));
            });
        server_->start();
    }

    void stop() override {
        server_->stop();
    }

private:
    std::unique_ptr<ProfinetServer> server_;
    LaserQueue laser_out_;
    VibrationQueue vibration_out_;
};

}
}
