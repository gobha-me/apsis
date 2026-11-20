// Faction decisions as intents (spec B5, rule `intents`). Each house decides on its own staggered day, from a
// read-only view of the world, in parallel; every intent takes effect after a delay > 0 and clears at the end of
// the day it falls due: inside a house by priority, between houses by bid (indivisible) with a hash tie-break.
#include <algorithm>
#include <chrono>
#include <format>
#include "intents.hpp"
#include "sim.hpp"

namespace apsis::world {
// from power.cpp
i64 presence(const World& W, const Agent& a, int x);
i64 defence(const World& W, int x);
void transfer(World& W, Station& s, int to, int kind);
void startContest(World& W, const Agent& A, int x);
bool canTarget(const World& W, const Agent& a, const Station& s, int owner, const CR* cr);
bool gate(const World& W, int f, int o);
struct Target { int x = -1; i64 score = 0; };
Target bestTarget(const World& W, const Agent& a, const CR& cr);

enum IK : int32_t { I_CUTOFF = 0, I_FORTIFY, I_CONTEST, I_EXPAND, I_SUPPLY, I_RELIGHT, I_EDGE, I_TOLL, I_CONTRACT, I_BUY, I_PATRON };
const char* kIntentName[] = {"cut off", "fortify", "contest", "expand", "supply", "relight", "edge patrol", "toll rate", "carrier contract", "trader buy", "patron"};
static const uint32_t K_STAG = strHash("stagger"), K_TIE = strHash("tie");

namespace {
const Agent* agc(const World& W, int id) { auto it = W.pw.ag.find(id); return it == W.pw.ag.end() ? nullptr : &it->second; }
i64 stagger(const World& W, int f) { return hashN(W.seed, K_STAG, f) % W.CP.factionCycle; }
CR lastCR(const World& W) { return CR{W.crCost, W.crRet, W.crDv, W.crOe}; }
i64 delayOf(const Params& P, int k) {
  switch (k) { case I_SUPPLY: return P.delaySupply; case I_EDGE: return P.delayPatrol; case I_TOLL: return P.delayToll; case I_CONTRACT: case I_BUY: return P.delayContract; case I_RELIGHT: return P.delayBeacon; case I_PATRON: return P.delayStance; default: return P.delayStrategic; }
}
i64 roundBid(const WParams& P, i64 b) { if (b <= 0) return b; const i64 step = std::max(P.bidStepMin, b * P.bidStepPm / 1000); return step > 1 ? b - b % step : b; }
bool hasRoom(const World& W, int f) { i64 outp = 0; for (const auto& s : W.S) if (s.id >= W.n0 && s.owner == f) ++outp; return (i64)W.S.size() < W.n0 + W.P.maxStations - 12 && outp < W.P.maxOutposts; }
bool burntOutpost(const World& W, const Agent& a) { const i64 mo = W.day / 30; for (auto& [x, m] : a.drop) if (x >= W.n0 && mo - m < W.P.dropMemory) return true; return false; }
i64 mineKept(const World& W, int h) { i64 n = 0; for (const auto& s : W.S) { if (W.bk.ds[(size_t)s.id]) continue; const int o = s.owner; const Agent* a = agc(W, o);
    int k = (isAgent(o) && a && a->alive) ? o : -1; if (k < 0) { const int kp = W.bk.kp[(size_t)s.id]; const Agent* b = agc(W, kp); if (kp >= 0 && isAgent(kp) && b && b->alive) k = kp; } if (k == h) ++n; } return n; }
bool strained(const World& W, const Agent& a) { const i64 n = mineKept(W, a.id); bool war = false; for (const auto& c : W.pw.con) if (c.a == a.id || c.d == a.id) war = true;
  return a.T < n * W.P.bcnUpkeepJ * W.P.brokeMonths || war || n > std::max<i64>(1, a.S / W.P.sPerLaneJ); }
}  // namespace

// ---------- decide: a pure function of the world as it stands ----------
static std::vector<Intent> decideFaction(const World& W, int f) {
  const auto& P = W.P; const Agent& a = W.pw.ag.at(f); const i64 mo = W.day / 30; const CR cr = lastCR(W);
  std::vector<Intent> out; int32_t seq = 0;
  auto add = [&](int32_t k, int32_t obj, i64 amount, i64 bid, int32_t prio, int32_t aux) { Intent x; x.due = W.day + delayOf(W.CP, k); x.kind = k; x.object = obj; x.faction = f; x.priority = prio; x.seq = seq++; x.aux = aux; x.amount = amount; x.bid = bid; out.push_back(x); };
  const bool strategic = a.kind != K_HOUSE && a.kind != K_GUILD && ((W.day + stagger(W, f)) % (W.CP.factionCycle * P.decideEvery)) == 0;
  if (strategic) {  // the v0.20 options, same scores, same order of preference
    struct Opt { int k; i64 score; int x; int o; i64 bid; }; std::vector<Opt> opts;
    if (a.T < 0) { std::vector<int> own; for (const auto& s : W.S) if (s.owner == f && s.tier > 0) own.push_back(s.id);
      auto v = [&](int x) { return ((size_t)x < cr.ret.size() ? cr.ret[(size_t)x] : 0) - ((size_t)x < cr.cost.size() ? cr.cost[(size_t)x] : 0); };
      std::stable_sort(own.begin(), own.end(), [&](int x, int y) { return v(x) != v(y) ? v(x) < v(y) : x < y; });
      if ((int)own.size() > (a.kind != K_BASE ? 1 : 0)) opts.push_back({I_CUTOFF, 120 + std::min<i64>(200, -a.T / 500), own[0], 0, 0}); }
    if (a.T > P.fortCost) { int wx = -1; i64 wt = 0;
      for (const auto& s : W.S) { if (s.owner != f || s.tier <= 0) continue; auto fo = a.fort.find(s.id); if (fo != a.fort.end() && fo->second > mo) continue;
        i64 th = 0; for (const auto& c : W.pw.con) if (c.x == s.id) th = 2; i64 ext = 0;
        for (const auto& [bid, b] : W.pw.ag) if (b.alive && b.id != f && b.kind != K_HOUSE && gate(W, b.id, f) && presence(W, b, s.id) * 100 > presence(W, a, s.id) * P.ratioT) ext = 1;
        const i64 t = th + ext; if (t && (wx < 0 || t > wt)) { wx = s.id; wt = t; } }
      if (wx >= 0) opts.push_back({I_FORTIFY, 90 + wt * 20, wx, 1, 0}); }
    if (a.T > P.contestCost * (P.minDur / 2)) { const Target t = bestTarget(W, a, cr); if (t.x >= 0) opts.push_back({I_CONTEST, 100 + t.score, t.x, 2, t.score}); }
    auto fi = W.fac.find(f);
    if (hasRoom(W, f) && !burntOutpost(W, a) && a.kind == K_BASE && fi != W.fac.end() && !fi->second.survey && a.T > P.expandT) opts.push_back({I_EXPAND, 80 + std::min<i64>(100, (a.T - P.expandT) / 2000), f, 3, 0});
    std::stable_sort(opts.begin(), opts.end(), [](const Opt& p, const Opt& q) { return p.score != q.score ? p.score > q.score : p.o < q.o; });
    if (!opts.empty()) add(opts[0].k, opts[0].x, 1, opts[0].bid, 0, 0);
  }
  const bool monthly = ((W.day + stagger(W, f)) % W.CP.factionCycle) == 0;
  if (!monthly) return out;
  // supply, per resource: each own station below the supply line asks the house's best-stocked station (the shared source)
  if (a.kind != K_GUILD) for (int k = 0; k < 4; ++k) {
    int src = -1; for (const auto& s : W.S) if (s.owner == f && (src < 0 || s.stock[k] * W.S[src].cap[k] > W.S[src].stock[k] * s.cap[k])) src = s.id;
    if (src < 0) continue;
    for (const auto& s : W.S) { if (s.owner != f || s.id == src || s.stock[k] * 10000 >= s.cap[k] * P.supplyLinePct) continue;
      const i64 need = std::min(s.cap[k] * P.targetPct / 10000 - s.stock[k], s.cap[k] * P.reliefPct / 10000); if (need <= 0) continue;   // one relief lot
      add(I_SUPPLY, src * 4 + k, need, 0, (int32_t)(s.stage * 10000 + (10000 - s.stock[k] * 10000 / s.cap[k])), s.id); } }
  // relight one dark beacon (v0.20 light rule, decided not done)
  if (isAgent(f) && !strained(W, a)) { const i64 cap = std::max<i64>(1, a.S / P.sPerLaneJ);
    if (mineKept(W, f) < cap - P.relightGap && hashN(W.seed, K_MAP, 2, f, mo) % 1000 < (uint32_t)P.relightPm) {
      std::vector<int> cand; for (const auto& y : W.S) { if (!W.bk.ds[(size_t)y.id]) continue; const Agent* o = agc(W, y.owner);
        if (y.owner == f || ((!isAgent(y.owner) || !o || !o->alive) && coreDist(W, f, y.id) <= P.hopNpc * 2)) cand.push_back(y.id); }
      std::stable_sort(cand.begin(), cand.end(), [&](int p, int q) { const int op = W.S[p].owner == f ? 0 : 1, oq = W.S[q].owner == f ? 0 : 1; if (op != oq) return op < oq; const i64 dp = coreDist(W, f, p), dq = coreDist(W, f, q); if (dp != dq) return dp < dq; return p < q; });
      if (!cand.empty() && a.T >= P.relightJ * P.relightReserve) add(I_RELIGHT, cand[0], P.relightJ, a.T, 0, 0); } }
  // edge patrols on the systems with the most unknown contacts (v0.20 edge rule)
  if (isAgent(f)) { std::vector<std::pair<i64, int>> want;
    for (const auto& s : W.S) { if (s.owner != f) continue; const i64 g = (size_t)s.id < W.dark.g.size() ? W.dark.g[(size_t)s.id] : 0; const i64 w = (g * (s.pop / 1000) / 10) * P.edgePullPct / 100; if (w >= P.edgeW) want.push_back({w, s.id}); }
    std::stable_sort(want.begin(), want.end(), [](auto& x, auto& y) { return x.first != y.first ? x.first > y.first : x.second < y.second; });
    for (size_t i = 0; i < want.size() && (i64)i < P.edgeMax; ++i) add(I_EDGE, want[i].second, P.edgeCost, 0, (int32_t)(P.edgeMax - (i64)i), 0); }
  // the split Core's carriers review their toll in month 4 of the year; sector houses pick carriers in month 5
  if (W.pw.split && mo % 12 == 4 && (f == 0 || f == GUILD)) { bool lanes = false; for (const auto& l : W.net.lanes) if (l.owner == f) lanes = true; const int o = f == 0 ? GUILD : 0; const Agent* oa = agc(W, o);
    bool olanes = false; for (const auto& l : W.net.lanes) if (l.owner == o) olanes = true;
    if (lanes && oa && oa->alive && olanes) { auto n = [&](int c) { i64 k = 0; for (const auto& [x, ct] : W.pw.contract) if (ct.carrier == c) ++k; return k; };
      const i64 diff = n(f) - n(o); const int t = diff > 0 ? 1 : diff < 0 ? -1 : 0; auto lt = W.pw.trend.find(f); const int last = lt == W.pw.trend.end() ? 0 : lt->second;
      auto ri = W.pw.rate.find(f); const i64 r = ri != W.pw.rate.end() ? ri->second : P.tollBase; const i64 nr = (t != 0 && t == last) ? clampI(r + t * P.tollStep, P.tollFloor, P.tollMax) : r;
      add(I_TOLL, f, nr, 0, 0, t); } }
  if (W.pw.split && mo % 12 == 5 && a.kind != K_GUILD) for (int h : W.net.sectorHomes) { if (W.S[h].owner != f || f == 0 || f == GUILD) continue;
    std::vector<int> car; for (int c : {0, GUILD}) { const Agent* ca = agc(W, c); bool any = false; for (const auto& l : W.net.lanes) if (l.owner == c) any = true; if (ca && ca->alive && any) car.push_back(c); }
    auto rate = [&](int c) { auto it = W.pw.rate.find(c); return it != W.pw.rate.end() ? it->second : P.tollBase; };
    std::stable_sort(car.begin(), car.end(), [&](int p, int q) { return rate(p) != rate(q) ? rate(p) < rate(q) : p < q; });
    auto ci = W.pw.contract.find(f);
    if (car.empty()) { if (ci != W.pw.contract.end()) add(I_CONTRACT, f, 0, 0, 0, -1); continue; }
    const int cheapest = car[0]; bool known = ci != W.pw.contract.end() && std::find(car.begin(), car.end(), ci->second.carrier) != car.end();
    if (!known) { const i64 len = ci == W.pw.contract.end() ? P.firstContract[0] + hashN(W.seed, K_SPLIT, 77, f) % (P.firstContract[1] - P.firstContract[0] + 1) : P.contractMonths; add(I_CONTRACT, f, mo + len, 0, 0, cheapest); continue; }
    if (mo < ci->second.until) continue;
    if (cheapest != ci->second.carrier && rate(cheapest) * 100 <= rate(ci->second.carrier) * (100 - P.switchMarginPct)) add(I_CONTRACT, f, mo + P.contractMonths, 0, 0, cheapest);
    else add(I_CONTRACT, f, mo + P.contractMonths, 0, 0, ci->second.carrier); break; }
  // the trader house buys a recently dropped station (v0.20 wild rule)
  if (f == HOUSE) { std::vector<int> cand;
    for (const auto& s : W.S) { if (s.owner != IND || s.tier <= 0 || s.dropped == INT64_MIN || mo - s.dropped > P.houseRecent) continue; bool dep = false; for (const auto& d : s.dep) if (depOut(d) > 0) dep = true; if (!dep) continue;
      bool con = false; for (const auto& c : W.pw.con) if (c.x == s.id) con = true; if (!con) cand.push_back(s.id); }
    auto ret = [&](int x) { return (size_t)x < cr.ret.size() ? cr.ret[(size_t)x] : 0; };
    std::stable_sort(cand.begin(), cand.end(), [&](int p, int q) { return ret(p) != ret(q) ? ret(p) > ret(q) : p < q; });
    if (!cand.empty() && W.S[cand[0]].pop * P.housePriceK <= a.T) add(I_BUY, cand[0], 1, W.S[cand[0]].pop * P.housePriceK, 0, 0); }
  // minors review their patron once a year (v0.20 houseYear, month 3)
  if (mo % P.patronEvery == 3 && (a.kind == K_MINOR || a.kind == K_UPSTART)) add(I_PATRON, f, 0, 0, 0, 0);
  return out;
}

void decideIntents(World& W, const std::vector<int>&, const CR&) {}   // intents mode decides daily in factionDay, not inside the monthly power step

static int choosePatronIntent(const World& W, const Agent& a, int exclude) {
  int x = -1; if (a.seat >= 0 && W.S[a.seat].owner == a.id) x = a.seat; else for (const auto& y : W.S) if (y.owner == a.id) { x = y.id; break; }
  if (x < 0) return -1; int best = -1; i64 bv = -1;
  for (const auto& [id, b] : W.pw.ag) if (b.alive && b.kind == K_BASE && b.id != a.id && b.id != exclude && relOf(W, a.id, b.id) > -3000) { const i64 v = b.S * 100 / (100 + coreDist(W, b.id, x) * W.P.presK); if (v > bv) { bv = v; best = b.id; } }
  return best;
}

// ---------- clear: serial, in canonical order, each intent re-checked against the world as it is now ----------
static void applyIntent(World& W, const Intent& x, i64 grant) {
  const auto& P = W.P; auto it = W.pw.ag.find(x.faction); if (it == W.pw.ag.end() || !it->second.alive) return; Agent& a = it->second; const i64 mo = W.day / 30; const CR cr = lastCR(W);
  W.cov.cleared[x.kind]++;
  switch (x.kind) {
    case I_CUTOFF: { Station& s = W.S[x.object]; i64 n = 0; for (const auto& y : W.S) if (y.owner == a.id && y.tier > 0) ++n;
      if (s.owner == a.id && s.tier > 0 && n > (a.kind != K_BASE ? 1 : 0) && a.T < 0) transfer(W, s, s.pop * 100 >= s.basePop * P.indPopPct ? IND : ABAN, 1); break; }
    case I_FORTIFY: if (W.S[x.object].owner == a.id && a.T > P.fortCost) { a.T -= P.fortCost; a.fort[x.object] = mo + P.fortMonths; } break;
    case I_CONTEST: { const Station& s = W.S[x.object]; int mine = 0; for (const auto& c : W.pw.con) if (c.a == a.id) ++mine;
      if (mine < P.maxContests && a.T > P.contestCost * (P.minDur / 2) && canTarget(W, a, s, s.owner, &cr)) startContest(W, a, x.object); break; }
    case I_EXPAND: { auto fi = W.fac.find(a.id); if (fi != W.fac.end() && !fi->second.survey && a.T > P.expandT && hasRoom(W, a.id) && !burntOutpost(W, a)) { fi->second.survey = 1; fi->second.t = P.surveyMonths[0]; a.T -= P.expandT / 4; } break; }
    case I_SUPPLY: { const int src = x.object / 4, k = x.object % 4; Station& s = W.S[src]; Station& d = W.S[x.aux];
      if (grant <= 0 || s.owner != a.id || d.owner != a.id) break; const i64 n = std::min(grant, d.cap[k] - d.stock[k]); if (n <= 0) break; s.stock[k] -= n; d.stock[k] += n; break; }
    case I_RELIGHT: if (W.bk.ds[(size_t)x.object] && a.T >= P.relightJ * P.relightReserve) { a.T -= P.relightJ; auto& bk = W.bk; if (W.S[x.object].owner != a.id) bk.lit++;
        bk.ds[(size_t)x.object] = 0; bk.u[(size_t)x.object] = 0; bk.relit++; if (W.S[x.object].owner != a.id) bk.kp[(size_t)x.object] = a.id; W.pw.pathV++; } break;
    case I_EDGE: if (W.S[x.object].owner == a.id && a.T >= P.edgeCost * P.edgeReserve) { a.T -= P.edgeCost; W.dark.edge[x.object] = a.id; W.dark.edgeUntil[x.object] = W.day + W.CP.factionCycle; W.dark.edgeN++; W.dark.edgeCost += P.edgeCost; } break;
    case I_TOLL: W.pw.rate[a.id] = x.amount; W.pw.trend[a.id] = x.aux; W.pw.pathV++; break;
    case I_CONTRACT: if (x.aux < 0) W.pw.contract.erase(a.id); else W.pw.contract[a.id] = Contract{x.aux, x.amount}; W.pw.pathV++; break;
    case I_BUY: { Station& s = W.S[x.object]; i64 own = 0; for (const auto& y : W.S) if (y.owner == HOUSE) ++own; bool con = false; for (const auto& c : W.pw.con) if (c.x == s.id) con = true;
      const i64 price = s.pop * P.housePriceK; if (s.owner == IND && !con && own < P.houseMax && a.T >= price) { a.T -= price; a.alive = 1; W.pw.buys++; transfer(W, s, HOUSE, 0); } break; }
    case I_PATRON: { if (a.kind == K_BASE || a.kind == K_HOUSE || a.kind == K_GUILD) break;
      const Agent* cur = nullptr; if (a.patron >= 0) { auto p = W.pw.ag.find(a.patron); if (p != W.pw.ag.end() && p->second.alive && p->second.kind == K_BASE) cur = &p->second; }
      const int best = choosePatronIntent(W, a, -1);
      auto setP = [&](int p) { a.patron = p; if (p >= 0) { auto& v = W.pw.rel[pk(a.id, p)]; v = std::max(v, P.patronRel); } };
      if (!cur) { if (best >= 0 || a.patron >= 0) setP(best); }
      else if (best >= 0 && best != cur->id && a.seat >= 0) { const Agent& b = W.pw.ag.at(best); const i64 pb = b.S * 100 / (100 + coreDist(W, b.id, a.seat) * P.presK), pc = cur->S * 100 / (100 + coreDist(W, cur->id, a.seat) * P.presK);
        if (pb * 100 > pc * P.switchPct) { relAdd(W, a.id, cur->id, -2000); setP(best); } } break; }
  }
}

void factionDay(World& W) {
  // 1. clear what falls due today
  size_t end = 0; while (end < W.queue.size() && W.queue[end].due == W.day) ++end;
  std::vector<Intent> due(W.queue.begin(), W.queue.begin() + (ptrdiff_t)end); W.queue.erase(W.queue.begin(), W.queue.begin() + (ptrdiff_t)end);
  for (size_t a = 0; a < due.size();) {
    size_t b = a; while (b < due.size() && due[b].kind == due[a].kind && due[b].object == due[a].object) ++b;
    std::span<const Intent> g(due.data() + a, b - a); const int kind = g[0].kind;
    if (kind == I_SUPPLY) {   // one source, one supply: by priority, all-or-nothing per request (feed one, never half of both)
      const int src = g[0].object / 4, k = g[0].object % 4; i64 avail = std::max<i64>(0, W.S[src].stock[k] - W.S[src].cap[k] * W.P.hubReservePct / 10000);
      int fed = -1;
      for (const auto& x : g) { if (x.amount <= avail) { applyIntent(W, x, x.amount); avail -= x.amount; if (fed < 0) fed = x.aux; }
        else if (fed >= 0) { W.cov.feedOne++; if (W.cov.log.size() < 3) W.cov.log.push_back(std::format("day {}: house {} could feed one station with supply {} from station {}: fed station {} (more urgent) in full, station {} gets nothing this cycle (asked {}, {} left)", W.day, x.faction, k, src, fed, x.aux, x.amount, avail)); } }
    } else if (kind == I_CONTEST || kind == I_RELIGHT || kind == I_BUY) {   // indivisible: one winner
      bool multi = false; for (const auto& x : g) if (x.faction != g[0].faction) multi = true;
      int win = 0;
      if (multi) { W.cov.contested++;
        std::vector<Intent> r(g.begin(), g.end()); for (auto& x : r) { W.cov.bids++; if (x.bid > 0 && x.bid < W.P.bidStepMin) W.cov.belowStep++; x.bid = roundBid(W.P, x.bid); }
        win = clearIndivisible(r, W.seed, W.day); const int exact = clearIndivisible(g, W.seed, W.day);
        for (size_t i = 1; i < r.size(); ++i) if (r[i].bid == r[(size_t)win].bid && (int)i != win) { W.cov.ties++; break; }
        if (win != exact) W.cov.rankMoved++; if (win < 0) win = 0; }
      applyIntent(W, g[(size_t)win], 1);
    } else for (const auto& x : g) applyIntent(W, x, 1);
    a = b;
  }
  // 2. decide: the houses whose day it is, in parallel, from the world as it now stands
  std::vector<int> deciders; for (const auto& [id, ag] : W.pw.ag) if (ag.alive && ((W.day + stagger(W, id)) % W.CP.factionCycle) == 0) deciders.push_back(id);
  if (deciders.empty()) return;
  W.cov.deciderDays++; W.cov.maxDeciders = std::max<i64>(W.cov.maxDeciders, (i64)deciders.size());
  std::vector<std::vector<Intent>> out(deciders.size());
  if (W.threads > 1 && (!W.pool || W.pool->size() != W.threads)) W.pool = std::make_shared<Pool>(W.threads);
  if (W.threads > 1 && deciders.size() >= kMinParallel) W.cov.poolRuns++;
  const auto t0 = std::chrono::steady_clock::now();
  parallel_for(W.threads > 1 ? W.pool.get() : nullptr, deciders.size(), [&](size_t i) { out[i] = decideFaction(W, deciders[i]); });
  W.cov.decideNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
  for (auto& v : out) for (const auto& x : v) { if (x.due <= W.day) { W.cov.zeroDelay++; fail("intent with no delay: same-tick reactions are not allowed"); } W.cov.intents++; W.cov.byKind[x.kind]++; W.queue.push_back(x); }
  std::stable_sort(W.queue.begin(), W.queue.end(), canonLess);
}

}  // namespace apsis::world
