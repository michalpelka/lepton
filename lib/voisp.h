#pragma once
#include <span>
#include <vector>
#include <cstdint>
#include <cassert>
#include <optional>
#include <cstdint>
#include <cstring>
namespace VoISP {
    constexpr size_t VoISPPacketSize = 4+160;

    uint16_t inline swapBytes(uint16_t val) {
        return (val >> 8) | (val << 8);
    }


    static inline uint16_t packet_id(const uint8_t* packet) {
        return ((uint16_t)packet[0] << 8) | packet[1];
    }

    static inline uint16_t packet_crc(const uint8_t* packet) {
        return ((uint16_t)packet[2] << 8) | packet[3];
    }

    static inline int is_discard_packet(uint16_t id16) {
        return (id16 & 0x0F00) == 0x0F00;
    }

    static inline uint8_t getTTTBits(uint16_t id16) {
        // get first four MSB of ID
        return static_cast<uint8_t>((id16 >> 12) & 0x0F);
    }

    static inline uint16_t getPacketNumber(uint16_t id16) {
        // get first four MSB of ID
        return id16 & 0x0FFF;
    }

    static inline std::optional<uint8_t> getSegmentNumber(uint16_t id16) {

        const auto packetNo = getPacketNumber(id16);
        if (packetNo != 20) {
            return std::nullopt;
        }
        const auto ttt = getTTTBits(id16);
        return ttt;
    }

    static const std::array<uint16_t, 80> GetImageLine(const uint8_t* packet) {
        std::array<uint16_t, 80> line{};
        assert(packet != nullptr);
        const uint8_t* dataBegin = packet + 4; // skip ID and CRC

        for (std::size_t i = 0; i < line.size(); ++i) {
            const std::size_t off = 2 * i;
            line[i] = (static_cast<uint16_t>(dataBegin[off]) << 8)
                    | static_cast<uint16_t>(dataBegin[off + 1]);
        }

        return line;
    }

    static inline uint16_t computeCRC(const uint8_t* packet, size_t size) {
        assert(size >= 4); // at least ID + CRC present

        std::vector<uint8_t> tmp(packet, packet + size);
        tmp[0] &= 0x0F;   // clear four MSB of ID for CRC calculation
        tmp[2] = 0;       // treat CRC bytes as zero
        tmp[3] = 0;

        uint16_t crc = 0x0000; // use 0x0000 initial value
        for (size_t i = 0; i < size; ++i) {
            crc ^= static_cast<uint16_t>(tmp[i]) << 8;
            for (int b = 0; b < 8; ++b) {
                if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
                else crc <<= 1;
            }
        }
        return crc;
    }


}
