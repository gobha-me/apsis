#include "proto.hpp"
#include <algorithm>
#include "../core/pool.hpp"
#include "../core/rng.hpp"

namespace apsis::proto {
namespace {
constexpr uint32_t K_STOCK = strHash("stock"), K_RATE = strHash("rate"), K_STAG = strHash("stagger"), K_CON = strHash("contract"), K_BOT = strHash("bot");
constexpr int64_t kReserve = 100, kBuyPrice = 3, kSellPrice = 4, kMoney = G;

int64_t stagger(const World& w, int f) { return hashN(w.seed, K_STAG, f) % w.P.factionCycle; }

// Decide: a pure function of the snapshot. Writes only its own output vector.
std::vector<Intent> decide(const World& w, int f) {
  std::vector<Intent> out; int32_t seq = 0;
  auto add = [&](int32_t kind, int32_t obj, int64_t amount, int64_t bid, int32_t prio, int32_t aux, int64_t delay) {
    Intent x; x.due = w.day + delay; x.kind = kind; x.object = obj; x.faction = f; x.priority = prio; x.seq = seq++; x.aux = aux; x.amount = amount; x.bid = bid; out.push_back(x);
  };
  std::vector<int> own; for (int s = 0; s < (int)w.S.size(); ++s) if (w.S[s].owner == f) own.push_back(s);
  for (int g = 0; g < G; ++g) {
    int hi = -1; for (int s : own) if (hi < 0 || w.S[s].stock[g] > w.S[hi].stock[g]) hi = s;
    if (hi < 0) continue;
    // every short station asks the richest one: shared limit, settled by priority inside the faction
    for (int s : own) if (s != hi && w.S[s].stock[g] < w.S[s].cap[g] / 4 && w.S[hi].stock[g] > w.S[s].stock[g] + 200)
      add(I_MOVE, hi * G + g, (w.S[hi].stock[g] - w.S[s].stock[g]) / 3, 0, (int32_t)((w.S[s].cap[g] - w.S[s].stock[g]) / 10), s, w.P.delaySupply);
    int64_t tot = 0; for (int s : own) tot += w.S[s].stock[g];
    if (f != 0 && !own.empty() && tot < 400 * (int64_t)own.size()) {   // buy from the Core hub: contested between factions, pro rata by bid
      int lo = own[0]; for (int s : own) if (w.S[s].stock[g] < w.S[lo].stock[g]) lo = s;
      const int64_t units = 300, price = 2 + hashN(w.seed, K_STOCK, f, w.day, g) % 3;
      if (w.F[f].treasury >= units * price) add(I_HUB, g, units, units * price, 0, lo, w.P.delaySupply);
    }
  }
  if (w.F[f].treasury > 5000)                // bid for a contract: indivisible, highest bid wins
    add(I_CONTRACT, (int32_t)(hashN(w.seed, K_CON, f, w.day) % w.C.size()), 1, w.F[f].treasury / 20 / 250 * 250, 0, 0, w.P.delayContract);  // posted in steps of 250, so ties happen
  return out;
}

void moveGoods(World& w, int from, int to, int g, int64_t n) {
  n = std::min(n, w.S[to].cap[g] - w.S[to].stock[g]);
  if (n <= 0) return;
  w.S[from].stock[g] -= n; w.S[to].stock[g] += n;
}

void applyIntents(World& w) {
  size_t end = 0; while (end < w.queue.size() && w.queue[end].due == w.day) ++end;
  std::span<const Intent> due(w.queue.data(), end);
  for (size_t a = 0; a < due.size();) {
    size_t b = a; while (b < due.size() && due[b].kind == due[a].kind && due[b].object == due[a].object) ++b;
    auto g = due.subspan(a, b - a); const Intent& x = g[0];
    if (x.kind == I_MOVE) {
      const int src = x.object / G, good = x.object % G;
      const auto grant = clearDivisible(g, std::max<int64_t>(0, w.S[src].stock[good] - kReserve));
      for (size_t i = 0; i < g.size(); ++i) { moveGoods(w, src, g[i].aux, good, grant[i]); w.cov.moveGranted += grant[i] > 0; }
    } else if (x.kind == I_HUB) {
      const int good = x.object;
      const auto grant = clearDivisible(g, std::max<int64_t>(0, w.S[0].stock[good] - kReserve));
      w.cov.hubContested += g.front().faction != g.back().faction;
      for (size_t i = 0; i < g.size(); ++i) {
        const int64_t price = g[i].bid / std::max<int64_t>(1, g[i].amount);
        int64_t n = std::min(grant[i], w.F[g[i].faction].treasury / std::max<int64_t>(1, price));
        n = std::min(n, w.S[g[i].aux].cap[good] - w.S[g[i].aux].stock[good]);
        if (n <= 0) continue;
        ++w.cov.hubGranted;
        w.S[0].stock[good] -= n; w.S[g[i].aux].stock[good] += n;
        w.F[g[i].faction].treasury -= n * price; w.F[w.S[0].owner].treasury += n * price;
      }
    } else if (x.kind == I_CONTRACT) {
      const int win = clearIndivisible(g, w.seed, w.day);
      for (size_t i = 1; i < g.size(); ++i) if (g[i].bid == g[0].bid && g[i].faction != g[0].faction) { ++w.cov.contractTies; break; }
      if (win >= 0 && w.F[g[win].faction].treasury >= g[win].bid) {
        w.F[g[win].faction].treasury -= g[win].bid; w.led.lose(kMoney, g[win].bid);
        w.C[x.object].holder = g[win].faction; ++w.cov.contractWins;
      }
    }
    a = b;
  }
  w.queue.erase(w.queue.begin(), w.queue.begin() + (ptrdiff_t)end);
}

void applyAction(World& w, const Action& a) {
  const int64_t before = w.pl.wallet;
  struct Count { World& w; int64_t b; ~Count() { w.cov.actsApplied += w.pl.wallet != b; } } count{w, before};
  Pilot& p = w.pl; Station& s = w.S[p.at];
  int64_t free = w.P.stockHold; for (auto h : p.hold) free -= h;
  if (a.kind == SELL) {
    const int g = (int)(a.f[0] % G);
    int64_t n = std::min({a.f[1], p.hold[g], s.cap[g] - s.stock[g], w.F[s.owner].treasury / kSellPrice});
    if (n <= 0) return;
    p.hold[g] -= n; s.stock[g] += n; w.F[s.owner].treasury -= n * kSellPrice; p.wallet += n * kSellPrice;
  } else if (a.kind == BUY) {
    const int g = (int)(a.f[0] % G);
    int64_t n = std::min({a.f[1], s.stock[g] - kReserve, free, p.wallet / kBuyPrice});
    if (n <= 0) return;
    s.stock[g] -= n; p.hold[g] += n; p.wallet -= n * kBuyPrice; w.F[s.owner].treasury += n * kBuyPrice;
  } else if (a.kind == MOVE) {
    const int to = (int)(a.f[0] % (int64_t)w.S.size());
    if (to == p.at || p.wallet < w.P.subLight) return;
    p.at = to; p.wallet -= w.P.subLight; w.led.lose(kMoney, w.P.subLight);
  }
}

void economy(World& w) {
  for (auto& s : w.S) {
    const int64_t made = std::min(s.rate, s.cap[s.prod] - s.stock[s.prod]);
    if (made > 0) { s.stock[s.prod] += made; w.led.make(s.prod, made); }
    for (int g = 0; g < G; ++g) if (s.stock[g] >= 3) { s.stock[g] -= 3; w.led.lose(g, 3); }
  }
  for (int f = 0; f < (int)w.F.size(); ++f) {
    int64_t inc = 10; for (const auto& c : w.C) if (c.holder == f) inc += 5;
    w.F[f].treasury += inc; w.led.make(kMoney, inc);
  }
}

void putWorld(Writer& wr, const World& w, bool withParams) {
  if (withParams) put(wr, w.P);
  wr.u(w.seed); wr.i(w.day);
  wr.u(w.S.size()); for (const auto& s : w.S) { wr.i(s.owner); wr.i(s.prod); wr.i(s.rate); for (auto v : s.stock) wr.i(v); for (auto v : s.cap) wr.i(v); }
  wr.u(w.F.size()); for (const auto& f : w.F) wr.i(f.treasury);
  wr.u(w.C.size()); for (const auto& c : w.C) wr.i(c.holder);
  wr.i(w.pl.at); wr.i(w.pl.wallet); for (auto v : w.pl.hold) wr.i(v);
  wr.vec(w.queue); put(wr, w.led);
}
}  // namespace

World create(uint32_t seed, int stations, int factions, int contracts) {
  World w; w.seed = seed;
  w.S.resize((size_t)stations); w.F.resize((size_t)factions); w.C.resize((size_t)contracts);
  for (int s = 0; s < stations; ++s) {
    auto& st = w.S[(size_t)s]; st.owner = s % factions; st.prod = (int32_t)(hashN(seed, K_RATE, s) % G); st.rate = 20 + hashN(seed, K_RATE, s, 1) % 20;
    for (int g = 0; g < G; ++g) { st.cap[g] = 2000; st.stock[g] = 300 + hashN(seed, K_STOCK, s, g) % 700; w.led.make(g, st.stock[g]); }
  }
  for (auto& f : w.F) { f.treasury = 20000; w.led.make(kMoney, f.treasury); }
  w.pl.at = 1 % stations; w.pl.wallet = 2000; w.led.make(kMoney, w.pl.wallet);
  return w;
}

void submit(World& w, const Action& a) { w.inbox.submit(a); }

void tick(World& w) {
  // 1. decide, in parallel, from the snapshot (the world as the day starts)
  std::vector<int> deciders;
  for (int f = 0; f < (int)w.F.size(); ++f) if ((w.day + stagger(w, f)) % w.P.factionCycle == 0) deciders.push_back(f);
  std::vector<std::vector<Intent>> out(deciders.size());
  w.cov.multiDecideDays += deciders.size() > 1;
  w.cov.poolDays += w.threads > 1 && deciders.size() >= kMinParallel;
  if (w.threads > 1 && (!w.pool || w.pool->size() != w.threads)) w.pool = std::make_shared<Pool>(w.threads);
  parallel_for(w.threads > 1 ? w.pool.get() : nullptr, deciders.size(), [&](size_t i) { out[i] = decide(w, deciders[i]); });
  for (auto& v : out) { for (const auto& x : v) if (x.due <= w.day) fail("intent with no delay: same-tick reactions are not allowed"); w.cov.intents += (int64_t)v.size(); w.queue.insert(w.queue.end(), v.begin(), v.end()); }
  std::stable_sort(w.queue.begin(), w.queue.end(), canonLess);
  // 2. clear what falls due today (serial, sorted)
  applyIntents(w);
  // 3. the player's actions, in canonical order
  for (const auto& a : w.inbox.drain()) { w.log.push_back({w.day, a}); applyAction(w, a); }
  // 4. the daily economy
  economy(w);
  ++w.day;
}

std::vector<Action> bot(const World& w) {
  const Pilot& p = w.pl; const Station& s = w.S[(size_t)p.at]; std::vector<Action> acts;
  for (int g = 0; g < G; ++g) if (p.hold[g] > 0 && s.stock[g] < s.cap[g] / 2) acts.push_back({SELL, {g, p.hold[g]}});
  if (s.stock[s.prod] > 600) acts.push_back({BUY, {s.prod, 40}});
  if (hashN(w.seed, K_BOT, w.day) % 5 == 0) acts.push_back({MOVE, {(int64_t)(hashN(w.seed, K_BOT, w.day, 1) % w.S.size())}});
  // raw order is scrambled on purpose: canonical ordering must make it irrelevant
  if (hashN(w.seed, K_BOT, w.day, 2) & 1) std::reverse(acts.begin(), acts.end());
  return acts;
}

std::vector<uint8_t> checkpoint(const World& w) { Writer wr; putWorld(wr, w, true); return wr.b; }

bool restore(const std::vector<uint8_t>& b, World& out) {
  World w; Reader r{b}; get(r, w.P);
  w.seed = (uint32_t)r.u(); w.day = r.i();
  const auto n = [&](uint64_t cap) { const uint64_t v = r.u(); if (v > cap) { r.bad = true; return uint64_t{0}; } return v; };
  w.S.resize((size_t)n(1u << 20)); for (auto& s : w.S) { s.owner = (int32_t)r.i(); s.prod = (int32_t)r.i(); s.rate = r.i(); for (auto& v : s.stock) v = r.i(); for (auto& v : s.cap) v = r.i(); }
  w.F.resize((size_t)n(1u << 20)); for (auto& f : w.F) f.treasury = r.i();
  w.C.resize((size_t)n(1u << 20)); for (auto& c : w.C) c.holder = (int32_t)r.i();
  w.pl.at = (int32_t)r.i(); w.pl.wallet = r.i(); for (auto& v : w.pl.hold) v = r.i();
  r.vec(w.queue); get(r, w.led);
  if (r.bad || r.p != b.size()) return false;
  out = std::move(w); return true;
}

std::string hashState(const World& w) { Writer wr; putWorld(wr, w, false); return hashBytes(wr.b); }

bool ledgerOk(const World& w) {
  for (int g = 0; g < G; ++g) { int64_t t = w.pl.hold[g]; for (const auto& s : w.S) t += s.stock[g]; if (t != w.led.expected(g)) return false; }
  int64_t m = w.pl.wallet; for (const auto& f : w.F) m += f.treasury;
  return m == w.led.expected(kMoney);
}

}  // namespace apsis::proto
