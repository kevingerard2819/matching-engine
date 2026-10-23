#include "bounded_queue.hpp"
#include "feed_protocol.hpp"
#include "linux_udp.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <thread>

namespace {
static_assert(std::atomic<bool>::is_always_lock_free, "signal flag must be lock-free");
std::atomic<bool> stopped{false};
void stop(int) { stopped.store(true, std::memory_order_relaxed); }
}

int main(int argc, char** argv) {
    using namespace matching;
    try {
        if (argc < 3 || argc > 6)
            throw std::invalid_argument("usage: udp_feed PORT SYMBOL [CAPACITY=4096] [IDLE_MS=2000] [TRADES_FILE]");
        const auto port = static_cast<std::uint16_t>(unsigned_argument(argv[1], 65535, true));
        const auto symbol = static_cast<std::uint32_t>(unsigned_argument(argv[2], UINT32_MAX));
        const auto capacity = argc >= 4 ? unsigned_argument(argv[3], 10000000) : 4096;
        const auto idle_ms = argc >= 5 ? unsigned_argument(argv[4], 3600000) : 2000;
        std::ofstream output;
        if (argc == 6) { output.open(argv[5]); if (!output) throw std::runtime_error("cannot open trades file"); }
        UdpSocket socket;
        // Local experiments only; one publisher per process/session.
        auto local = address("127.0.0.1", port);
        if (::bind(socket.fd(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
            throw socket_error("bind");
        socklen_t length = sizeof(local);
        if (::getsockname(socket.fd(), reinterpret_cast<sockaddr*>(&local), &length) < 0)
            throw socket_error("getsockname");
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        BoundedQueue<Command> queue(static_cast<std::size_t>(capacity));
        MatchingEngine engine;
        std::exception_ptr consumer_error;
        std::atomic<bool> consumer_failed{false};
        std::uint64_t processed = 0, rejected = 0, cancelled = 0;
        std::thread consumer([&] {
            try {
                while (auto command = queue.pop()) {
                    try { cancelled += engine.process(*command).cancelled; ++processed; }
                    catch (const std::invalid_argument&) { ++rejected; }
                }
            } catch (...) {
                consumer_error = std::current_exception();
                consumer_failed.store(true);
                queue.close();
            }
        });
        std::uint64_t received = 0, malformed = 0, duplicates = 0, foreign = 0, accepted = 0;
        bool gap = false, overflow = false;
        std::exception_ptr receiver_error;
        try {
            std::cout << "READY port=" << ntohs(local.sin_port) << std::endl;
            SequenceGate gate;
            auto last = std::chrono::steady_clock::now();
            sockaddr_in publisher{};
            bool have_publisher = false;
            while (!stopped.load(std::memory_order_relaxed) && !consumer_failed.load()) {
                if (std::chrono::steady_clock::now() - last >= std::chrono::milliseconds(idle_ms)) break;
                pollfd descriptor{socket.fd(), POLLIN, 0};
                const int ready = ::poll(&descriptor, 1, 50);
                if (ready < 0) { if (errno == EINTR) continue; throw socket_error("poll"); }
                if (!ready) continue;
                if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("UDP poll error");
                Packet packet{};
                sockaddr_in source{};
                socklen_t source_size = sizeof(source);
                const auto bytes = ::recvfrom(socket.fd(), packet.data(), packet.size(), MSG_TRUNC | MSG_DONTWAIT,
                    reinterpret_cast<sockaddr*>(&source), &source_size);
                if (bytes < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    throw socket_error("recvfrom");
                }
                ++received;
                FeedMessage message{};
                try { message = decode_packet(packet.data(), static_cast<std::size_t>(bytes)); }
                catch (const std::invalid_argument&) { ++malformed; continue; }
                if (message.symbol != symbol || (have_publisher &&
                    (publisher.sin_addr.s_addr != source.sin_addr.s_addr || publisher.sin_port != source.sin_port))) {
                    ++foreign; continue;
                }
                const auto decision = gate.check(message.sequence);
                if (decision == SequenceDecision::DUPLICATE) { ++duplicates; continue; }
                if (decision == SequenceDecision::GAP) { gap = true; break; }
                if (!queue.try_push(message.command)) { overflow = !consumer_failed.load(); break; }
                publisher = source; have_publisher = true;
                gate.commit(); ++accepted;
                last = std::chrono::steady_clock::now();
            }
        } catch (...) { receiver_error = std::current_exception(); }
        queue.close();
        consumer.join();
        if (consumer_error) std::rethrow_exception(consumer_error);
        if (receiver_error) std::rethrow_exception(receiver_error);
        if (output.is_open()) {
            for (const auto& trade : engine.book().trades())
                output << "TRADE " << trade.sequence << " maker=" << trade.maker_id << " taker=" << trade.taker_id
                       << " price=" << trade.price << " quantity=" << trade.quantity << '\n';
            output.close();
            if (!output) throw std::runtime_error("failed writing trades file");
        }
        std::cout << "received=" << received << " accepted=" << accepted << " processed=" << processed
                  << " rejected=" << rejected << " cancelled=" << cancelled << " malformed=" << malformed
                  << " duplicates=" << duplicates << " foreign=" << foreign << " gap=" << gap
                  << " overflow=" << overflow << " trades=" << engine.book().trades().size()
                  << " resting_orders=" << engine.book().size() << '\n';
        return gap || overflow || rejected ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n'; return 1;
    }
}
