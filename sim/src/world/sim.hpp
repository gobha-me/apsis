#pragma once
#include <string>
#include "state.hpp"
namespace apsis::world {
World create(uint32_t seed, i64 majors, const Rules& rules, const WParams& P = WParams{});
void step(World& W);                       // one day
bool ledgerOk(const World& W);
std::string dump(const World& W);          // the state, one fact per line, in v0.20's terms (for the port gate)
std::string hashState(const World& W);
int generalWarPairs(const World& W, int* pairs);   // great-house pairs hostile now (burn20 hostileNow)
}
namespace apsis::world {
std::vector<uint8_t> checkpoint(const World& W);
bool restore(const std::vector<uint8_t>& b, World& out);   // false on a damaged checkpoint
}
