#pragma once
#include "matching_engine.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace matching {
constexpr std::size_t packet_size = 48;
using Packet = std::array<std::uint8_t, packet_size>;
struct FeedMessage { std::uint32_t symbol; std::uint64_t sequence; Command command; };
Packet encode_packet(const FeedMessage& message);
FeedMessage decode_packet(const std::uint8_t* data, std::size_t size);
enum class SequenceDecision { ACCEPT, DUPLICATE, GAP };
class SequenceGate {
public:
    SequenceDecision check(std::uint64_t sequence) const {
        return sequence < expected_ ? SequenceDecision::DUPLICATE :
               sequence == expected_ ? SequenceDecision::ACCEPT : SequenceDecision::GAP;
    }
    void commit() { ++expected_; }
private:
    std::uint64_t expected_ = 1;
};
} // namespace matching
