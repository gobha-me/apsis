// World state beyond the map: houses (agents), contests, tolls, beacons, the dark edge.
#pragma once
#include <climits>
#include "world.hpp"
#include "pilotstate.hpp"
#include "../core/input.hpp"
namespace apsis::world {

enum Kind : int { K_BASE = 0, K_HOUSE = 1, K_MINOR = 2, K_GUILD = 3, K_UPSTART = 4 };
struct Agent {
  int id = 0, kind = K_BASE, alive = 1, seat = -1, patron = -1, up = 0, deep = 0, weak = 0;
  i64 T = 0, S = 0, born = 0, died = -1, peak = 0, n = 0, crush = 0, fallC = 0, weakC = 0, riseC = 0, fell = 0, rose = 0;
  std::map<int, i64> fort, drop;
};
struct Contest { i64 uid = 0; int x = 0, a = 0, d = 0; i64 m = 0, margin = 0, dsum = 0, luck = 0, min = 0; int core = 0; };
struct Contract { int carrier = 0; i64 until = 0; };
using PK = std::pair<int, int>;
inline PK pk(int a, int b) { return a < b ? PK{a, b} : PK{b, a}; }
struct PathSig { std::vector<int> owners; std::map<int, i64> rate; std::map<int, Contract> contract; std::vector<int> alive;
  bool operator==(const PathSig& o) const { if (owners != o.owners || rate != o.rate || alive != o.alive || contract.size() != o.contract.size()) return false;
    for (auto a = contract.begin(), b = o.contract.begin(); a != contract.end(); ++a, ++b) if (a->first != b->first || a->second.carrier != b->second.carrier || a->second.until != b->second.until) return false; return true; } };
struct Pw {
  std::map<int, Agent> ag; std::map<PK, i64> rel; std::vector<Contest> con;
  std::map<PK, i64> trade, paid; int nextId = 9; i64 relVol = 0, relN = 0, clogN = 0;
  i64 conUid = 0; int split = 0; std::map<int, i64> rate, tollAcc; std::map<int, Contract> contract; std::map<int, int> jPrev, trend;
  std::vector<int> majors0; i64 pathV = 0; bool hasPathSig = false; PathSig pathSig;
  // counters the gates read
  i64 contests = 0, wins = 0, holds = 0, breaks = 0, upSpawn = 0, falls = 0, rises = 0, buys = 0;
  // path cache (not state: rebuilt from the same inputs)
  i64 pathsV = -1; std::map<std::string, std::vector<int>> paths;
};
struct Fac { i64 base = 0; int survey = 0; i64 t = 0, chart = 0; int charted = 0; };
struct Bk { std::vector<i64> ds, u; std::vector<int> kp; i64 upkeep = 0, lapsed = 0, relit = 0, lit = 0, darkCut = 0, prosp = 0; };
struct Dark { std::map<int, i64> edgeUntil; std::vector<i64> g; std::vector<A3> cur; std::vector<char> curOn; std::vector<i64> flow; std::vector<i64> lastNpc;
  std::map<int, int> edge, edgeHit; std::map<int, i64> ps; i64 edgeN = 0, edgeCost = 0; };
struct Led { i64 init = 0, prod = 0, burn = 0, spill = 0, waste = 0, loot = 0, rawExt = 0, rawConv = 0, rawLoot = 0, rawSpill = 0, ploss = 0; };

struct World {
  WParams P; Params CP; Rules rules; uint32_t seed = 0;   // CP: the core placeholder table (delays, cycle)
  Net net;
  i64 day = 0; int n0 = 0;
  std::vector<Station> S; std::vector<Freighter> F; std::vector<RouteSt> RT;
  std::map<int, Fac> fac; std::vector<int> facIds;
  Pw pw; Bk bk; Dark dark; Led led;
  // faction intents (B5): the queue and what cleared
  Pilot pl;                            // layer 3: the player (alive = 0 until START)
  std::vector<Intent> queue; std::vector<i64> crCost, crRet, crDv, crOe; int crSet = 0;
  // not state
  Inbox inbox; std::vector<LogEntry> log;   // the input channel (not state)
  mutable std::vector<i64> distM, wellD; mutable size_t distN = 0;   // cache, not state: straight-line distances between systems
  mutable std::map<std::string, std::shared_ptr<const void>> routeCache; mutable i64 routeCacheDay = -1;   // cache, not state: the bots' routes for today
  mutable std::vector<i64> tollCache; mutable i64 tollCacheDay = -1;   // cache, not state: arrival tolls for today
  int threads = 1; std::shared_ptr<Pool> pool;
  struct Cov { i64 stationFails = 0, poolRuns = 0, deciderDays = 0, maxDeciders = 0, intents = 0, zeroDelay = 0, feedOne = 0, contested = 0, ties = 0, rankMoved = 0, belowStep = 0, bids = 0, decideNs = 0; std::map<int, i64> byKind, cleared; std::vector<std::string> log; } cov;
};

}  // namespace apsis::world
