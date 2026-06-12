#include "profinet_parser.h"
#include <cstring>
#include <stdexcept>

namespace porcelain_monitor {

uint16_t ProfinetParser::read_u16(const std::vector<uint8_t>& data, size_t offset) const {
    if (offset + 1 >= data.size()) throw std::out_of_range("read_u16 offset out of range");
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t ProfinetParser::read_u32(const std::vector<uint8_t>& data, size_t offset) const {
    if (offset + 3 >= data.size()) throw std::out_of_range("read_u32 offset out of range");
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint64_t ProfinetParser::read_u64(const std::vector<uint8_t>& data, size_t offset) const {
    if (offset + 7 >= data.size()) throw std::out_of_range("read_u64 offset out of range");
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    return value;
}

float ProfinetParser::read_float(const std::vector<uint8_t>& data, size_t offset) const {
    if (offset + 3 >= data.size()) throw std::out_of_range("read_float offset out of range");
    uint32_t int_val = read_u32(data, offset);
    float value;
    std::memcpy(&value, &int_val, sizeof(float));
    return value;
}

double ProfinetParser::read_double(const std::vector<uint8_t>& data, size_t offset) const {
    if (offset + 7 >= data.size()) throw std::out_of_range("read_double offset out of range");
    uint64_t int_val = read_u64(data, offset);
    double value;
    std::memcpy(&value, &int_val, sizeof(double));
    return value;
}

void ProfinetParser::write_u16(std::vector<uint8_t>& data, size_t offset, uint16_t value) const {
    data[offset] = static_cast<uint8_t>(value & 0xFF);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void ProfinetParser::write_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) const {
    for (int i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

std::string ProfinetParser::packet_type_to_string(uint16_t frame_id) const {
    switch (frame_id) {
        case static_cast<uint16_t>(PacketType::RT_CLASS_1): return "RT_CLASS_1";
        case static_cast<uint16_t>(PacketType::RT_CLASS_2): return "RT_CLASS_2";
        case static_cast<uint16_t>(PacketType::RT_CLASS_3): return "RT_CLASS_3";
        case static_cast<uint16_t>(PacketType::LASER_DATA): return "LASER_DATA";
        case static_cast<uint16_t>(PacketType::VIBRATION_DATA): return "VIBRATION_DATA";
        case static_cast<uint16_t>(PacketType::DIAGNOSIS): return "DIAGNOSIS";
        case static_cast<uint16_t>(PacketType::CONFIGURATION): return "CONFIGURATION";
        case static_cast<uint16_t>(PacketType::ACKNOWLEDGE): return "ACKNOWLEDGE";
        default: return "UNKNOWN_" + std::to_string(frame_id);
    }
}

ProfinetPacket ProfinetParser::parse(const std::vector<uint8_t>& data,
                                      const std::string& source_ip,
                                      const std::string& dest_ip) {
    if (data.size() < HEADER_SIZE) {
        throw std::runtime_error("Packet too small for PROFINET header");
    }

    ProfinetHeader header;
    header.frame_id = read_u16(data, 0);
    header.service_id = data[2];
    header.service_type = data[3];
    header.cycle_counter = read_u32(data, 4);
    header.timestamp = read_u64(data, 8);
    header.data_status = read_u16(data, 16);
    header.payload_length = read_u16(data, 18);

    ProfinetPacket packet;
    packet.source_ip = source_ip;
    packet.destination_ip = dest_ip;
    packet.frame_id = header.frame_id;
    packet.packet_type = packet_type_to_string(header.frame_id);
    packet.received_at = std::chrono::system_clock::now();

    size_t payload_offset = HEADER_SIZE + 4;
    if (data.size() >= payload_offset + header.payload_length) {
        packet.payload.assign(data.begin() + payload_offset,
                              data.begin() + payload_offset + header.payload_length);
    } else {
        throw std::runtime_error("Incomplete PROFINET payload");
    }

    return packet;
}

LaserMicroscopeData ProfinetParser::parse_laser_data(const std::vector<uint8_t>& payload) {
    LaserMicroscopeData data;
    size_t offset = 0;

    data.sensor_id = read_u32(payload, offset); offset += 4;
    data.porcelain_id = read_u32(payload, offset); offset += 4;

    uint64_t timestamp_ns = read_u64(payload, offset); offset += 8;
    data.measurement_time = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(timestamp_ns));

    data.scan_area[0] = read_double(payload, offset); offset += 8;
    data.scan_area[1] = read_double(payload, offset); offset += 8;
    data.scan_area[2] = read_double(payload, offset); offset += 8;
    data.scan_area[3] = read_double(payload, offset); offset += 8;

    data.resolution = read_double(payload, offset); offset += 8;
    data.crack_detected = (payload[offset++] != 0);
    data.crack_count = read_u32(payload, offset); offset += 4;

    for (int i = 0; i < data.crack_count; ++i) {
        CrackInfo crack;
        crack.porcelain_id = data.porcelain_id;
        crack.max_depth = read_double(payload, offset); offset += 8;
        crack.max_width = read_double(payload, offset); offset += 8;
        crack.total_length = read_double(payload, offset); offset += 8;

        uint32_t point_count = read_u32(payload, offset); offset += 4;

        for (uint32_t j = 0; j < point_count; ++j) {
            Point3D point;
            point.x = read_double(payload, offset); offset += 8;
            point.y = read_double(payload, offset); offset += 8;
            point.z = read_double(payload, offset); offset += 8;
            point.depth = read_double(payload, offset); offset += 8;
            point.width = read_double(payload, offset); offset += 8;
            point.normal = std::array<double, 3>{
                read_double(payload, offset), offset += 8,
                read_double(payload, offset), offset += 8,
                read_double(payload, offset)
            };
            offset += 8;
            point.curvature = read_double(payload, offset); offset += 8;
            crack.points.push_back(point);
        }

        data.cracks.push_back(crack);
    }

    uint32_t json_length = read_u32(payload, offset); offset += 4;
    if (json_length > 0 && offset + json_length <= payload.size()) {
        std::string json_str(payload.begin() + offset,
                             payload.begin() + offset + json_length);
        data.processed_data = json::parse(json_str);
    }

    return data;
}

VibrationData ProfinetParser::parse_vibration_data(const std::vector<uint8_t>& payload) {
    VibrationData data;
    size_t offset = 0;

    data.sensor_id = read_u32(payload, offset); offset += 4;
    data.porcelain_id = read_u32(payload, offset); offset += 4;

    uint64_t timestamp_ns = read_u64(payload, offset); offset += 8;
    data.measurement_time = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(timestamp_ns));

    data.rms_value = read_double(payload, offset); offset += 8;
    data.peak_value = read_double(payload, offset); offset += 8;
    data.dominant_frequency = read_double(payload, offset); offset += 8;
    data.temperature = read_float(payload, offset); offset += 4;
    data.humidity = read_float(payload, offset); offset += 4;

    uint32_t amplitude_count = read_u32(payload, offset); offset += 4;
    for (uint32_t i = 0; i < amplitude_count; ++i) {
        data.amplitude.push_back(read_double(payload, offset));
        offset += 8;
    }

    uint32_t json_length = read_u32(payload, offset); offset += 4;
    if (json_length > 0 && offset + json_length <= payload.size()) {
        std::string json_str(payload.begin() + offset,
                             payload.begin() + offset + json_length);
        data.frequency_spectrum = json::parse(json_str);
    }

    return data;
}

std::vector<uint8_t> ProfinetParser::build_acknowledge(uint32_t cycle_counter) {
    std::vector<uint8_t> packet(HEADER_SIZE + 4, 0);

    write_u16(packet, 0, static_cast<uint16_t>(PacketType::ACKNOWLEDGE));
    packet[2] = 0x01;
    packet[3] = 0x01;
    write_u32(packet, 4, cycle_counter);

    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (int i = 0; i < 8; ++i) {
        packet[8 + i] = static_cast<uint8_t>((now_ns >> (i * 8)) & 0xFF);
    }

    write_u16(packet, 16, 0x0001);
    write_u16(packet, 18, 4);

    write_u32(packet, 20, 0);

    return packet;
}

}
