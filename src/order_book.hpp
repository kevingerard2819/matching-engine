#pragma once

#include "order.hpp"
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace matching {
class OrderBook {
public:
    OrderBook() = default;
    // Index entries contain iterators into the price levels.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    // Throws invalid_argument for invalid values or a duplicate live ID.
    // Returns this order's executions and retains them in trades().
    std::vector<Trade> add(const LimitOrder& request);
    bool cancel(OrderId id);
    std::optional<Order> find(OrderId id) const;
    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    std::vector<Order> orders(Side side) const;
    const std::vector<Trade>& trades() const noexcept { return trades_; }
    std::size_t size() const noexcept { return index_.size(); }
    // Optional allocation experiment, called before processing begins.
    void reserve(std::size_t live_orders, std::size_t trades) {
        index_.reserve(live_orders);
        trades_.reserve(trades);
    }

private:
    using Level = std::list<Order>;
    using Bids = std::map<Price, Level, std::greater<Price>>;
    using Asks = std::map<Price, Level>;
    struct Location { Side side; Price price; Level::iterator order; };

    template <typename Levels>
    void match(Order& incoming, Levels& opposite, std::vector<Trade>& executions);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Location> index_;
    std::vector<Trade> trades_;
    Sequence arrival_sequence_ = 0;
    Sequence trade_sequence_ = 0;
};
}  // namespace matching
