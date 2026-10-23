#include "order_book.hpp"
#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace matching {
template <typename Levels>
void OrderBook::match(Order& incoming, Levels& opposite, std::vector<Trade>& executions) {
    while (incoming.quantity && !opposite.empty()) {
        auto level = opposite.begin();
        const Price price = level->first;
        if ((incoming.side == Side::BUY && incoming.price < price) ||
            (incoming.side == Side::SELL && incoming.price > price)) break;

        auto& resting = level->second.front();
        const Quantity quantity = std::min(incoming.quantity, resting.quantity);
        if (trade_sequence_ == std::numeric_limits<Sequence>::max())
            throw std::overflow_error("trade sequence exhausted");
        Trade trade{++trade_sequence_, resting.id, incoming.id,
                    incoming.side == Side::BUY ? incoming.id : resting.id,
                    incoming.side == Side::SELL ? incoming.id : resting.id,
                    resting.price, quantity};
        trades_.push_back(trade);
        executions.push_back(trade);
        incoming.quantity -= quantity;
        resting.quantity -= quantity;
        if (resting.quantity == 0) {
            index_.erase(resting.id);
            level->second.pop_front();
            if (level->second.empty()) opposite.erase(level);
        }
    }
}

std::vector<Trade> OrderBook::add(const LimitOrder& request) {
    if (request.id == 0 || request.price <= 0 || request.quantity == 0 ||
        (request.side != Side::BUY && request.side != Side::SELL))
        throw std::invalid_argument("ID, price, and quantity must be positive; side must be BUY or SELL");
    if (index_.count(request.id)) throw std::invalid_argument("duplicate live order ID");
    if (arrival_sequence_ == std::numeric_limits<Sequence>::max())
        throw std::overflow_error("arrival sequence exhausted");

    Order incoming{request.id, request.side, request.price, request.quantity, ++arrival_sequence_};
    std::vector<Trade> executions;
    if (incoming.side == Side::BUY) match(incoming, asks_, executions);
    else match(incoming, bids_, executions);

    if (incoming.quantity) {
        auto& level = incoming.side == Side::BUY ? bids_[incoming.price] : asks_[incoming.price];
        level.push_back(incoming);
        index_.emplace(incoming.id, Location{incoming.side, incoming.price, std::prev(level.end())});
    }
    return executions;
}

bool OrderBook::cancel(OrderId id) {
    const auto entry = index_.find(id);
    if (entry == index_.end()) return false;
    const auto location = entry->second;
    if (location.side == Side::BUY) {
        auto level = bids_.find(location.price);
        level->second.erase(location.order);
        if (level->second.empty()) bids_.erase(level);
    } else {
        auto level = asks_.find(location.price);
        level->second.erase(location.order);
        if (level->second.empty()) asks_.erase(level);
    }
    index_.erase(entry);
    return true;
}

std::optional<Order> OrderBook::find(OrderId id) const {
    const auto entry = index_.find(id);
    if (entry == index_.end()) return std::nullopt;
    return *entry->second.order;
}
std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}
std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}
std::vector<Order> OrderBook::orders(Side side) const {
    std::vector<Order> result;
    if (side == Side::BUY) {
        for (const auto& level : bids_) result.insert(result.end(), level.second.begin(), level.second.end());
    } else {
        for (const auto& level : asks_) result.insert(result.end(), level.second.begin(), level.second.end());
    }
    return result;
}
}  // namespace matching
