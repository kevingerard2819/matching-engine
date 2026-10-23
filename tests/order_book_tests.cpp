#include "order_io.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace matching;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string("check failed at line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

template <typename Exception, typename Function> void throws(Function function) {
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}

void full_fill() {
    OrderBook book;
    book.add({1, Side::SELL, 1000, 100});
    const auto trades = book.add({2, Side::BUY, 1005, 100});
    CHECK(trades.size() == 1);
    CHECK(trades[0].quantity == 100 && trades[0].price == 1000);
    CHECK(trades[0].maker_id == 1 && trades[0].taker_id == 2);
    CHECK(trades[0].buy_id == 2 && trades[0].sell_id == 1);
    CHECK(book.size() == 0 && !book.best_bid() && !book.best_ask());
    CHECK(book.trades().size() == 1 && book.trades()[0].sequence == 1);
}

void partial_fills() {
    OrderBook book;
    book.add({1, Side::SELL, 1000, 100});
    book.add({2, Side::BUY, 1005, 40});
    CHECK(book.find(1)->quantity == 60 && !book.find(2));
    CHECK(book.find(1)->arrival_sequence == 1);
    book.add({3, Side::BUY, 1005, 80});
    CHECK(!book.find(1) && book.find(3)->quantity == 20);
    CHECK(book.best_bid() == 1005 && !book.best_ask());
    CHECK(book.trades()[1].quantity == 60);
}

void price_priority() {
    OrderBook book;
    book.add({1, Side::SELL, 1010, 10});
    book.add({2, Side::SELL, 1000, 10});
    auto trades = book.add({3, Side::BUY, 1020, 20});
    CHECK(trades.size() == 2 && trades[0].maker_id == 2 && trades[1].maker_id == 1);
    CHECK(trades[0].price == 1000 && trades[1].price == 1010);
    book.add({4, Side::BUY, 1000, 10});
    book.add({5, Side::BUY, 1010, 10});
    trades = book.add({6, Side::SELL, 990, 20});
    CHECK(trades.size() == 2 && trades[0].maker_id == 5 && trades[1].maker_id == 4);
    CHECK(trades[0].price == 1010 && trades[1].price == 1000);
}

void time_priority() {
    for (const auto side : {Side::BUY, Side::SELL}) {
        OrderBook book;
        book.add({90, side, 1000, 10});
        book.add({2, side, 1000, 10});
        const auto opposite = side == Side::BUY ? Side::SELL : Side::BUY;
        book.add({3, opposite, 1000, 5});
        const auto trades = book.add({4, opposite, 1000, 10});
        CHECK(trades.size() == 2 && trades[0].maker_id == 90 && trades[1].maker_id == 2);
        CHECK(trades[0].quantity == 5 && trades[1].quantity == 5);
        CHECK(book.find(2)->quantity == 5);
    }
}

void cancellations() {
    for (const auto side : {Side::BUY, Side::SELL}) {
        OrderBook book;
        book.add({1, side, 1000, 10});
        book.add({2, side, 1000, 10});
        book.add({3, side, 1000, 10});
        CHECK(book.cancel(2) && !book.cancel(2) && !book.cancel(999));
        const auto trades = book.add({4, side == Side::BUY ? Side::SELL : Side::BUY, 1000, 15});
        CHECK(trades.size() == 2 && trades[0].maker_id == 1 && trades[1].maker_id == 3);
        CHECK(book.cancel(3) && book.size() == 0);
        CHECK(!book.best_bid() && !book.best_ask());
    }
}

void non_crossing_and_limit() {
    OrderBook book;
    book.add({1, Side::BUY, 990, 10});
    book.add({2, Side::SELL, 1010, 10});
    book.add({3, Side::SELL, 1020, 10});
    CHECK(book.trades().empty() && book.size() == 3);
    book.add({4, Side::BUY, 1010, 15});
    CHECK(book.trades().size() == 1 && book.find(4)->quantity == 5);
    CHECK(book.best_bid() == 1010 && book.best_ask() == 1020);
    CHECK(book.orders(Side::BUY)[0].id == 4);
    CHECK(book.orders(Side::BUY)[1].id == 1);
}

void validation_and_id_reuse() {
    OrderBook book;
    for (const auto order : std::vector<LimitOrder>{{0, Side::BUY, 1, 1}, {1, Side::BUY, 0, 1},
             {1, Side::BUY, -1, 1}, {1, Side::BUY, 1, 0}, {1, static_cast<Side>(9), 1, 1}})
        throws<std::invalid_argument>([&] { book.add(order); });
    book.add({1, Side::BUY, 1000, 10});
    throws<std::invalid_argument>([&] { book.add({1, Side::SELL, 1000, 10}); });
    CHECK(book.size() == 1 && book.trades().empty() && book.find(1)->quantity == 10);
    CHECK(book.cancel(1));
    book.add({1, Side::SELL, 1000, 10});
    CHECK(book.find(1)->arrival_sequence == 2);
    book.add({2, Side::BUY, 1000, 10});
    book.add({2, Side::BUY, 1000, 1});
    CHECK(book.find(2)->arrival_sequence == 4);
}

