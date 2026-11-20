// Internal operations of the world, shared between its source files. Names follow v0.20.
#pragma once
#include <string>
#include "state.hpp"
#include "../core/rng.hpp"
namespace apsis::world {
extern const uint32_t K_ICPT, K_POS, K_FAIL, K_FSTK, K_SURV, K_FOUND, K_PAT, K_SPLIT, K_SPEC, K_DARK, K_MAP;
struct CR { std::vector<i64> cost, ret, dv, oe; };
inline i64 depOut(const Dep& d) { return d.rate0 * d.res / d.res0; }
inline i64 depSum(const Station& s) { i64 a = 0; for (const auto& d : s.dep) a += depOut(d); return a; }
i64 worstRatio(const Station& s);
i64 coreDist(const World& W, int o, int x);
int hullsOn(const World& W, int ri);
Freighter mkFreighter(const std::vector<Freighter>& F, const Route& r, int k, const WParams& P);
void stationDay(World& W, Station& s);
void freighterDay(World& W, Freighter& f);
CR stationCR(const World& W);
void accounting(World& W, const CR& cr);
void survey(World& W);
void syncHulls(World& W);
void rehop(World& W);
// power
i64 relOf(const World& W, int a, int b);
void relAdd(World& W, int a, int b, i64 d);
std::vector<int> aliveIds(const World& W);
bool hostile(const World& W, int a, int b);
void power(World& W, const CR& cr);
void tradeMonth(World& W);
i64 laneToll(const World& W, const Lane& l, int importer);
// beacons and the dark edge
bool laneDarkI(const World& W, int li);
void mapMonthJ(World& W);
void darkMonth(World& W);
i64 psOf(const World& W, int h);
}  // namespace apsis::world
