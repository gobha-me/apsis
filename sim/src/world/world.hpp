// Layer 2: the world (map, economy, factions), ported from v0.20's world-only code paths.
// The port keeps v0.20's names so it can be checked against it. With rule `intents` off it must
// match v0.20 tick for tick (no player); with `intents` on, factions decide through intents (B5).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../core/clear.hpp"
#include "../core/ledger.hpp"
#include "../core/params.hpp"
#include "../core/pool.hpp"
#include "wparams.hpp"
#include <functional>

namespace apsis::world {

using i64 = int64_t;
using A4 = std::array<i64, 4>;
using A3 = std::array<i64, 3>;
constexpr int IND = 2, ABAN = 3, HOUSE = 4, GUILD = 8;
constexpr int MAJOR_IDS[6] = {0, 1, 5, 6, 7, 8};
inline bool isFac(int o) { return o == 0 || o == 1 || o >= 5; }
inline bool isAgent(int o) { return o == 0 || o == 1 || o >= 4; }
inline i64 idiv(i64 a, i64 b) { return a / b; }
inline i64 clampI(i64 v, i64 a, i64 b) { return v < a ? a : v > b ? b : v; }
struct Pt { i64 x = 0, y = 0; bool ok = false; };
i64 edist(const Pt& p, const Pt& q);

// ---------- static map (net) ----------
struct NetSt { int id = 0, tier = 0, parent = -1, owner = 0, region = 0; Pt pos; std::string name; };
struct Lane { int a = 0, b = 0; i64 len = 0; int lng = 0; i64 hw = 0, hd = 0, ha = 0; int j = -1, owner = -1; int bypass = -1; };
struct Leg { int a = 0, b = 0; i64 from = 0, len = 0; int stopIdx = -1; };
struct Route { int id = 0, hub = 0, owner = 0; std::vector<int> stops; std::vector<Leg> legs; i64 len = 0, far = 0; int freighters = 0, base = 0, relay = 0; };
struct Hazard { int t = 0; Pt p; i64 r = 0, acc = 0, sub = 0, per = 0, win = 0; int rift = 0, id = 0; };   // t: 0 neb, 1 well, 2 pul, 3 rog
struct Net {
  std::vector<NetSt> st;
  std::vector<Lane> lanes;
  std::vector<Route> routes;
  std::vector<int> hubs, majors, fairHubs, junctions, sectorHomes;
  int n0 = 0, coreHome = 0; i64 side = 0, laneN0 = -1;
  i64 J = 0;   // the jump unit (spec B1): median hop length of the map, in map units; layer 3 measures jumps in it
  std::vector<Hazard> hz;
  std::vector<std::vector<i64>> dist; std::vector<std::vector<int>> next;
  std::map<int, std::array<int, 4>> spec;
};

// ---------- state ----------
struct Dep { int t = 0; i64 rate0 = 0, res0 = 0, res = 0, half = -1, quarter = -1; int b = 0; };   // b: 0 world, 1 belt, 2 giant, 3 ice
struct Acct { std::vector<i64> cost, ret; i64 dv = 0, rawOut = 0, owned = 0; };
struct Station {
  int id = 0, tier = 0, owner = 0; i64 basePop = 0, pop = 0; A4 burn0{}, cap{}, prod0{}, stock{}, outNow{};
  i64 ema = 7000, stage = 0, dwell = 0, wear = 0, mood = 0, leftNormal = -1;
  std::vector<Dep> dep; A3 raw{}, rawCap{}; Acct acct; A3 so3{}; i64 born = 0;
  i64 dropped = INT64_MIN;   // month the owner let it go (v0.20 s.dropped), or none
};
struct Freighter { int id = 0, r = 0, leg = -1; i64 t = 0; std::vector<A4> cargo; A4 ret{}; A3 raw{}; int dead = 0, retire = 0; };
struct Unit { i64 s = 0; };
struct Group { i64 s = 0, pos = 0, last = -1000; };
struct RouteSt { i64 P = 0, Q = 300; A3 V{}; i64 dv = 0, loot = 0; std::vector<i64> tHist; std::vector<Group> groups; std::vector<Unit> units; };

}  // namespace apsis::world
