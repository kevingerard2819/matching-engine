#include "order_io.hpp"
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace matching {
namespace {
template <typename T> T positive(const std::string& token) {
    T value{};
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value == 0)
        throw std::runtime_error("expected positive integer: " + token);
    return value;
}
}
std::vector<Command> read_commands(std::istream& input) {
    std::vector<Command> commands;
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
        std::istringstream row(line);
        std::vector<std::string> fields;
        for (std::string field; row >> field;) fields.push_back(field);
        if (fields.empty()) continue;
        try {
            if (fields[0] == "ADD" && fields.size() == 5) {
                if (fields[2] != "BUY" && fields[2] != "SELL") throw std::runtime_error("invalid side");
                commands.emplace_back(LimitOrder{positive<OrderId>(fields[1]),
                    fields[2] == "BUY" ? Side::BUY : Side::SELL,
                    positive<Price>(fields[3]), positive<Quantity>(fields[4])});
                const auto& order = std::get<LimitOrder>(commands.back());
                if (order.price <= 0) throw std::runtime_error("price must be positive");
            } else if (fields[0] == "CANCEL" && fields.size() == 2) {
                commands.emplace_back(CancelOrder{positive<OrderId>(fields[1])});
            } else throw std::runtime_error("expected ADD id BUY|SELL price quantity or CANCEL id");
        } catch (const std::exception& error) {
            throw std::runtime_error("line " + std::to_string(number) + ": " + error.what());
        }
    }
    if (input.bad()) throw std::runtime_error("failed reading order stream");
    return commands;
}
std::vector<Command> load_commands(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return read_commands(input);
}
}  // namespace matching
