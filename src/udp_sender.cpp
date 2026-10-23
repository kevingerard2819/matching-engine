#include "feed_protocol.hpp"
#include "linux_udp.hpp"
#include "order_io.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace matching;
    try {
        if (argc < 5 || argc > 6)
            throw std::invalid_argument("usage: udp_sender FILE IPV4 PORT SYMBOL [INTERVAL_US=100]");
        const auto commands = load_commands(argv[1]);
        const auto destination = address(argv[2], static_cast<std::uint16_t>(unsigned_argument(argv[3], 65535)));
        const auto symbol = static_cast<std::uint32_t>(unsigned_argument(argv[4], UINT32_MAX));
        const auto delay = argc == 6 ? unsigned_argument(argv[5], 10000000, true) : 100;
        UdpSocket socket;
        std::uint64_t sequence = 1;
        for (const auto& command : commands) {
            const auto packet = encode_packet({symbol, sequence++, command});
            ssize_t sent;
            do {
                sent = ::sendto(socket.fd(), packet.data(), packet.size(), 0,
                                reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
            } while (sent < 0 && errno == EINTR);
            if (sent != static_cast<ssize_t>(packet.size())) throw socket_error("sendto");
            if (delay) std::this_thread::sleep_for(std::chrono::microseconds(delay));
        }
        std::cout << "sent=" << commands.size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n'; return 1;
    }
}
