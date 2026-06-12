#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {

class ProfinetParser {
public:
    enum class PacketType : uint16_t {
        RT_CLASS_1 = 0x0001,
        RT_CLASS_2 = 0x0002,
        RT_CLASS_3 = 0x0003,
        LASER_DATA = 0x8001,
        VIBRATION_DATA = 0x8002,
        DIAGNOSIS = 0x8003,
        CONFIGURATION = 0x8004,
        ACKNOWLEDGE = 0xFFFF
    };

    struct ProfinetHeader {
        uint16_t frame_id;
        uint8_t service_id;
        uint8_t service_type;
        uint32_t cycle_counter;
        uint64_t timestamp;
        uint16_t data_status;
        uint16_t payload_length;
    };

    ProfinetParser() = default;
    ~ProfinetParser() = default;

    ProfinetPacket parse(const std::vector<uint8_t>& data,
                         const std::string& source_ip,
                         const std::string& dest_ip);

    LaserMicroscopeData parse_laser_data(const std::vector<uint8_t>& payload);
    VibrationData parse_vibration_data(const std::vector<uint8_t>& payload);

    std::vector<uint8_t> build_acknowledge(uint32_t cycle_counter);

    static constexpr size_t HEADER_SIZE = 16;

private:
    uint16_t read_u16(const std::vector<uint8_t>& data, size_t offset) const;
    uint32_t read_u32(const std::vector<uint8_t>& data, size_t offset) const;
    uint64_t read_u64(const std::vector<uint8_t>& data, size_t offset) const;
    float read_float(const std::vector<uint8_t>& data, size_t offset) const;
    double read_double(const std::vector<uint8_t>& data, size_t offset) const;

    void write_u16(std::vector<uint8_t>& data, size_t offset, uint16_t value) const;
    void write_u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) const;

    std::string packet_type_to_string(uint16_t frame_id) const;
};

}