void integer_boundaries() {
    OrderBook book;
    const auto price = std::numeric_limits<Price>::max();
    const auto quantity = std::numeric_limits<Quantity>::max();
    const auto id = std::numeric_limits<OrderId>::max();
    book.add({id, Side::BUY, price, quantity});
    const auto trades = book.add({1, Side::SELL, price, quantity});
    CHECK(trades.size() == 1 && trades[0].quantity == quantity && book.size() == 0);
}

void command_parser() {
    std::istringstream input("# comment\n\nADD 1 SELL 1000 100 # rest\nADD 2 BUY 1005 40\nCANCEL 1\n");
    const auto commands = read_commands(input);
    CHECK(commands.size() == 3);
    MatchingEngine engine;
    CHECK(engine.process(commands[0]).trades.empty());
    CHECK(engine.process(commands[1]).trades[0].quantity == 40);
    CHECK(engine.process(commands[2]).cancelled);
    CHECK(!engine.process(CancelOrder{1}).cancelled && engine.book().size() == 0);
    for (const auto* bad : {"ADD 1 BID 10 1", "ADD -1 BUY 10 1", "ADD 1 BUY -10 1",
             "ADD 1 BUY 10 0", "ADD 1 BUY 10 1 extra", "CANCEL 1 extra", "UNKNOWN 1",
             "ADD 1 BUY 9223372036854775808 1", "CANCEL 18446744073709551616", "CANCEL 2x"}) {
        std::istringstream stream(std::string("# header\n") + bad);
        try { read_commands(stream); throw std::logic_error("accepted invalid command"); }
        catch (const std::runtime_error& error) { CHECK(std::string(error.what()).find("line 2:") == 0); }
    }
}

// Independent, deliberately slow reference: scan all live orders for each fill.
void randomized_reference() {
    OrderBook book;
    std::vector<Order> reference;
    std::mt19937_64 random(12345);
    Sequence arrival = 0, trade_sequence = 0;
    for (OrderId id = 1; id <= 5000; ++id) {
        if (!reference.empty() && random() % 4 == 0) {
            const auto offset = static_cast<std::size_t>(random() % reference.size());
            CHECK(book.cancel(reference[offset].id));
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(offset));
        } else {
            LimitOrder request{id, random() % 2 ? Side::BUY : Side::SELL,
                               static_cast<Price>(990 + random() % 21), 1 + random() % 100};
            Order incoming{request.id, request.side, request.price, request.quantity, ++arrival};
            std::vector<Trade> expected;
            while (incoming.quantity) {
                auto best = reference.end();
                for (auto it = reference.begin(); it != reference.end(); ++it) {
                    if (it->side == incoming.side) continue;
                    if (incoming.side == Side::BUY ? it->price > incoming.price : it->price < incoming.price) continue;
                    if (best == reference.end() ||
                        (incoming.side == Side::BUY ? it->price < best->price : it->price > best->price) ||
                        (it->price == best->price && it->arrival_sequence < best->arrival_sequence)) best = it;
                }
                if (best == reference.end()) break;
                const auto quantity = std::min(incoming.quantity, best->quantity);
                expected.push_back({++trade_sequence, best->id, incoming.id,
                    incoming.side == Side::BUY ? incoming.id : best->id,
                    incoming.side == Side::SELL ? incoming.id : best->id, best->price, quantity});
                incoming.quantity -= quantity;
                best->quantity -= quantity;
                if (!best->quantity) reference.erase(best);
            }
            if (incoming.quantity) reference.push_back(incoming);
            const auto actual = book.add(request);
            CHECK(actual.size() == expected.size());
            for (std::size_t i = 0; i < actual.size(); ++i) {
                CHECK(actual[i].sequence == expected[i].sequence);
                CHECK(actual[i].maker_id == expected[i].maker_id && actual[i].taker_id == expected[i].taker_id);
                CHECK(actual[i].buy_id == expected[i].buy_id && actual[i].sell_id == expected[i].sell_id);
                CHECK(actual[i].price == expected[i].price && actual[i].quantity == expected[i].quantity);
            }
        }
        CHECK(book.size() == reference.size());
        CHECK(book.trades().size() == trade_sequence);
        for (const auto side : {Side::BUY, Side::SELL}) {
            std::vector<Order> expected;
            for (const auto& order : reference) if (order.side == side) expected.push_back(order);
            std::sort(expected.begin(), expected.end(), [side](const Order& a, const Order& b) {
                if (a.price != b.price) return side == Side::BUY ? a.price > b.price : a.price < b.price;
                return a.arrival_sequence < b.arrival_sequence;
            });
            const auto actual = book.orders(side);
            CHECK(actual.size() == expected.size());
            for (std::size_t i = 0; i < actual.size(); ++i) {
                CHECK(actual[i].id == expected[i].id && actual[i].quantity == expected[i].quantity);
                CHECK(actual[i].price == expected[i].price && actual[i].arrival_sequence == expected[i].arrival_sequence);
            }
            const auto best = side == Side::BUY ? book.best_bid() : book.best_ask();
            CHECK(expected.empty() ? !best : best == expected.front().price);
        }
    }
}

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"full fill", full_fill}, {"partial fills", partial_fills}, {"price priority", price_priority},
        {"time priority", time_priority}, {"cancellations", cancellations},
        {"non-crossing and limit", non_crossing_and_limit}, {"validation and ID reuse", validation_and_id_reuse},
        {"integer boundaries", integer_boundaries}, {"command parser", command_parser},
        {"randomized reference", randomized_reference}};
    unsigned failures = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
