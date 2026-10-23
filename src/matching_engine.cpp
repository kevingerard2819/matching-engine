#include "matching_engine.hpp"

namespace matching {
ProcessResult MatchingEngine::process(const Command& command) {
    if (const auto* order = std::get_if<LimitOrder>(&command))
        return {book_.add(*order), false};
    return {{}, book_.cancel(std::get<CancelOrder>(command).id)};
}
}  // namespace matching
