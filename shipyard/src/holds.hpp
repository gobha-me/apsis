#pragma once

#include <apsis/shipyard/loader.hpp>
#include <string>

namespace apsis::shipyard::detail {
// Holds preserve zero-based deck indices; only the human label adds one.
inline std::string hold_label(const Json& hold) {
    return "decks " + std::to_string(hold.at("firstDeck").get<std::size_t>() + 1) +
           "–" + std::to_string(hold.at("lastDeck").get<std::size_t>() + 1);
}
} // namespace apsis::shipyard::detail
