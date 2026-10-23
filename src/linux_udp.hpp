#pragma once
#include <arpa/inet.h>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace matching {
inline std::uint64_t unsigned_argument(const char* text, std::uint64_t maximum, bool zero = false) {
    const std::string token(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
        (!zero && value == 0) || value > maximum) throw std::invalid_argument("invalid argument: " + token);
    return value;
}
inline std::runtime_error socket_error(const char* action) {
    return std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}
class UdpSocket {
public:
    UdpSocket() : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) {
        if (fd_ < 0) throw socket_error("socket");
    }
    ~UdpSocket() { ::close(fd_); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    int fd() const { return fd_; }
private:
    int fd_;
};
inline sockaddr_in address(const char* host, std::uint16_t port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &result.sin_addr) != 1) throw std::invalid_argument("expected IPv4 address");
    return result;
}
} // namespace matching
