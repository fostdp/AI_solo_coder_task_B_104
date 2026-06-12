#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <array>
#include <functional>
#include <mutex>
#include "common.h"
#include "profinet_parser.h"

namespace porcelain_monitor {

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    using DataCallback = std::function<void(const ProfinetPacket&, const LaserMicroscopeData&)>;
    using VibrationCallback = std::function<void(const ProfinetPacket&, const VibrationData&)>;

    TcpSession(tcp::socket socket,
               DataCallback laser_cb,
               VibrationCallback vib_cb)
        : socket_(std::move(socket)),
          laser_callback_(std::move(laser_cb)),
          vibration_callback_(std::move(vib_cb)) {}

    void start();

private:
    void do_read();
    void try_parse_frames();
    void send_acknowledge(uint32_t cycle_counter);

    tcp::socket socket_;
    std::array<uint8_t, 8192> read_buf_{};
    std::vector<uint8_t> frame_buf_;
    static constexpr size_t MAX_FRAME_BUF_SIZE = 4 * 1024 * 1024;
    ProfinetParser parser_;
    DataCallback laser_callback_;
    VibrationCallback vibration_callback_;
};

class UdpServer : public std::enable_shared_from_this<UdpServer> {
public:
    using DataCallback = std::function<void(const ProfinetPacket&, const LaserMicroscopeData&)>;
    using VibrationCallback = std::function<void(const ProfinetPacket&, const VibrationData&)>;

    UdpServer(boost::asio::io_context& io_context,
              uint16_t port,
              DataCallback laser_cb,
              VibrationCallback vib_cb)
        : socket_(io_context, udp::endpoint(udp::v4(), port)),
          laser_callback_(std::move(laser_cb)),
          vibration_callback_(std::move(vib_cb)) {}

    void start();

private:
    void do_receive();
    void handle_receive(const boost::system::error_code& error, size_t bytes_transferred);

    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::vector<uint8_t> recv_buffer_{65536};
    ProfinetParser parser_;
    DataCallback laser_callback_;
    VibrationCallback vibration_callback_;
};

class ProfinetServer {
public:
    using DataCallback = std::function<void(const ProfinetPacket&, const LaserMicroscopeData&)>;
    using VibrationCallback = std::function<void(const ProfinetPacket&, const VibrationData&)>;

    ProfinetServer(boost::asio::io_context& io_context, uint16_t port);
    ~ProfinetServer() = default;

    void set_laser_callback(DataCallback cb) { laser_callback_ = std::move(cb); }
    void set_vibration_callback(VibrationCallback cb) { vibration_callback_ = std::move(cb); }

    void start();
    void stop();

private:
    boost::asio::io_context& io_context_;
    tcp::acceptor tcp_acceptor_;
    std::shared_ptr<UdpServer> udp_server_;
    DataCallback laser_callback_;
    VibrationCallback vibration_callback_;
    std::vector<std::shared_ptr<TcpSession>> sessions_;
    std::mutex mutex_;

    void do_accept();
    void handle_accept(std::shared_ptr<TcpSession> session,
                       const boost::system::error_code& error);
};

}
