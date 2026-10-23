#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {
std::uint64_t number(const char* argument) {
    const std::string text(argument);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("invalid unsigned integer: " + text);
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 4) throw std::runtime_error("usage: generate_orders FILE COUNT [SEED]");
        const auto count = number(argv[2]);
        if (count == 0 || count == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("COUNT must be between 1 and UINT64_MAX-1");
        std::mt19937_64 random(argc == 4 ? number(argv[3]) : 42);
        std::ofstream output(argv[1]);
        if (!output) throw std::runtime_error("cannot open output file");
        std::uint64_t next_id = 1;
        for (std::uint64_t i = 0; i < count; ++i) {
            if (next_id > 1 && random() % 10 == 0) {
                output << "CANCEL " << 1 + random() % (next_id - 1) << '\n';
            } else {
                output << "ADD " << next_id++ << (random() % 2 ? " BUY " : " SELL ")
                       << 990 + random() % 21 << ' ' << 1 + random() % 100 << '\n';
            }
        }
        output.close();
        if (!output) throw std::runtime_error("failed writing output file");
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
