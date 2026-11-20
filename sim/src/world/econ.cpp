// The daily economy and the monthly bookkeeping (v0.20 stationDay, freighters, accounting, survey, hulls).
#include <algorithm>
#include "gen.hpp"
#include "ops.hpp"

namespace apsis::world {

const uint32_t K_ICPT = strHash("icpt"), K_POS = strHash("pos"), K_FAIL = strHash("fail"), K_FSTK = strHash("fstk"), K_SURV = strHash("surv"), K_FOUND = strHash("found"),
               K_PAT = strHash("patron"), K_SPLIT = strHash("split"), K_SPEC = strHash("spec"), K_DARK = strHash("dark"), K_MAP = strHash("map");
static const int RAW_TO[3][4] = {{0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 0, 0}};

i64 worstRatio(const Station& s) { int m = 0; for (int k = 1; k < 4; ++k) if (s.stock[k] * s.cap[m] < s.stock[m] * s.cap[k]) m = k; return std::min<i64>(10000, s.stock[m] * 10000 / s.cap[m]); }
i64 coreDist(const World& W, int o, int x) {
  i64 d = 1000000000; for (const auto& c : W.S) if (c.tier == 0 && c.owner == o) d = std::min(d, W.net.dist[c.id][x]);
  if (d == 1000000000) { auto it = W.pw.ag.find(o); const int seat = it != W.pw.ag.end() ? it->second.seat : -1; d = seat >= 0 ? W.net.dist[seat][x] : 0; }
  return d;
}
int hullsOn(const World& W, int ri) { int n = 0; for (const auto& f : W.F) if (f.r == ri && !f.dead) ++n; return n; }
Freighter mkFreighter(const std::vector<Freighter>& F, const Route& r, int k, const WParams& P) {
  Freighter f; f.id = (int)F.size(); f.r = r.id; f.t = idiv(k * (r.len + P.dwell * (i64)r.stops.size()), std::max(1, r.freighters)) + 1; f.cargo.assign(r.stops.size(), A4{}); return f; }

static void convRaw(World& W, Station& s) {
  const auto& P = W.P;
  for (int t = 0; t < 3; ++t) { const i64 c = s.raw[t]; if (!c) continue;
    s.raw[t] -= c; W.led.rawConv += c; const i64 out = c * P.localEffPct / 100;
    s.acct.rawOut += out * P.rawValue * P.royaltyPct / 100;
    for (int k = 0; k < 4; ++k) if (RAW_TO[t][k]) { s.stock[k] += out; W.led.prod += out; if (s.stock[k] > s.cap[k]) { W.led.spill += s.stock[k] - s.cap[k]; s.stock[k] = s.cap[k]; } } }
}

void stationDay(World& W, Station& s) {
  const auto& P = W.P; auto& led = W.led; const i64 d = W.day;
  if (s.owner == ABAN) {
    s.pop = std::max<i64>(0, s.pop - std::max<i64>(1, s.pop / 200)); s.stage = 3; s.mood += (0 - s.mood) / 90;
    for (int k = 0; k < 4; ++k) { const i64 burn = s.burn0[k] * s.pop / s.basePop, take = std::min(burn, s.stock[k]); s.stock[k] -= take; led.burn += take; }
    return;
  }
  const i64 bp = P.burnPct[(size_t)s.stage];
  for (int k = 0; k < 4; ++k) {
    const i64 prod = s.prod0[k] * s.pop / s.basePop; s.stock[k] += prod; led.prod += prod;
    if (s.stock[k] > s.cap[k]) { led.spill += s.stock[k] - s.cap[k]; s.stock[k] = s.cap[k]; }
    const i64 burn = (s.burn0[k] * s.pop / s.basePop) * bp / 100, take = std::min(burn, s.stock[k]); s.stock[k] -= take; led.burn += take;
    if (s.stock[k] == 0 && burn > 0 && !s.outNow[0]) s.outNow[0] = 1;
  }
  if (s.outNow[0] && s.stock[0] > s.burn0[0] * 10 && s.stock[1] > s.burn0[1] * 10 && s.stock[2] > s.burn0[2] * 10 && s.stock[3] > s.burn0[3] * 10) s.outNow[0] = 0;
  for (auto& dp : s.dep) {
    if (dp.quarter >= 0 && (dp.res == 0 || s.rawCap[dp.t] <= s.raw[dp.t])) continue;
    const i64 o0 = dp.rate0 * dp.res / dp.res0; i64 out = s.pop == s.basePop ? o0 : o0 * s.pop / s.basePop;
    out = std::min({out, dp.res, s.rawCap[dp.t] - s.raw[dp.t]});
    if (out > 0) { dp.res -= out; s.raw[dp.t] += out; led.rawExt += out; }
    if (dp.quarter < 0) { const i64 o = depOut(dp); if (dp.half < 0 && o * 2 < dp.rate0) dp.half = d; if (o * 4 < dp.rate0) dp.quarter = d; }
  }
  convRaw(W, s);
  const i64 w = worstRatio(s); s.ema += (w - s.ema) / P.emaDiv;
  if (w < P.failT) { const i64 g = P.failT - w, chance = P.failMax * g * g / (P.failT * P.failT);
    if (hashN(W.seed, K_FAIL, d, s.id) % 10000 < chance) { W.cov.stationFails++; s.wear = std::min<i64>(10000, s.wear + P.wearHit);
      const int k = (int)(hashN(W.seed, K_FSTK, d, s.id) % 4); const i64 loss = std::min(s.stock[k], s.burn0[k] * P.failWasteDays); s.stock[k] -= loss; led.waste += loss; } }
  else s.wear = std::max<i64>(0, s.wear - P.wearDecay);
  s.dwell++;
  if (s.dwell >= P.stageDwell) { i64 ns = s.stage;
    if (s.stage < 3 && s.ema < P.down[(size_t)s.stage]) ns = s.stage + 1; else if (s.stage > 0 && s.ema > P.up[(size_t)s.stage - 1]) ns = s.stage - 1;
    if (ns != s.stage) { if (s.stage == 0) s.leftNormal = d; if (ns == 0) s.leftNormal = -1; s.stage = ns; s.dwell = 0; } }
  if (s.stage == 3) s.pop = std::max(s.basePop / 10, s.pop - std::max<i64>(1, s.pop / 300));
  else if (s.stage == 0 && s.pop < s.basePop) s.pop = std::min(s.basePop, s.pop + std::max<i64>(1, (s.basePop - s.pop) / 600));
  const i64 target = P.moodTarget[(size_t)s.stage] - s.wear / 4; s.mood += (target - s.mood) / 90;
}

static i64 patrolPos(const World& W, int r, i64 u, i64 n, i64 day) { const i64 L = W.net.routes[r].len, span = 2 * L, p = (span * u / std::max<i64>(1, n) + day) % span; return p <= L ? p : span - p; }

static void unload(World& W, Freighter& f) {
  const Route& r = W.net.routes[f.r]; Station& hub = W.S[r.hub]; const auto& P = W.P;
  for (int k = 0; k < 4; ++k) { hub.stock[k] += f.ret[k]; f.ret[k] = 0; }
  for (auto& c : f.cargo) for (int k = 0; k < 4; ++k) { hub.stock[k] += c[k]; c[k] = 0; }
  for (int k = 0; k < 4; ++k) if (hub.stock[k] > hub.cap[k]) { W.led.spill += hub.stock[k] - hub.cap[k]; hub.stock[k] = hub.cap[k]; }
  i64 v = 0; for (int t = 0; t < 3; ++t) { hub.raw[t] += f.raw[t]; v += f.raw[t]; f.raw[t] = 0; if (hub.raw[t] > hub.rawCap[t]) { W.led.rawSpill += hub.raw[t] - hub.rawCap[t]; hub.raw[t] = hub.rawCap[t]; } }
  W.RT[f.r].dv += v * P.rawValue;
}

static void loadFreighter(World& W, Freighter& f) {
  const auto& P = W.P; const Route& r = W.net.routes[f.r];
  unload(W, f);
  Station& hub = W.S[r.hub]; if (hub.owner == ABAN) return;
  i64 eta = 0; std::vector<i64> etaOf(r.stops.size(), 0);
  for (const auto& lg : r.legs) { eta += lg.len; if (lg.stopIdx >= 0) { etaOf[lg.stopIdx] = eta; eta += P.dwell; } }
  std::vector<const Freighter*> mates; for (const auto& g : W.F) if (g.r == f.r && &g != &f && !g.dead) mates.push_back(&g);
  std::vector<std::pair<int, int>> ents; std::vector<i64> w, caps;
  for (size_t si = 0; si < r.stops.size(); ++si) { const Station& s = W.S[r.stops[si]];
    for (int k = 0; k < 4; ++k) {
      i64 inbound = 0; for (const auto* g : mates) inbound += g->cargo[si][k];
      const i64 burn = (s.burn0[k] * s.pop / s.basePop) * P.burnPct[(size_t)s.stage] / 100, prod = s.prod0[k] * s.pop / s.basePop;
      const i64 proj = s.stock[k] + (prod - burn) * etaOf[si] + inbound;
      i64 need = std::max<i64>(0, s.cap[k] * P.targetPct / 10000 - proj); if (s.owner == ABAN) need = 0;
      const int ro = W.S[r.hub].owner; const i64 prio = s.owner == ro ? 3 : (s.owner == IND || s.owner == HOUSE) ? 2 : 1, urgency = s.stage >= 2 ? 2 : 1;
      ents.push_back({(int)si, k}); w.push_back(need * prio * urgency); caps.push_back(need); } }
  const i64 reserve = P.hubReservePct, hold = P.freighterCap;
  A4 avail; for (int k = 0; k < 4; ++k) avail[k] = std::max<i64>(0, hub.stock[k] - hub.cap[k] * reserve / 10000);
  std::vector<i64> capsA = caps;
  for (int k = 0; k < 4; ++k) { std::vector<size_t> idx; i64 tot = 0; for (size_t i = 0; i < ents.size(); ++i) if (ents[i].second == k) { idx.push_back(i); tot += caps[i]; }
    if (tot > avail[k]) { std::vector<i64> ww, cc; for (size_t i : idx) { ww.push_back(w[i] ? w[i] : 1); cc.push_back(caps[i]); } const auto a = cappedApportion(ww, cc, avail[k]); for (size_t j = 0; j < idx.size(); ++j) capsA[idx[j]] = a[j]; } }
  const auto got = cappedApportion(w, capsA, hold);
  for (size_t i = 0; i < got.size(); ++i) { f.cargo[ents[i].first][ents[i].second] += got[i]; hub.stock[ents[i].second] -= got[i]; }
}

static void interceptCheck(World& W, Freighter& f, const Leg& lg) {
  const auto& P = W.P; RouteSt& rt = W.RT[f.r];
  const i64 lo = lg.from - P.pirSensor, hi = lg.from + lg.len + P.pirSensor;
  i64 load = f.raw[0] + f.raw[1] + f.raw[2]; for (const auto& c : f.cargo) load += c[0] + c[1] + c[2] + c[3];
  if (!load) return;
  for (auto& g : rt.groups) {
    if (g.s <= 0 || g.pos < lo || g.pos > hi || W.day - g.last < P.groupCooldown) continue;
    i64 def = P.freighterDef; std::vector<Unit*> near;
    for (size_t i = 0; i < rt.units.size(); ++i) { auto& u = rt.units[i]; if (u.s > 0 && std::abs(patrolPos(W, f.r, (i64)i, (i64)rt.units.size(), W.day) - g.pos) <= P.patrolRange) { def += u.s; near.push_back(&u); } }
    if (g.s <= def) continue;
    g.last = W.day;
    const i64 win = g.s * 10000 / (g.s + def);
    if (hashN(W.seed, K_ICPT, W.day, f.id) % 10000 < win) {
      i64 taken = 0, val = 0;
      for (auto& c : f.cargo) for (int k = 0; k < 4; ++k) { const i64 t = c[k] * P.lootPct / 100; c[k] -= t; taken += t; val += t * P.valuePerUnit[k]; }
      for (int t = 0; t < 3; ++t) { const i64 x = f.raw[t] * P.lootPct / 100; f.raw[t] -= x; W.led.rawLoot += x; val += x * P.rawValue; }
      W.led.loot += taken; rt.loot += val;
      for (auto* u : near) u->s -= u->s * P.patrolLossPct / 100;
    } else { g.s -= g.s * P.failLossPct / 100; for (auto* u : near) u->s -= u->s * P.huntLossPct / 100; }
    return;
  }
}

void freighterDay(World& W, Freighter& f) {
  if (f.dead) return;
  const auto& P = W.P; const Route& r = W.net.routes[f.r];
  if (--f.t > 0) return;
  if (f.leg >= 0) { const Leg& lg = r.legs[(size_t)f.leg];
    if (lg.stopIdx >= 0) { Station& s = W.S[lg.b]; auto& c = f.cargo[(size_t)lg.stopIdx]; i64 dval = 0;
      for (int k = 0; k < 4; ++k) { const i64 room = std::max<i64>(0, s.cap[k] - s.stock[k]), give = std::min(room, c[k]); s.stock[k] += give; c[k] -= give; f.ret[k] += c[k]; c[k] = 0; dval += give * P.valuePerUnit[k]; }
      W.RT[f.r].dv += dval; s.acct.dv += dval;
      const int a = W.S[r.hub].owner, b = s.owner; if (a != b && isAgent(a) && isAgent(b)) W.pw.trade[pk(a, b)] += dval; } }
  f.leg++;
  if (f.leg >= (int)r.legs.size()) { f.leg = -1; if (f.retire) { unload(W, f); f.dead = 1; return; } loadFreighter(W, f); f.t = P.dwell; return; }
  const Leg& lg = r.legs[(size_t)f.leg]; interceptCheck(W, f, lg); f.t = lg.len + (lg.stopIdx >= 0 ? P.dwell : 0);
}

static int desiredHulls(const World& W, const Route& r) {
  const auto& P = W.P; i64 n = r.base; const int ho = W.S[r.hub].owner; bool own = false; for (int x : r.stops) if (W.S[x].owner == ho) own = true;
  if (own) { const i64 num = r.len + P.dwell * (i64)r.stops.size(); n = std::max(n, (num + P.floorDays - 1) / P.floorDays); }
  if (ho == ABAN) n = 0;
  return (int)std::min(P.maxHulls, n);
}
void syncHulls(World& W) {
  for (auto& r : W.net.routes) {
    std::vector<size_t> live; for (size_t i = 0; i < W.F.size(); ++i) if (W.F[i].r == r.id && !W.F[i].dead && !W.F[i].retire) live.push_back(i);
    const int want = desiredHulls(W, r);
    for (int k = (int)live.size(); k < want; ++k) { Freighter f = mkFreighter(W.F, r, 0, W.P); f.t = 1; W.F.push_back(f); }
    for (int k = (int)live.size() - 1; k >= want; --k) W.F[live[(size_t)k]].retire = 1;
    r.freighters = want;
  }
}
CR stationCR(const World& W) {
  const auto& P = W.P; const size_t N = W.S.size(); CR cr; cr.cost.assign(N, 0); cr.ret.assign(N, 0); cr.dv.assign(N, 0); cr.oe.assign(N, 0);
  for (const auto& s : W.S) { cr.cost[s.id] = s.acct.dv; cr.dv[s.id] = s.acct.dv; cr.ret[s.id] = s.acct.rawOut + s.pop * P.taxBp / 10000; }
  for (size_t ri = 0; ri < W.net.routes.size(); ++ri) { const auto& r = W.net.routes[ri]; const i64 n = (i64)r.stops.size(), hull = hullsOn(W, (int)ri) * P.hullCostMonth, pat = W.RT[ri].P * P.patrolCostPct / 100; for (int x : r.stops) cr.cost[x] += (hull + pat) / n; }
  std::map<int, i64> cnt; for (const auto& s : W.S) if (isAgent(s.owner)) cnt[s.owner]++;
  for (const auto& s : W.S) { if (s.tier == 0 || !isAgent(s.owner)) continue; const i64 c = coreDist(W, s.owner, s.id) * P.oeDistK + cnt[s.owner] * P.oeCountK; cr.cost[s.id] += c; cr.oe[s.id] = c; }
  return cr;
}
static bool lastOf(const World& W, const Station& s) { for (const auto& y : W.S) if (&y != &s && y.owner == s.owner) return false; return true; }
void accounting(World& W, const CR& cr) {
  const auto& P = W.P; const i64 win = P.windowMonths;
  for (auto& s : W.S) {
    if (s.tier == 0 || s.owner == ABAN) { s.acct.dv = 0; s.acct.rawOut = 0; continue; }
    auto& a = s.acct; a.cost.push_back(cr.cost[s.id]); a.ret.push_back(cr.ret[s.id]); if ((i64)a.cost.size() > win) { a.cost.erase(a.cost.begin()); a.ret.erase(a.ret.begin()); }
    a.dv = 0; a.rawOut = 0; a.owned++;
    if ((i64)a.cost.size() < win || a.owned < win) continue;
    if (s.born > 0 && a.owned < P.graceMonths) continue;
    i64 cs = 0, rs = 0; for (auto v : a.cost) cs += v; for (auto v : a.ret) rs += v;
    if (isFac(s.owner) && cs * 100 > rs * P.dropPct && !lastOf(W, s)) {
      const int was = s.owner; auto fi = W.fac.find(was);
      if (fi != W.fac.end() && fi->second.base) fi->second.base = std::max<i64>(1, fi->second.base - depSum(s));
      s.owner = s.pop * 100 >= s.basePop * P.indPopPct ? IND : ABAN;
      s.dropped = W.day / 30; if (auto it = W.pw.ag.find(was); it != W.pw.ag.end()) it->second.drop[s.id] = s.dropped;
      if (s.owner == ABAN) s.stage = 3; a.owned = 0; a.cost.clear(); a.ret.clear();
    } else if (s.owner == IND && cs * 100 < rs * P.adoptPct) {
      for (const auto& rr : W.net.routes) { if (std::find(rr.stops.begin(), rr.stops.end(), s.id) == rr.stops.end() || !isFac(W.S[rr.hub].owner)) continue;
        s.owner = W.S[rr.hub].owner; if (auto fi = W.fac.find(s.owner); fi != W.fac.end()) fi->second.base += depSum(s); a.owned = 0; a.cost.clear(); a.ret.clear(); break; }
    }
  }
}

void rehop(World& W) {
  Net& net = W.net; std::vector<Pt> pts; for (const auto& x : net.st) pts.push_back(x.pos);
  std::map<int, int> jo; std::map<std::pair<int, int>, size_t> keep;
  for (size_t i = 0; i < net.lanes.size(); ++i) { const auto& l = net.lanes[i]; if (l.j >= 0 && !jo.count(l.j)) jo[l.j] = l.owner; if (!keep.count({l.a, l.b})) keep[{l.a, l.b}] = i; }
  auto nl = hopList(pts, W.P, net.hz);
  for (auto& l : nl) { const bool ja = std::find(net.junctions.begin(), net.junctions.end(), l.a) != net.junctions.end(), jb = std::find(net.junctions.begin(), net.junctions.end(), l.b) != net.junctions.end();
    l.j = ja ? l.a : jb ? l.b : -1; l.owner = l.j >= 0 && jo.count(l.j) ? jo[l.j] : -1; }
  std::vector<i64> flow(nl.size(), 0); for (size_t i = 0; i < nl.size(); ++i) { auto it = keep.find({nl[i].a, nl[i].b}); if (it != keep.end() && it->second < W.dark.flow.size()) flow[i] = W.dark.flow[it->second]; }
  W.dark.flow = flow; net.lanes = nl; W.pw.pathV++;
}

void survey(World& W) {
  const auto& P = W.P; Net& net = W.net; const i64 mo = W.day / 30;
  for (size_t fi = 0; fi < W.facIds.size(); ++fi) { const int f = W.facIds[fi]; Fac& F = W.fac[f];
    if (!F.base) continue;
    auto ai = W.pw.ag.find(f);
    if (ai != W.pw.ag.end() && ai->second.kind != K_BASE) continue;
    if (ai != W.pw.ag.end() && ai->second.T < 0) continue;
    if (ai != W.pw.ag.end() && !F.survey) { bool recent = false; for (auto& [x, m] : ai->second.drop) if (x >= W.n0 && mo - m < P.dropMemory) recent = true; if (recent) continue; }
    auto outposts = [&] { i64 n = 0; for (const auto& s : W.S) if (s.id >= W.n0 && s.owner == f) ++n; return n; };
    if (!F.survey) {
      i64 cur = 0; for (const auto& s : W.S) if (s.owner == f) cur += depSum(s);
      bool aban = false; for (const auto& s : W.S) if (s.owner == ABAN) aban = true;
      const bool room = ((i64)W.S.size() < W.n0 + P.maxStations - 12 && outposts() < P.maxOutposts) || aban;
      if (room && cur * 10000 < F.base * P.surveyT) { F.survey = 1; F.t = P.surveyMonths[0] + hashN(W.seed, K_SURV, f, mo) % (P.surveyMonths[1] - P.surveyMonths[0] + 1); }
      continue;
    }
    if (F.chart) { F.t = std::max<i64>(1, F.t - F.chart); F.chart = 0; F.charted = 1; }
    if (--F.t > 0) continue;
    F.survey = 0;
    std::vector<int> hubs; for (const auto& s : W.S) if (s.tier == 0 && s.owner == f) hubs.push_back(s.id);
    if (hubs.empty()) continue;
    auto nd = [&](int id) { i64 m = INT64_MAX; for (int h : hubs) m = std::min(m, net.dist[h][id]); return m; };
    std::vector<int> cand; for (const auto& s : W.S) { if (s.owner != ABAN) continue; i64 a = depSum(s), b = 0; for (const auto& d : s.dep) b += d.rate0; if (a * 100 >= b * P.reclaimMinPct) cand.push_back(s.id); }
    std::stable_sort(cand.begin(), cand.end(), [&](int a, int b) { const i64 x = nd(a), y = nd(b); return x != y ? x < y : a < b; });
    if (!cand.empty()) { Station& s = W.S[cand[0]]; s.owner = f; s.pop = s.basePop * P.foundPopPct / 100; s.dwell = 0; s.ema = worstRatio(s); if (F.charted) F.charted = 0;
      s.stage = s.ema < P.down[2] ? 3 : s.ema < P.down[1] ? 2 : s.ema < P.down[0] ? 1 : 0; s.leftNormal = s.stage ? W.day : -1;
      s.acct.owned = 0; s.acct.cost.clear(); s.acct.ret.clear(); F.base += depSum(s); continue; }
    if ((i64)W.S.size() >= W.n0 + P.maxStations - 12) continue;
    if (outposts() >= P.maxOutposts) continue;
    std::vector<int> own; for (const auto& s : W.S) if (s.owner == f && s.tier > 0) own.push_back(s.id);
    std::stable_sort(own.begin(), own.end(), [&](int a, int b) { const i64 x = nd(a), y = nd(b); return x != y ? x < y : a < b; });
    const int parent = own.empty() ? hubs[0] : own[0], id = (int)W.S.size();
    int64_t n = 0; auto rr = [&](i64 a, i64 b) { return a + hashN(W.seed, K_FOUND, id, n++) % (b - a + 1); };
    NetSt ns; ns.id = id; ns.tier = 2; ns.name = "Outpost-" + std::to_string(id - W.n0 + 1); ns.parent = parent; ns.owner = f; net.st.push_back(ns);
    { const i64 L0 = rr(14, 26); const auto dd = hashN(W.seed, K_FOUND, id, 99) % 64; const Pt pp = net.st[(size_t)parent].pos;
      extern Pt dir64pub(i64); const Pt d = dir64pub((i64)dd); net.st[(size_t)id].pos.x = clampI(pp.x + d.x * L0 / 1000, 2, net.side - 2); net.st[(size_t)id].pos.y = clampI(pp.y + d.y * L0 / 1000, 2, net.side - 2); net.st[(size_t)id].pos.ok = true; rehop(W); }
    floyd(net);
    Station s = mkStation(P, net.st[(size_t)id], W.seed); s.dep = genBodies(W.seed, net.st[(size_t)id], P, id + 1000);
    for (int t = 0; t < 3; ++t) { i64 a = 0; for (const auto& d : s.dep) if (d.t == t) a += d.rate0; s.rawCap[t] = std::max<i64>(1, a * P.rawCapDays); }
    s.pop = s.basePop * P.foundPopPct / 100; for (int k = 0; k < 4; ++k) s.stock[k] = s.cap[k] * 5000 / 10000; s.ema = 5000; s.born = W.day;
    for (auto v : s.stock) W.led.init += v;
    if (F.charted) F.charted = 0;
    { i64 b = 0; for (const auto& d : s.dep) b += d.rate0; W.S.push_back(s); W.fac[f].base += b; }
    std::stable_sort(hubs.begin(), hubs.end(), [&](int a, int b) { return net.dist[a][id] != net.dist[b][id] ? net.dist[a][id] < net.dist[b][id] : a < b; });
    Route r = buildRoute(net, hubs[0], {id}, P, false); net.routes.push_back(r);
    RouteSt rt; rt.tHist.assign((size_t)P.lagMonths + 1, 0); W.RT.push_back(rt);
    for (int k = 0; k < r.freighters; ++k) W.F.push_back(mkFreighter(W.F, net.routes.back(), k, P));
    W.bk.ds.push_back(0); W.bk.u.push_back(0); W.bk.kp.push_back(-1);
  }
}

}  // namespace apsis::world
