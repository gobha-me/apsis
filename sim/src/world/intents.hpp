#pragma once
#include "ops.hpp"
namespace apsis::world {
struct CR;
extern const char* kIntentName[];
void factionDay(World& W);                // intents mode: clear what falls due, then the houses whose day it is decide
void decideIntents(World& W, const std::vector<int>& facs, const CR& cr);   // B5: faction decisions as intents (rule `intents`)
}
