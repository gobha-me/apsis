// World creation, the monthly cycle and the day step (v0.20 create / monthEnd / step).
#include <algorithm>
#include <sstream>
#include "gen.hpp"
#include "ops.hpp"
#include "sim.hpp"
#include "../core/serial.hpp"
#include "intents.hpp"

namespace apsis::world {

World create(uint32_t seed, i64 majors, const Rules& rules, const WParams& P0) {
  World W; W.P = P0; W.P.majors = majors; W.rules = rules; W.seed = seed; const auto& P = W.P;
  Net net; std::vector<Station> S;
  for (int attempt = 0;; ++attempt) {
    const uint32_t ms = attempt ? hashN(seed, K_FAIR, attempt) : seed;
    net = genCoreMap(ms, P); S.clear(); for (const auto& s : net.st) S.push_back(mkStation(P, s, ms));
    placementRule(net, S, P, ms);
    i64 reb = 0; const bool ok = fairCheck(net, S, P, &reb);
    if (ok || attempt + 1 >= P.fairMaxTries) break;
  }
  for (auto& s : S) if (s.tier > 0) for (int t = 0; t < 3; ++t) { i64 a = 0; for (const auto& d : s.dep) if (d.t == t) a += d.rate0; s.rawCap[t] = std::max<i64>(1, a * P.rawCapDays); }
  { const i64 off = hashN(seed, K_SPEC) % 4, stp = net.sectorHomes.size() == 2 ? 2 : 1;
    for (int h : net.sectorHomes) { std::array<int, 4> perm; for (int k = 0; k < 4; ++k) perm[k] = (int)((k + h * stp + off) % 4);
      for (int k = 0; k < 4; ++k) S[h].prod0[k] = S[h].prod0[k] * P.specMult[(size_t)perm[k]] / 100; net.spec[h] = perm; } }
  struct Minor { int id, seat, patron; std::vector<int> held; }; std::vector<Minor> minors0;
  { int64_t n = 0; auto rr = [&](i64 k) { return (i64)(hashN(seed, K_PAT, n++) % k); }; const i64 nMin = (i64)net.majors.size(); int nid = 9;
    for (i64 i = 0; i < nMin; ++i) { std::vector<int> cand; for (const auto& x : S) if (x.tier == 2 && x.owner == IND) cand.push_back(x.id); if (cand.empty()) break;
      const int a = cand[(size_t)rr((i64)cand.size())]; const int id = nid++; std::vector<int> held{a}; S[a].owner = id; net.st[a].owner = id;
      if (rr(100) < P.minorPairPct) { std::vector<int> nb; for (const auto& l : net.lanes) { const int j = l.a == a ? l.b : l.b == a ? l.a : -1; if (j >= 0 && S[j].tier == 2 && S[j].owner == IND) nb.push_back(j); }
        std::sort(nb.begin(), nb.end()); if (!nb.empty()) { S[nb[0]].owner = id; net.st[nb[0]].owner = id; held.push_back(nb[0]); } }
      int pat = -1; i64 pd = 1000000000; for (int h : net.hubs) if (net.dist[h][a] < pd) { pd = net.dist[h][a]; pat = S[h].owner; }
      minors0.push_back({id, a, pat, held}); } }
  W.net = std::move(net); W.S = std::move(S); W.n0 = W.net.n0;
  for (const auto& r : W.net.routes) for (int k = 0; k < r.freighters; ++k) W.F.push_back(mkFreighter(W.F, r, k, P));
  for (size_t i = 0; i < W.net.routes.size(); ++i) { RouteSt rt; rt.tHist.assign((size_t)P.lagMonths + 1, 0); W.RT.push_back(rt); }
  for (int f : W.net.majors) { W.fac[f] = Fac{}; W.facIds.push_back(f); }
  for (const auto& s : W.S) for (auto v : s.stock) W.led.init += v;
  for (const auto& s : W.S) if (auto it = W.fac.find(s.owner); it != W.fac.end()) for (const auto& d : s.dep) it->second.base += d.rate0;
  auto& pw = W.pw;
  auto mk = [&](int id, int kind, i64 T) { Agent a; a.id = id; a.kind = kind; a.T = T; pw.ag[id] = a; return &pw.ag[id]; };
  for (int f : W.net.majors) mk(f, K_BASE, P.treasury0);
  mk(HOUSE, K_HOUSE, P.houseT0);
  pw.rel[pk(0, 1)] = P.relInit[0] + hashN(seed, K_SURV, 777) % (P.relInit[1] - P.relInit[0] + 1); pw.rel[pk(0, 4)] = 2000; pw.rel[pk(1, 4)] = 2000;
  for (size_t i = 0; i < W.net.majors.size(); ++i) { const int a = W.net.majors[i];
    for (size_t j = i + 1; j < W.net.majors.size(); ++j) { const int b = W.net.majors[j]; if (pk(a, b) != PK{0, 1}) pw.rel[pk(a, b)] = P.relInit[0] + hashN(seed, K_SURV, 777, a, b) % (P.relInit[1] - P.relInit[0] + 1); }
    if (a > 1) pw.rel[pk(a, HOUSE)] = 2000; }
  for (const auto& m : minors0) { Agent* a = mk(m.id, K_MINOR, P.minorT0); a->seat = m.seat; a->patron = m.patron; a->peak = (i64)m.held.size(); for (int f : W.net.majors) pw.rel[pk(f, m.id)] = f == m.patron ? P.patronRel : 0; }
  pw.nextId = minors0.empty() ? 9 : minors0.back().id + 1;
  pw.split = hashN(seed, K_SPLIT) % 100 < (uint32_t)P.splitPct ? 1 : 0;
  if (pw.split) { Agent* g = mk(GUILD, K_GUILD, P.treasury0); g->seat = -1; for (int f : W.net.majors) if (f != 0) pw.rel[pk(f, GUILD)] = 0; pw.rel[pk(0, GUILD)] = 0; }
  for (auto& l : W.net.lanes) if (l.j >= 0) l.owner = pw.split && (hashN(seed, K_SPLIT, 991 + l.j) % 2) ? GUILD : W.S[l.j].owner;
  for (int j : W.net.junctions) pw.jPrev[j] = W.S[j].owner;
  pw.majors0 = W.net.majors;
  const size_t N = W.S.size(); W.bk.ds.assign(N, 0); W.bk.u.assign(N, 0); W.bk.kp.assign(N, -1);
  return W;
}

static i64 patrolPos(const World& W, int r, i64 u, i64 n, i64 day) { const i64 L = W.net.routes[r].len, span = 2 * L, p = (span * u / std::max<i64>(1, n) + day) % span; return p <= L ? p : span - p; }
static void monthEnd(World& W) {
  const auto& P = W.P; const i64 mo = W.day / 30;
  for (size_t ri = 0; ri < W.net.routes.size(); ++ri) { const Route& r = W.net.routes[ri]; RouteSt& rt = W.RT[ri];
    for (size_t i = 0; i < rt.units.size(); ++i) { auto& u = rt.units[i]; const i64 a = patrolPos(W, (int)ri, (i64)i, (i64)rt.units.size(), W.day - 30), b = patrolPos(W, (int)ri, (i64)i, (i64)rt.units.size(), W.day);
      const i64 lo = std::min(a, b) - P.patrolRange, hi = std::max(a, b) + P.patrolRange;
      for (auto& g : rt.groups) if (g.s > 0 && u.s > 0 && g.pos >= lo && g.pos <= hi) { const i64 kill = std::min(g.s, u.s * P.huntKillPct / 100); g.s -= kill; u.s -= kill * P.huntLossPct / 100; } }
    i64 Psurv = 0, Q = 0; for (const auto& u : rt.units) Psurv += std::max<i64>(0, u.s); for (const auto& g : rt.groups) Q += std::max<i64>(0, g.s);
    rt.V[0] = rt.V[1]; rt.V[1] = rt.V[2]; rt.V[2] = rt.dv; const i64 V = (rt.V[0] + rt.V[1] + rt.V[2]) / 3, target = V * P.fundPct / 100;
    rt.tHist.erase(rt.tHist.begin()); rt.tHist.push_back(target);
    const i64 want = Psurv + (target - Psurv) / P.smoothDiv, cmax = std::max(P.capAbs, Psurv * P.capPct / 100);
    i64 Pn = std::max<i64>(0, Psurv + clampI(want - Psurv, -cmax, cmax));
    const i64 upkeep = Q * P.upkeepPct / 100; Q += (rt.loot - upkeep) * P.growthPct / 100; Q += V * P.lurePct / 100;
    { const i64 Q0 = rt.Q, c = std::max(P.pirCapAbs, Q0 * P.pirCapPct / 100); if (Q > Q0 + c) Q = Q0 + c; }
    if (V == 0) Q -= Q / 4; Q = std::max<i64>(0, Q);
    rt.P = Pn; rt.Q = Q; rt.dv = 0; rt.loot = 0;
    const i64 nu = std::min<i64>(12, Pn / P.patrolUnit + (Pn % P.patrolUnit ? 1 : 0)); const auto su = apportion(std::vector<i64>((size_t)nu, 1), Pn);
    rt.units.clear(); for (auto s : su) rt.units.push_back(Unit{s});
    const i64 ng = std::min<i64>(10, Q / P.pirGroup + (Q % P.pirGroup ? 1 : 0)); const auto sg = apportion(std::vector<i64>((size_t)ng, 1), Q);
    rt.groups.clear(); for (size_t g = 0; g < sg.size(); ++g) rt.groups.push_back(Group{sg[g], (i64)(hashN(W.seed, K_POS, ri, mo, g) % (r.len + 1)), -1000}); }
  tradeMonth(W);
  mapMonthJ(W);
  darkMonth(W);
  const CR cr = stationCR(W); W.crCost = cr.cost; W.crRet = cr.ret; W.crDv = cr.dv; W.crOe = cr.oe; W.crSet = 1;
  accounting(W, cr);
  survey(W);
  power(W, cr);
  syncHulls(W);
}

void step(World& W) {
  for (auto& s : W.S) stationDay(W, s);
  for (size_t i = 0; i < W.F.size(); ++i) freighterDay(W, W.F[i]);
  W.day++;
  if (W.day % 30 == 0) monthEnd(W);
  if (W.rules.intents) factionDay(W);
}

bool ledgerOk(const World& W) {
  i64 stocks = 0, cargo = 0, rawHeld = 0; for (const auto& s : W.S) { for (auto v : s.stock) stocks += v; rawHeld += s.raw[0] + s.raw[1] + s.raw[2]; }
  for (const auto& f : W.F) { for (auto v : f.ret) cargo += v; for (const auto& c : f.cargo) cargo += c[0] + c[1] + c[2] + c[3]; rawHeld += f.raw[0] + f.raw[1] + f.raw[2]; }
  const auto& L = W.led;
  return L.init + L.prod - L.burn - L.spill - L.waste - L.loot == stocks + cargo && L.rawExt == rawHeld + L.rawConv + L.rawLoot + L.rawSpill;
}

std::string dump(const World& W) {
  std::ostringstream o; auto arr = [&](const auto& a) { std::string s; for (size_t i = 0; i < a.size(); ++i) s += (i ? "," : "") + std::to_string(a[i]); return s; };
  o << "day " << W.day << "\n";
  for (const auto& s : W.S) { o << "S" << s.id << " o" << s.owner << " t" << s.tier << " p" << s.pop << " k" << arr(s.stock) << " g" << s.stage << " m" << s.mood << " e" << s.ema << " w" << s.wear << " r" << arr(s.raw);
    std::vector<i64> res; for (const auto& d : s.dep) res.push_back(d.res); o << " d" << arr(res) << "\n"; }
  for (const auto& f : W.F) { i64 c = 0; for (const auto& x : f.cargo) c += x[0] + x[1] + x[2] + x[3]; o << "F" << f.id << " r" << f.r << " l" << f.leg << " t" << f.t << " c" << c << " x" << f.dead << f.retire << "\n"; }
  for (size_t i = 0; i < W.RT.size(); ++i) { const auto& r = W.RT[i]; o << "R" << i << " P" << r.P << " Q" << r.Q << " g"; for (const auto& g : r.groups) o << g.s << "@" << g.pos << ";"; o << " u"; for (const auto& u : r.units) o << u.s << ";"; o << "\n"; }
  for (const auto& [id, a] : W.pw.ag) o << "A" << id << " k" << a.kind << " T" << a.T << " S" << a.S << " a" << a.alive << " s" << a.seat << " p" << a.patron << " c" << a.crush << "\n";
  for (const auto& [k, v] : W.pw.rel) o << "rel" << k.first << "," << k.second << " " << v << "\n";
  for (const auto& c : W.pw.con) o << "C" << c.x << " " << c.a << ">" << c.d << " m" << c.m << " g" << c.margin << "\n";
  o << "rate"; for (const auto& [k, v] : W.pw.rate) o << " " << k << ":" << v; o << "\ncontract"; for (const auto& [k, v] : W.pw.contract) o << " " << k << ":" << v.carrier << "/" << v.until; o << "\n";
  o << "lanes " << W.net.lanes.size() << " own"; for (const auto& l : W.net.lanes) o << " " << l.owner; o << "\n";
  o << "ds " << arr(W.bk.ds) << "\nu " << arr(W.bk.u) << "\nkp " << arr(W.bk.kp) << "\n";
  o << "ghost"; for (size_t i = 0; i < W.dark.g.size(); ++i) if (W.dark.g[i]) o << " " << i << ":" << W.dark.g[i]; o << "\nedge"; for (const auto& [s, h] : W.dark.edge) o << " " << s << ":" << h; o << "\n";
  for (const auto& [f, F] : W.fac) o << "fac" << f << " b" << F.base << " s" << F.survey << " t" << F.t << "\n";
  const auto& L = W.led; o << "led " << L.init << " " << L.prod << " " << L.burn << " " << L.spill << " " << L.waste << " " << L.loot << " " << L.rawExt << " " << L.rawConv << " " << L.rawLoot << " " << L.rawSpill << "\n";
  return o.str();
}
std::string hashState(const World& W) { const std::string d = dump(W); return hashBytes(std::vector<uint8_t>(d.begin(), d.end())); }
int generalWarPairs(const World& W, int* pairs) {
  std::vector<int> gs; for (const auto& [id, a] : W.pw.ag) if (a.alive && a.kind == K_BASE) gs.push_back(id);
  int n = 0; for (size_t i = 0; i < gs.size(); ++i) for (size_t k = i + 1; k < gs.size(); ++k) if (hostile(W, gs[i], gs[k])) ++n;
  if (pairs) *pairs = (int)(gs.size() * (gs.size() - 1) / 2); return n;
}

}  // namespace apsis::world
