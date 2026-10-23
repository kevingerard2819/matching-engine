#include "order_io.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: replay FILE");
        const auto commands = matching::load_commands(argv[1]);
        matching::MatchingEngine engine;
        std::size_t cancelled = 0;
        for (std::size_t i = 0; i < commands.size(); ++i) {
            matching::ProcessResult result;
            try { result = engine.process(commands[i]); }
            catch (const std::exception& error) {
                throw std::runtime_error("command " + std::to_string(i + 1) + ": " + error.what());
            }
            cancelled += result.cancelled;
            for (const auto& trade : result.trades)
                std::cout << "TRADE " << trade.sequence << " maker=" << trade.maker_id
                          << " taker=" << trade.taker_id << " price=" << trade.price
                          << " quantity=" << trade.quantity << '\n';
        }
        std::cout << "commands=" << commands.size() << " trades=" << engine.book().trades().size()
                  << " resting_orders=" << engine.book().size() << " cancelled=" << cancelled << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
