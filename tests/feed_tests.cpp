#include "bounded_queue.hpp"
#include "feed_protocol.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace matching;
#define CHECK(c) do { if (!(c)) throw std::runtime_error("failed: " #c); } while (false)

template <typename F> void invalid(F function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}

void protocol() {
    const auto packet = encode_packet({7, 1, LimitOrder{0x0102030405060708ULL, Side::BUY, 1005, 40}});
    CHECK(packet[4] == 0 && packet[7] == 7 && packet[15] == 1);
    CHECK(packet[24] == 1 && packet[31] == 8 && packet[38] == 3 && packet[39] == 237);
    const auto decoded = decode_packet(packet.data(), packet.size());
    const auto order = std::get<LimitOrder>(decoded.command);
    CHECK(decoded.symbol == 7 && decoded.sequence == 1 && order.price == 1005 && order.quantity == 40);
    CHECK(order.id == 0x0102030405060708ULL && order.side == Side::BUY);
    auto sell = decode_packet(encode_packet({7, 2, LimitOrder{1, Side::SELL, 1, UINT64_MAX}}).data(), packet_size);
    CHECK(std::get<LimitOrder>(sell.command).side == Side::SELL);
    CHECK(std::get<LimitOrder>(sell.command).quantity == UINT64_MAX);
    const auto cancel = encode_packet({7, 3, CancelOrder{99}});
    CHECK(std::get<CancelOrder>(decode_packet(cancel.data(), cancel.size()).command).id == 99);
    for (const auto offset : {0, 16, 17, 18, 23}) {
        auto bad = packet; bad[static_cast<std::size_t>(offset)] = 99;
        invalid([&] { decode_packet(bad.data(), bad.size()); });
    }
    for (const auto offset : {17, 32, 40}) {
        auto bad = cancel; bad[static_cast<std::size_t>(offset)] = 1;
        invalid([&] { decode_packet(bad.data(), bad.size()); });
    }
    invalid([&] { decode_packet(packet.data(), 47); });
    invalid([&] { decode_packet(packet.data(), 49); });
    invalid([&] { decode_packet(nullptr, 48); });
    invalid([&] { encode_packet({0, 1, CancelOrder{1}}); });
    invalid([&] { encode_packet({1, 0, CancelOrder{1}}); });
    invalid([&] { encode_packet({1, UINT64_MAX, CancelOrder{1}}); });
    invalid([&] { encode_packet({1, 1, CancelOrder{0}}); });
    invalid([&] { encode_packet({1, 1, LimitOrder{1, Side::BUY, -1, 1}}); });
    invalid([&] { encode_packet({1, 1, LimitOrder{1, Side::BUY, 1, 0}}); });
    SequenceGate gate;
    CHECK(gate.check(2) == SequenceDecision::GAP);
    CHECK(gate.check(1) == SequenceDecision::ACCEPT);
    gate.commit();
    CHECK(gate.check(1) == SequenceDecision::DUPLICATE);
    CHECK(gate.check(2) == SequenceDecision::ACCEPT);
}

void queues() {
    invalid([] { BoundedQueue<int> queue(0); });
    BoundedQueue<int> queue(2);
    CHECK(queue.try_push(1) && queue.try_push(2) && !queue.try_push(3));
    CHECK(queue.pop() == 1 && queue.try_push(3));
    queue.close(); queue.close();
    CHECK(!queue.try_push(4) && !queue.push(4));
    CHECK(queue.pop() == 2 && queue.pop() == 3 && !queue.pop());

    BoundedQueue<int> empty(1);
    auto reader = std::async(std::launch::async, [&] { return empty.pop(); });
    empty.close();
    CHECK(!reader.get());
    BoundedQueue<int> full(1);
    CHECK(full.push(1));
    auto writer = std::async(std::launch::async, [&] { return full.push(2); });
    full.close();
    CHECK(!writer.get() && full.pop() == 1 && !full.pop());

    for (const auto capacity : {1U, 3U, 1024U}) {
        BoundedQueue<unsigned> stress(capacity);
        std::atomic<bool> ordered{true};
        std::thread consumer([&] {
            unsigned expected = 0;
            while (auto value = stress.pop()) if (*value != expected++) ordered = false;
            if (expected != 100000) ordered = false;
        });
        for (unsigned i = 0; i < 100000; ++i) stress.push(i);
        stress.close(); consumer.join();
        CHECK(ordered.load());
    }
}
int main() {
    try { protocol(); queues(); std::cout << "PASS protocol, sequencing, bounded queue, shutdown, concurrent FIFO\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
