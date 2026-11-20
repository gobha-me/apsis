// Layer-3 bot styles. A bot reads the world and returns actions for the input channel; it never writes state.
#pragma once
#include "pilot.hpp"
namespace apsis::world {
// E1 frozen intents. Layer 3: hauler, crisis, pathfinder. Layer 4: the law styles. Builder and farmer come with beacons (4b).
enum Style : int { S_HAULER = 0, S_CRISIS = 1, S_PATHFINDER = 2, S_CAREFUL = 3, S_BOLD = 4, S_RECKLESS = 5, S_PAYER = 6, S_FORGETFUL = 7, S_DOUBLE = 8, S_COUNT = 9 };
extern const char* kStyleName[];
std::vector<Action> decideBot(const World& W, int style);
bool ready(const World& W);
}
