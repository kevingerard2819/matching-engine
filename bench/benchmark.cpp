#include "bounded_queue.hpp"
#include "order_io.hpp"
#include <charconv>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
std::size_t number(const char* text) {
    const std::string token(text);
    std::size_t value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !value)
        throw std::invalid_argument("expected positive integer");
    return value;
}
// Stable byte-wise digest covers every trade and final resting order, outside timing.
std::uint64_t digest(const matching::OrderBook& book) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) { hash ^= value & 255; hash *= 1099511628211ULL; value >>= 8; }
    };
    add(book.trades().size());
    for (const auto& trade : book.trades()) {
        add(trade.sequence); add(trade.maker_id); add(trade.taker_id); add(trade.buy_id);
        add(trade.sell_id); add(static_cast<std::uint64_t>(trade.price)); add(trade.quantity);
    }
    add(book.size());
    for (auto side : {matching::Side::BUY, matching::Side::SELL}) {
        add(side == matching::Side::BUY ? 1 : 2);
        for (const auto& order : book.orders(side)) {
            add(order.id); add(static_cast<std::uint64_t>(order.price));
            add(order.quantity); add(order.arrival_sequence);
        }
    }
    return hash;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 5)
            throw std::runtime_error("usage: benchmark FILE [direct|queued] [CAPACITY=4096] [RESERVE_COUNT]");
        const std::string mode = argc >= 3 ? argv[2] : "direct";
        if (mode != "direct" && mode != "queued") throw std::invalid_argument("invalid benchmark mode");
        const auto capacity = argc >= 4 ? number(argv[3]) : 4096;
        const auto commands = matching::load_commands(argv[1]);
        if (commands.empty()) throw std::runtime_error("benchmark input is empty");
        matching::MatchingEngine engine;
        if (argc == 5) { const auto count = number(argv[4]); engine.reserve(count, count); }
        matching::BoundedQueue<matching::Command> queue(capacity);
        const auto start = std::chrono::steady_clock::now();
        if (mode == "direct") {
            for (const auto& command : commands) engine.process(command);
        } else {
            std::exception_ptr failure, producer_failure;
            std::thread consumer([&] {
                try { while (auto command = queue.pop()) engine.process(*command); }
                catch (...) { failure = std::current_exception(); queue.close(); }
            });
            try { for (const auto& command : commands) if (!queue.push(command)) break; }
            catch (...) { producer_failure = std::current_exception(); }
            queue.close();
            consumer.join();
            if (failure) std::rethrow_exception(failure);
            if (producer_failure) std::rethrow_exception(producer_failure);
        }
        const auto stop = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(stop - start).count();
        if (seconds <= 0) throw std::runtime_error("timer resolution too low for workload");
        std::cout << std::fixed << std::setprecision(3)
                  << "commands=" << commands.size() << " trades=" << engine.book().trades().size()
                  << " resting_orders=" << engine.book().size() << " digest=" << digest(engine.book()) << '\n'
                  << "mode=" << mode << " capacity=" << capacity << " reserved=" << (argc == 5) << '\n'
                  << "processing_ms=" << seconds * 1000.0 << '\n'
                  << "commands_per_second=" << static_cast<double>(commands.size()) / seconds << '\n'
                  << "mean_ns_per_command=" << seconds * 1e9 / static_cast<double>(commands.size()) << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
