// Faction intents and the one clearing step (spec B5).
// Intents are decided in parallel from a snapshot, wait out their delay, then clear at the end of the
// tick they fall due, grouped by (kind, object):
//   inside a faction: by priority (high first), then decision order;
//   between factions, divisible objects: pro rata by bid (capped apportionment);
//   between factions, indivisible objects: highest bid, ties broken by hash.
// Everything is sorted on a total key, so the result is the same at any thread count.
#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <span>
#include <tuple>
#include <vector>
#include "fixed.hpp"
#include "rng.hpp"
#include "serial.hpp"

namespace apsis {

struct Intent {
  int64_t due = 0;       // day it takes effect
  int32_t kind = 0, object = 0, faction = 0, priority = 0, seq = 0, aux = 0;
  int64_t amount = 0, bid = 0;
  auto key() const { return std::tuple(due, kind, object, faction, -priority, seq); }
};
inline bool canonLess(const Intent& a, const Intent& b) { return a.key() < b.key(); }
inline void put(Writer& w, const Intent& x) { w.i(x.due); w.i(x.kind); w.i(x.object); w.i(x.faction); w.i(x.priority); w.i(x.seq); w.i(x.aux); w.i(x.amount); w.i(x.bid); }
inline void get(Reader& r, Intent& x) { x.due = r.i(); x.kind = (int32_t)r.i(); x.object = (int32_t)r.i(); x.faction = (int32_t)r.i(); x.priority = (int32_t)r.i(); x.seq = (int32_t)r.i(); x.aux = (int32_t)r.i(); x.amount = r.i(); x.bid = r.i(); }

// A group: intents with the same (due, kind, object), already in canonical order.
// Returns the amount granted to each intent.
inline std::vector<int64_t> clearDivisible(std::span<const Intent> g, int64_t available) {
  std::vector<int64_t> grant(g.size(), 0);
  std::vector<int32_t> facs; std::vector<int64_t> want, bids;
  for (const auto& x : g) {
    if (facs.empty() || facs.back() != x.faction) { facs.push_back(x.faction); want.push_back(0); bids.push_back(0); }
    want.back() += x.amount; bids.back() += std::max<int64_t>(x.bid, 0);
  }
  std::vector<int64_t> share;
  if (facs.size() == 1) share = {std::min(available, want[0])};
  else share = cappedApportion(bids, want, std::min(available, std::accumulate(want.begin(), want.end(), int64_t{0})));
  size_t k = 0; int64_t left = 0; int32_t cur = -1;
  for (size_t i = 0; i < g.size(); ++i) {
    if (g[i].faction != cur) { cur = g[i].faction; left = share[k++]; }
    grant[i] = std::min(g[i].amount, left); left -= grant[i];
  }
  return grant;
}

// Indivisible: the index of the winning intent in the group, or -1 if nobody bid.
inline int clearIndivisible(std::span<const Intent> g, uint32_t seed, int64_t day) {
  int best = -1;
  for (int i = 0; i < static_cast<int>(g.size()); ++i) {
    if (g[i].bid <= 0) continue;
    if (best < 0 || g[i].bid > g[best].bid) { best = i; continue; }
    if (g[i].bid == g[best].bid) {
      const uint32_t a = hashN(seed, 0x51ea7, day, g[i].object, g[i].faction, g[i].seq), b = hashN(seed, 0x51ea7, day, g[best].object, g[best].faction, g[best].seq);
      if (a < b || (a == b && canonLess(g[i], g[best]))) best = i;
    }
  }
  return best;
}

}  // namespace apsis
