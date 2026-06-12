#include "tcp_server.h"
#include <iostream>
#include <thread>

namespace porcelain_monitor {

void TcpSession::start() {
    do_read();
}

void TcpSession::do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
        boost::asio::buffer(read_buf_),
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                frame_buf_.insert(frame_buf_.end(),
                    read_buf_.begin(), read_buf_.begin() + bytes_transferred);
                try_parse_frames();
                do_read();
            } else if (ec != boost::asio::error::eof) {
                std::cerr << "TCP session read error: " << ec.message() << std::endl;
            }
        });
}

void TcpSession::try_parse_frames() {
    while (frame_buf_.size() >= ProfinetParser::HEADER_SIZE) {
        uint16_t payload_len = static_cast<uint16_t>(frame_buf_[12]) |
                               (static_cast<uint16_t>(frame_buf_[13]) << 8);

        size_t frame_total = ProfinetParser::HEADER_SIZE + payload_len;

        if (frame_buf_.size() < frame_total) {
            break;
        }

        try {
            std::vector<uint8_t> frame_data(frame_buf_.begin(),
                                             frame_buf_.begin() + frame_total);

            auto remote_ep = socket_.remote_endpoint();
            auto local_ep = socket_.local_endpoint();

            ProfinetPacket packet = parser_.parse(
                frame_data,
                remote_ep.address().to_string(),
                local_ep.address().to_string());

            if (packet.frame_id == static_cast<uint16_t>(ProfinetParser::PacketType::LASER_DATA)) {
                LaserMicroscopeData laser_data = parser_.parse_laser_data(packet.payload);
                if (laser_callback_) laser_callback_(packet, laser_data);
            } else if (packet.frame_id == static_cast<uint16_t>(ProfinetParser::PacketType::VIBRATION_DATA)) {
                VibrationData vib_data = parser_.parse_vibration_data(packet.payload);
                if (vibration_callback_) vibration_callback_(packet, vib_data);
            }

            uint32_t cycle_counter = parser_.read_u32(frame_data, 4);
            send_acknowledge(cycle_counter);

        } catch (const std::exception& e) {
            std::cerr << "Error parsing PROFINET frame, discarding: " << e.what() << std::endl;
        }

        frame_buf_.erase(frame_buf_.begin(), frame_buf_.begin() + frame_total);
    }

    if (frame_buf_.size() > MAX_FRAME_BUF_SIZE) {
        std::cerr << "Frame buffer overflow (" << frame_buf_.size()
                  << " bytes), resetting" << std::endl;
        frame_buf_.clear();
    }
}

void TcpSession::send_acknowledge(uint32_t cycle_counter) {
    auto self(shared_from_this());
    auto ack = parser_.build_acknowledge(cycle_counter);

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(ack),
        [self](boost::system::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "Error sending acknowledge: " << ec.message() << std::endl;
            }
        });
}

void UdpServer::start() {
    do_receive();
}

void UdpServer::do_receive() {
    auto self(shared_from_this());
    socket_.async_receive_from(
        boost::asio::buffer(recv_buffer_),
        remote_endpoint_,
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
            handle_receive(ec, bytes_transferred);
        });
}

void UdpServer::handle_receive(const boost::system::error_code& error,
                                size_t bytes_transferred) {
    if (!error && bytes_transferred > 0) {
        try {
            std::vector<uint8_t> data(recv_buffer_.begin(),
                                      recv_buffer_.begin() + bytes_transferred);

            auto local_endpoint = socket_.local_endpoint();
            ProfinetPacket packet = parser_.parse(
                data,
                remote_endpoint_.address().to_string(),
                local_endpoint.address().to_string());

            if (packet.frame_id == static_cast<uint16_t>(ProfinetParser::PacketType::LASER_DATA)) {
                LaserMicroscopeData laser_data = parser_.parse_laser_data(packet.payload);
                if (laser_callback_) {
                    laser_callback_(packet, laser_data);
                }
            } else if (packet.frame_id == static_cast<uint16_t>(ProfinetParser::PacketType::VIBRATION_DATA)) {
                VibrationData vib_data = parser_.parse_vibration_data(packet.payload);
                if (vibration_callback_) {
                    vibration_callback_(packet, vib_data);
                }
            }

            uint32_t cycle_counter = parser_.read_u32(data, 4);
            auto ack = parser_.build_acknowledge(cycle_counter);

            socket_.async_send_to(
                boost::asio::buffer(ack),
                remote_endpoint_,
                [](boost::system::error_code, std::size_t) {});

        } catch (const std::exception& e) {
            std::cerr << "Error parsing UDP PROFINET packet: " << e.what() << std::endl;
        }
    }

    do_receive();
}

ProfinetServer::ProfinetServer(boost::asio::io_context& io_context, uint16_t port)
    : io_context_(io_context),
      tcp_acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    udp_server_ = std::make_shared<UdpServer>(
        io_context, port,
        [this](const ProfinetPacket& p, const LaserMicroscopeData& d) {
            if (laser_callback_) laser_callback_(p, d);
        },
        [this](const ProfinetPacket& p, const VibrationData& d) {
            if (vibration_callback_) vibration_callback_(p, d);
        });
}

void ProfinetServer::start() {
    udp_server_->start();
    do_accept();
    std::cout << "PROFINET server started on port " << tcp_acceptor_.local_endpoint().port() << std::endl;
}

void ProfinetServer::stop() {
    boost::system::error_code ec;
    tcp_acceptor_.close(ec);
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
}

void ProfinetServer::do_accept() {
    tcp_acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                auto session = std::make_shared<TcpSession>(
                    std::move(socket),
                    laser_callback_,
                    vibration_callback_);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    sessions_.push_back(session);
                }
                session->start();
            }
            do_accept();
        });
}

void ProfinetServer::handle_accept(std::shared_ptr<TcpSession> session,
                                    const boost::system::error_code& error) {
    if (!error) {
        session->start();
    }
    do_accept();
}

}
