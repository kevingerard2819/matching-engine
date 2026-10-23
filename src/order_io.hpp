#pragma once
#include "matching_engine.hpp"
#include <iosfwd>
#include <vector>

namespace matching {
// ADD id BUY|SELL price_ticks quantity / CANCEL id; # introduces comments.
// Invalid lines throw runtime_error with their line number.
std::vector<Command> read_commands(std::istream& input);
std::vector<Command> load_commands(const char* path);
}  // namespace matching
