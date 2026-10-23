#pragma once

#include "order_book.hpp"
#include <variant>

namespace matching {
struct CancelOrder { OrderId id; };
using Command = std::variant<LimitOrder, CancelOrder>;
struct ProcessResult {
    std::vector<Trade> trades;
    bool cancelled = false;
};

// Single owner, single symbol, single thread. No synchronization required.
class MatchingEngine {
public:
    ProcessResult process(const Command& command);
    const OrderBook& book() const noexcept { return book_; }
    void reserve(std::size_t live_orders, std::size_t trades) { book_.reserve(live_orders, trades); }
private:
    OrderBook book_;
};
}  // namespace matching
