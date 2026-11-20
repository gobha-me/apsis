#pragma once
#include <functional>
#include "world.hpp"
namespace apsis::world {
extern const uint32_t K_NET, K_CORE, K_BODY, K_FAIR, K_JUMP;
void floyd(Net& net);
std::vector<int> pathOf(const Net& net, int a, int b);
Route buildRoute(const Net& net, int h, const std::vector<int>& stops, const WParams& P, bool relay);
std::vector<Lane> hopList(const std::vector<Pt>& pts, const WParams& P, const std::vector<Hazard>& hz);
Net genCoreMap(uint32_t seed, const WParams& P);
std::vector<Dep> genBodies(uint32_t seed, const NetSt& s, const WParams& P, i64 salt);
bool fairCheck(const Net& net, std::vector<Station>& S, const WParams& P, i64* rebalanced);
void placementRule(const Net& net, std::vector<Station>& S, const WParams& P, uint32_t seed);
Station mkStation(const WParams& P, const NetSt& s, uint32_t seed);
}
