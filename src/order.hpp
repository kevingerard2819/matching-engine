#pragma once

#include <cstdint>

namespace matching {
using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;
using Sequence = std::uint64_t;

enum class Side { BUY, SELL };

struct LimitOrder {
    OrderId id;
    Side side;
    Price price;  // Integer ticks, never floating point.
    Quantity quantity;
};

struct Order {
    OrderId id;
    Side side;
    Price price;
    Quantity quantity;  // Remaining quantity.
    Sequence arrival_sequence;
};

struct Trade {
    Sequence sequence;
    OrderId maker_id;
    OrderId taker_id;
    OrderId buy_id;
    OrderId sell_id;
    Price price;
    Quantity quantity;
};
}  // namespace matching
