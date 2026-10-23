#include "feed_protocol.hpp"
#include <limits>
#include <stdexcept>

namespace matching {
namespace {
void put(Packet& packet, std::size_t offset, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
        packet[offset + bytes - 1 - i] = static_cast<std::uint8_t>(value & 255);
        value >>= 8;
    }
}
std::uint64_t get(const std::uint8_t* data, std::size_t offset, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value = (value << 8) | data[offset + i];
    return value;
}
}
Packet encode_packet(const FeedMessage& message) {
    Packet packet{};
    packet[0] = 'M'; packet[1] = 'E'; packet[2] = '0'; packet[3] = '1';
    put(packet, 4, message.symbol, 4);
    put(packet, 8, message.sequence, 8);
    if (const auto* add = std::get_if<LimitOrder>(&message.command)) {
        if (add->side != Side::BUY && add->side != Side::SELL)
            throw std::invalid_argument("invalid side");
        packet[16] = 1;
        packet[17] = add->side == Side::BUY ? 1 : 2;
        put(packet, 24, add->id, 8);
        put(packet, 32, static_cast<std::uint64_t>(add->price), 8);
        put(packet, 40, add->quantity, 8);
    } else {
        packet[16] = 2;
        put(packet, 24, std::get<CancelOrder>(message.command).id, 8);
    }
    (void)decode_packet(packet.data(), packet.size());
    return packet;
}
FeedMessage decode_packet(const std::uint8_t* data, std::size_t size) {
    if (size != packet_size || !data) throw std::invalid_argument("packet must be exactly 48 bytes");
    if (data[0] != 'M' || data[1] != 'E' || data[2] != '0' || data[3] != '1')
        throw std::invalid_argument("unknown protocol version");
    for (std::size_t i = 18; i < 24; ++i)
        if (data[i]) throw std::invalid_argument("nonzero reserved field");
    const auto symbol = static_cast<std::uint32_t>(get(data, 4, 4));
    const auto sequence = get(data, 8, 8), id = get(data, 24, 8);
    const auto price = get(data, 32, 8), quantity = get(data, 40, 8);
    if (!symbol || !sequence || sequence == std::numeric_limits<std::uint64_t>::max() || !id)
        throw std::invalid_argument("invalid symbol, sequence, or ID");
    if (data[16] == 1) {
        if ((data[17] != 1 && data[17] != 2) || !price || !quantity ||
            price > static_cast<std::uint64_t>(std::numeric_limits<Price>::max()))
            throw std::invalid_argument("invalid add fields");
        return {symbol, sequence, LimitOrder{id, data[17] == 1 ? Side::BUY : Side::SELL,
                                            static_cast<Price>(price), quantity}};
    }
    if (data[16] != 2 || data[17] || price || quantity)
        throw std::invalid_argument("invalid cancel fields");
    return {symbol, sequence, CancelOrder{id}};
}
} // namespace matching
