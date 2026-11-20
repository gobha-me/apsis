// Houses and power (v0.20 power layer, minors, falls, tolls and trade), with intents off.
#include <algorithm>
#include "gen.hpp"
#include "ops.hpp"
#include "intents.hpp"

namespace apsis::world {
namespace {
i64 crv(const std::vector<i64>& v, int i) { return i >= 0 && (size_t)i < v.size() ? v[(size_t)i] : 0; }
Agent* ag(World& W, int id) { auto it = W.pw.ag.find(id); return it == W.pw.ag.end() ? nullptr : &it->second; }
const Agent* agc(const World& W, int id) { auto it = W.pw.ag.find(id); return it == W.pw.ag.end() ? nullptr : &it->second; }
bool isJunction(const World& W, int x) { return std::find(W.net.junctions.begin(), W.net.junctions.end(), x) != W.net.junctions.end(); }
}

i64 relOf(const World& W, int a, int b) { auto it = W.pw.rel.find(pk(a, b)); return it == W.pw.rel.end() ? 0 : it->second; }
void relAdd(World& W, int a, int b, i64 d) { if (!isAgent(a) || !isAgent(b) || a == b) return; auto& v = W.pw.rel[pk(a, b)]; const i64 n = clampI(v + d, -10000, 10000); W.pw.relVol += std::abs(n - v); v = n; }
std::vector<int> aliveIds(const World& W) { std::vector<int> v; for (const auto& [id, a] : W.pw.ag) if (a.alive) v.push_back(id); return v; }
bool hostile(const World& W, int a, int b) { if (a == b || !isAgent(a) || !isAgent(b)) return false; if (relOf(W, a, b) <= W.P.contestRelT) return true;
  for (const auto& c : W.pw.con) if ((c.a == a && c.d == b) || (c.a == b && c.d == a)) return true; return false; }

static i64 project(const World& W, const Agent& b, int x) { return b.S * 100 / (100 + coreDist(W, b.id, x) * W.P.presK); }
static const Agent* patronOf(const World& W, int o) { const Agent* a = agc(W, o); if (!a || a->kind == K_BASE || a->kind == K_HOUSE || a->patron < 0) return nullptr; const Agent* p = agc(W, a->patron); return p && p->alive && p->kind == K_BASE ? p : nullptr; }
static i64 protection(const World& W, int o, int x) { const Agent* p = patronOf(W, o); return p ? project(W, *p, x) * W.P.protectPct / 100 : 0; }
i64 presence(const World& W, const Agent& a, int x) {
  const auto& P = W.P; const Station& s = W.S[x]; const i64 mo = W.day / 30;
  auto fortOn = [&] { auto it = a.fort.find(x); return it != a.fort.end() && it->second && it->second > mo; };
  if (s.owner == a.id && isJunction(W, x)) { i64 v = std::max<i64>(1, s.pop / P.strengthPopDiv); v += project(W, a, x) * P.junctionProjPm / 1000; if (fortOn()) v = v * (100 + P.fortPct) / 100; return v; }
  i64 d = 1000000000;
  if (s.owner == a.id) d = coreDist(W, a.id, x); else for (const auto& y : W.S) if (y.owner == a.id) d = std::min(d, W.net.dist[y.id][x]);
  if (d > P.hopNpc && s.owner != a.id) return 0;
  if (d == 1000000000) return 0;
  i64 v = a.S * 100 / (100 + d * P.presK);
  if (s.owner == a.id) { v = v * (100 + P.homePct) / 100; if (fortOn()) v = v * (100 + P.fortPct) / 100; }
  return v;
}
i64 defence(const World& W, int x) { const Station& s = W.S[x]; const int o = s.owner; if (o == IND) return std::max<i64>(1, s.pop / W.P.indMilDiv); if (o == ABAN) return 1; const Agent* a = agc(W, o); return a && a->alive ? std::max<i64>(1, presence(W, *a, x) + protection(W, o, x)) : 1; }
static int choosePatron(const World& W, const Agent& a, int exclude) {
  int x = -1; if (a.seat >= 0 && W.S[a.seat].owner == a.id) x = a.seat; else for (const auto& y : W.S) if (y.owner == a.id) { x = y.id; break; }
  if (x < 0) return -1; int best = -1; i64 bv = -1;
  for (const auto& [id, b] : W.pw.ag) if (b.alive && b.kind == K_BASE && b.id != a.id && b.id != exclude && relOf(W, a.id, b.id) > -3000) { const i64 v = project(W, b, x); if (v > bv) { bv = v; best = b.id; } }
  return best;
}
static void setPatron(World& W, Agent& a, int p) { a.patron = p; if (p >= 0) { auto& v = W.pw.rel[pk(a.id, p)]; v = std::max(v, W.P.patronRel); } }
bool gate(const World& W, int f, int o) { if (o == IND || o == ABAN) return true; const Agent* p = patronOf(W, o); if (p && p->id != f && relOf(W, f, p->id) >= W.P.contestRelT) return false; return relOf(W, f, o) < W.P.contestRelT; }

void transfer(World& W, Station& s, int to, int kind /*0 other, 1 dropped-kind*/) {
  const int was = s.owner; if (was == to) return;
  if (auto it = W.fac.find(was); it != W.fac.end() && it->second.base) it->second.base = std::max<i64>(1, it->second.base - depSum(s));
  if (auto it = W.fac.find(to); it != W.fac.end()) it->second.base += depSum(s);
  s.owner = to; s.acct.owned = 0; s.acct.cost.clear(); s.acct.ret.clear();
  if (kind == 1) { s.dropped = W.day / 30; if (Agent* a = ag(W, was)) a->drop[s.id] = s.dropped; }
  std::erase_if(W.pw.con, [&](const Contest& c) { return c.x == s.id; });
  if (Agent* a = ag(W, to); a && a->seat < 0) a->seat = s.id;
}
void startContest(World& W, const Agent& A, int x) {
  const auto& P = W.P; const Station& s = W.S[x]; const int f = A.id; Contest c; c.uid = ++W.pw.conUid; c.x = x; c.a = f; c.d = s.owner;
  c.luck = (i64)(hashN(W.seed, K_FOUND, 4243, x, W.day / 30) % 201) - 100; c.min = s.tier == 0 ? P.coreMinDur : P.minDur; c.core = s.tier == 0 ? 1 : 0;
  W.pw.con.push_back(c); W.pw.contests++;
  relAdd(W, f, s.owner, -P.relStartHit); if (const Agent* p = patronOf(W, s.owner); p && p->id != f) relAdd(W, f, p->id, -(P.relStartHit / 2));
}
bool canTarget(const World& W, const Agent& a, const Station& s, int owner, const CR* cr) {
  const auto& P = W.P; const i64 mo = W.day / 30;
  if (owner == a.id || owner == ABAN || !gate(W, a.id, owner)) return false;
  for (const auto& c : W.pw.con) if (c.x == s.id) return false;
  if (const Agent* o = agc(W, owner); o && o->patron == a.id && o->kind != K_BASE) return false;
  if (a.kind != K_BASE && a.patron == owner) return false;
  if (auto it = a.drop.find(s.id); it != a.drop.end() && mo - it->second < P.dropMemory) return false;
  if (cr && s.tier > 0) { i64 n = 1; for (const auto& y : W.S) if (y.owner == a.id) ++n;
    const i64 cost = crv(cr->cost, s.id) - crv(cr->oe, s.id) + coreDist(W, a.id, s.id) * P.oeDistK + n * P.oeCountK;
    if (cost * 100 > crv(cr->ret, s.id) * (isFac(owner) || owner == HOUSE ? P.takeFacPctHouses : P.adoptPct)) return false; }
  return true;
}
struct Target { int x = -1; i64 score = 0; };
Target bestTarget(const World& W, const Agent& a, const CR& cr) {
  const auto& P = W.P; Target best; int mine = 0; for (const auto& c : W.pw.con) if (c.a == a.id) ++mine; if (mine >= P.maxContests) return best;
  for (const auto& s : W.S) { if (!canTarget(W, a, s, s.owner, &cr)) continue; const i64 pa = presence(W, a, s.id); if (!pa) continue;
    const i64 ratio = pa * 100 / defence(W, s.id), need = s.tier == 0 ? P.coreRatioT : isFac(s.owner) ? P.ratioFacT : P.ratioT; if (ratio <= need) continue;
    const i64 score = std::min<i64>(300, ratio - need) + (s.tier == 0 ? 50 : 0);
    if (best.x < 0 || score > best.score || (score == best.score && s.id < best.x)) { best.x = s.id; best.score = score; } }
  return best;
}

// ---------- tolls and trade ----------
static i64 rateOf(const World& W, int o) { auto it = W.pw.rate.find(o); return it != W.pw.rate.end() ? it->second : W.P.tollBase; }
i64 laneToll(const World& W, const Lane& l, int importer) {
  if (l.owner < 0 || l.owner == importer) return 0; const Agent* o = agc(W, l.owner); if (!o || !o->alive) return 0;
  if (l.j >= 0 && (size_t)l.j < W.bk.ds.size() && W.bk.ds[(size_t)l.j]) return 0;
  i64 r = rateOf(W, l.owner); auto c = W.pw.contract.find(importer);
  if (W.pw.split && c != W.pw.contract.end() && c->second.carrier != l.owner && (l.owner == 0 || l.owner == GUILD)) r = r * W.P.spotPct / 100;
  return r;
}
static std::vector<int> tradePath(const World& W, int a, int b, int importer) {
  const Net& net = W.net; const int N = (int)W.S.size(); const auto& P = W.P;
  std::vector<std::vector<int>> adj((size_t)N); for (size_t i = 0; i < net.lanes.size(); ++i) { const auto& l = net.lanes[i]; if (l.a < N && l.b < N) { adj[l.a].push_back((int)i); adj[l.b].push_back((int)i); } }
  const i64 INF = 1000000000000LL; std::vector<i64> d((size_t)N, INF); std::vector<int> via((size_t)N, -1); std::vector<char> done((size_t)N, 0); d[a] = 0;
  for (;;) { int u = -1; for (int i = 0; i < N; ++i) if (!done[i] && d[i] < INF && (u < 0 || d[i] < d[u])) u = i; if (u < 0 || u == b) break; done[u] = 1;
    for (int li : adj[u]) { const auto& l = net.lanes[(size_t)li]; const int v = l.a == u ? l.b : l.a;
      const i64 w = (laneDarkI(W, li) ? l.len * 100 * P.darkTradePct / 100 : l.len * 100) * (l.lng ? 2 : 1) + laneToll(W, l, importer) * P.tollDist + l.hw;
      if (d[u] + w < d[v]) { d[v] = d[u] + w; via[v] = li; } } }
  std::vector<int> path; for (int x = b; x != a && via[x] >= 0;) { const auto& l = net.lanes[(size_t)via[x]]; path.push_back(via[x]); x = l.a == x ? l.b : l.a; }
  return path;
}
static int laneIdx(const World& W, int li) { const auto& L = W.net.lanes; const auto& l = L[(size_t)li]; for (size_t i = 0; i < L.size(); ++i) if (L[i].a == l.a && L[i].b == l.b && L[i].len == l.len) return (int)i; return -1; }
void tradeMonth(World& W) {
  const auto& P = W.P; auto& pw = W.pw; const Net& net = W.net;
  std::vector<int> homes; for (int h : net.sectorHomes) if (W.S[h].owner != ABAN) homes.push_back(h);
  std::map<std::string, std::vector<int>> cache; if (pw.pathsV != pw.pathV) { pw.paths.clear(); pw.pathsV = pw.pathV; }
  for (int k = 0; k < 4; ++k) {
    std::vector<int> by = homes; std::stable_sort(by.begin(), by.end(), [&](int a, int b) { const i64 ra = W.S[a].stock[k] * 10000 / W.S[a].cap[k], rb = W.S[b].stock[k] * 10000 / W.S[b].cap[k]; return ra != rb ? ra > rb : a < b; });
    for (size_t i = 0; i < (by.size() >> 1); ++i) {
      Station& hi = W.S[by[i]]; Station& lo = W.S[by[by.size() - 1 - i]];
      const i64 gap = hi.stock[k] * 10000 / hi.cap[k] - lo.stock[k] * 10000 / lo.cap[k];
      const i64 amt = std::min(gap * std::min(hi.cap[k], lo.cap[k]) / 10000 * P.tradePct / 100, lo.cap[k] - lo.stock[k]);
      if (amt <= 0) continue;
      hi.stock[k] -= amt; lo.stock[k] += amt;
      const int imp = lo.owner; auto ci = pw.contract.find(imp);
      const std::string key = std::to_string(hi.id) + ">" + std::to_string(lo.id) + ":" + std::to_string(imp) + ":" + std::to_string(ci != pw.contract.end() ? ci->second.carrier : -1) + ":" + std::to_string(net.lanes.size()) + ":" + std::to_string(pw.pathV);
      std::vector<int>* path; if (auto it = cache.find(key); it != cache.end()) path = &it->second; else { auto jt = pw.paths.find(key); if (jt == pw.paths.end()) jt = pw.paths.emplace(key, tradePath(W, hi.id, lo.id, imp)).first; path = &cache.emplace(key, jt->second).first->second; }
      if (W.dark.flow.size() < net.lanes.size()) W.dark.flow.resize(net.lanes.size(), 0);
      for (int li : *path) { const int x = laneIdx(W, li); if (x >= 0) W.dark.flow[(size_t)x] += amt; }
      for (int li : *path) { const Lane& l = net.lanes[(size_t)li]; const i64 r = laneToll(W, l, imp); if (!r) continue; const i64 t = amt * r / 100; if (!t) continue;
        pw.tollAcc[l.owner] += t; pw.tollAcc[imp] -= t; pw.paid[pk(hi.id, lo.id)] += t; }
    }
  }
}
static PathSig sig(const World& W) { PathSig s; for (const auto& l : W.net.lanes) s.owners.push_back(l.owner); s.rate = W.pw.rate; s.contract = W.pw.contract; for (const auto& [id, a] : W.pw.ag) s.alive.push_back(a.alive ? 1 : 0); return s; }
static void tollYear(World& W) {
  const auto& P = W.P; auto& pw = W.pw; Net& net = W.net; const i64 mo = W.day / 30; const PathSig before = sig(W);
  for (int j : net.junctions) { const int o = W.S[j].owner; if (o != pw.jPrev[j]) { pw.jPrev[j] = o; const Agent* a = agc(W, o); const bool keep = isAgent(o) && o != HOUSE && a && a->alive; for (auto& l : net.lanes) if (l.j == j) l.owner = keep ? o : -1; } }
  for (auto& l : net.lanes) if (l.owner >= 0) { const Agent* a = agc(W, l.owner); if (!a || !a->alive) l.owner = -1; }
  if (pw.split) { Agent* g = ag(W, GUILD); bool any = false; for (const auto& l : net.lanes) if (l.owner == GUILD) any = true; if (g && g->alive && !any) { g->alive = 0; g->died = mo; } }
  auto carriers = [&] { std::vector<int> car; for (int c : {0, GUILD}) { const Agent* a = agc(W, c); if (!a || !a->alive) continue; bool any = false; for (const auto& l : net.lanes) if (l.owner == c) any = true; if (any) car.push_back(c); } return car; };
  if (mo % 12 == 4 && pw.split && !W.rules.intents) { const auto car = carriers();
    auto n = [&](int c) { i64 k = 0; for (const auto& [x, ct] : pw.contract) if (ct.carrier == c) ++k; return k; };
    if (car.size() == 2) for (int c : car) { const int o = car[0] == c ? car[1] : car[0]; const i64 r = rateOf(W, c); const i64 diff = n(c) - n(o); const int t = diff > 0 ? 1 : diff < 0 ? -1 : 0;
      auto lt = pw.trend.find(c); const int last = lt == pw.trend.end() ? 0 : lt->second; if (t != 0 && t == last) pw.rate[c] = clampI(r + t * P.tollStep, P.tollFloor, P.tollMax); pw.trend[c] = t; } }
  if (mo % 12 == 5 && pw.split && !W.rules.intents) { const auto car = carriers();
    for (int h : net.sectorHomes) { const int x = W.S[h].owner; const Agent* a = agc(W, x); if (!a || !a->alive || a->kind == K_GUILD || std::find(car.begin(), car.end(), x) != car.end()) continue;
      auto ci = pw.contract.find(x); std::vector<int> cs = car; std::stable_sort(cs.begin(), cs.end(), [&](int p, int q) { const i64 rp = rateOf(W, p), rq = rateOf(W, q); return rp != rq ? rp < rq : p < q; });
      if (cs.empty()) { if (ci != pw.contract.end()) pw.contract.erase(ci); continue; }
      const int cheapest = cs[0];
      if (ci == pw.contract.end() || std::find(car.begin(), car.end(), ci->second.carrier) == car.end()) { const i64 len = ci == pw.contract.end() ? P.firstContract[0] + hashN(W.seed, K_SPLIT, 77, x) % (P.firstContract[1] - P.firstContract[0] + 1) : P.contractMonths;
        pw.contract[x] = Contract{cheapest, mo + len}; continue; }
      if (mo < ci->second.until) continue;
      if (cheapest != ci->second.carrier && rateOf(W, cheapest) * 100 <= rateOf(W, ci->second.carrier) * (100 - P.switchMarginPct)) pw.contract[x] = Contract{cheapest, mo + P.contractMonths};
      else ci->second.until = mo + P.contractMonths; } }
  const PathSig after = sig(W);
  if (!(after == before) || !pw.hasPathSig || !(pw.pathSig == before)) { pw.pathV++; pw.pathSig = sig(W); pw.hasPathSig = true; }
}

// ---------- decisions, contests, wildcards ----------
static void decide(World& W, Agent& a, const CR& cr) {
  const auto& P = W.P; auto& pw = W.pw; const i64 mo = W.day / 30;
  struct Opt { int k; i64 score; int x; int o; }; std::vector<Opt> opts;
  if (a.T < 0) { std::vector<int> own; for (const auto& s : W.S) if (s.owner == a.id && s.tier > 0) own.push_back(s.id);
    std::stable_sort(own.begin(), own.end(), [&](int x, int y) { const i64 vx = crv(cr.ret, x) - crv(cr.cost, x), vy = crv(cr.ret, y) - crv(cr.cost, y); return vx != vy ? vx < vy : x < y; });
    if ((int)own.size() > (a.kind != K_BASE ? 1 : 0)) opts.push_back({0, 120 + std::min<i64>(200, -a.T / 500), own[0], 0}); }
  if (a.T > P.fortCost) { int wx = -1; i64 wt = 0;
    for (const auto& s : W.S) { if (s.owner != a.id || s.tier <= 0) continue; auto f = a.fort.find(s.id); if (f != a.fort.end() && f->second > mo) continue;
      i64 th = 0; for (const auto& c : pw.con) if (c.x == s.id) th = 2; i64 ext = 0;
      for (int bid : aliveIds(W)) { const Agent& b = pw.ag.at(bid); if (b.id != a.id && b.kind != K_HOUSE && gate(W, b.id, a.id) && presence(W, b, s.id) * 100 > presence(W, a, s.id) * P.ratioT) ext = 1; }
      const i64 t = th + ext; if (t && (wx < 0 || t > wt)) { wx = s.id; wt = t; } }
    if (wx >= 0) opts.push_back({1, 90 + wt * 20, wx, 1}); }
  if (a.T > P.contestCost * (P.minDur / 2)) { const Target t = bestTarget(W, a, cr); if (t.x >= 0) opts.push_back({2, 100 + t.score, t.x, 2}); }
  i64 outp = 0; for (const auto& s : W.S) if (s.id >= W.n0 && s.owner == a.id) ++outp;
  const bool room = (i64)W.S.size() < W.n0 + P.maxStations - 12 && outp < P.maxOutposts;
  bool burnt = false; for (auto& [x, m] : a.drop) if (x >= W.n0 && mo - m < P.dropMemory) burnt = true;
  auto fi = W.fac.find(a.id);
  if (room && !burnt && a.kind == K_BASE && fi != W.fac.end() && !fi->second.survey && a.T > P.expandT) opts.push_back({3, 80 + std::min<i64>(100, (a.T - P.expandT) / 2000), -1, 3});
  opts.push_back({4, 60, -1, 4});
  std::stable_sort(opts.begin(), opts.end(), [](const Opt& p, const Opt& q) { return p.score != q.score ? p.score > q.score : p.o < q.o; });
  const Opt c = opts[0];
  if (c.k == 0) { Station& s = W.S[c.x]; transfer(W, s, s.pop * 100 >= s.basePop * P.indPopPct ? IND : ABAN, 1); }
  else if (c.k == 1) { a.T -= P.fortCost; a.fort[c.x] = mo + P.fortMonths; }
  else if (c.k == 2) startContest(W, a, c.x);
  else if (c.k == 3) { Fac& F = W.fac[a.id]; F.survey = 1; F.t = P.surveyMonths[0]; a.T -= P.expandT / 4; }
}

static void houseYear(World& W, const CR&) {
  const auto& P = W.P; auto& pw = W.pw; const i64 mo = W.day / 30;
  if (mo % P.patronEvery == 3) for (int id : aliveIds(W)) { Agent& a = pw.ag.at(id);
    if (a.kind == K_BASE || a.kind == K_HOUSE || a.kind == K_GUILD) continue;
    if (!(a.seat >= 0 && W.S[a.seat].owner == a.id)) for (const auto& z : W.S) if (z.owner == a.id) { a.seat = z.id; break; }
    if (W.rules.intents) continue;   // intents mode: the patron choice is an intent
    const Agent* cur = patronOf(W, a.id); const int best = choosePatron(W, a, -1);
    if (!cur) { if (best >= 0 || a.patron >= 0) setPatron(W, a, best); }
    else if (best >= 0 && best != cur->id && project(W, pw.ag.at(best), a.seat) * 100 > project(W, *cur, a.seat) * P.switchPct) { relAdd(W, a.id, cur->id, -2000); setPatron(W, a, best); }
  }
  if (mo % 12 == 9) for (int id : aliveIds(W)) { Agent& a = pw.ag.at(id);
    if (a.kind == K_HOUSE || a.kind == K_GUILD) continue;
    i64 n = 0, homes = 0; for (const auto& x : W.S) if (x.owner == a.id) { ++n; if (x.tier == 0) ++homes; }
    if (a.kind == K_BASE && a.id == 0 && W.S[W.net.coreHome].owner == 0) { const bool cond = n <= P.fallMax || (homes == 0 && n <= P.fallNoHomeMax);
      a.fallC = 0; a.weakC = cond ? a.weakC + 1 : 0; if (!a.weak && a.weakC >= P.statusYears) a.weak = 1; else if (a.weak && !cond) a.weak = 0; }
    else if (a.kind == K_BASE) { a.fallC = (n <= P.fallMax || (homes == 0 && n <= P.fallNoHomeMax)) ? a.fallC + 1 : 0;
      if (a.fallC >= P.statusYears) { a.kind = K_MINOR; a.fallC = 0; a.fell++; pw.falls++; setPatron(W, a, choosePatron(W, a, -1)); } }
    else { a.riseC = ((homes >= 1 && n >= P.riseHome) || n >= P.riseMin) ? a.riseC + 1 : 0;
      if (a.riseC >= P.statusYears) { a.kind = K_BASE; a.riseC = 0; a.rose++; a.patron = -1; pw.rises++;
        if (!W.fac.count(a.id)) { i64 b = 0; for (const auto& x : W.S) if (x.owner == a.id) b += depSum(x); W.fac[a.id] = Fac{std::max<i64>(1, b), 0, 0, 0, 0}; W.facIds.push_back(a.id); } } }
  }
}

void power(World& W, const CR& cr) {
  const auto& P = W.P; auto& pw = W.pw; const i64 mo = W.day / 30; const Net& net = W.net;
  const std::vector<int> live = aliveIds(W);
  std::map<int, i64> flow; auto add = [&](int o, i64 v) { const Agent* a = agc(W, o); if (a && a->alive) flow[o] += v; };
  for (const auto& s : W.S) add(s.owner, crv(cr.ret, s.id) - crv(cr.oe, s.id));
  for (size_t ri = 0; ri < net.routes.size(); ++ri) { const auto& r = net.routes[ri]; const int ro = W.S[r.hub].owner; const i64 n = (i64)r.stops.size();
    const i64 share = (hullsOn(W, (int)ri) * P.hullCostMonth + W.RT[ri].P * P.patrolCostPct / 100) / std::max<i64>(1, n);
    for (int x : r.stops) { const int o = W.S[x].owner; const i64 v = crv(cr.dv, x); add(ro, -share); if (o != ro) { const i64 fee = v + share * P.servicePct / 100; add(ro, fee); add(o, -fee); } } }
  for (auto& [o, v] : pw.tollAcc) add(o, v); pw.tollAcc.clear();
  for (const auto& l : net.lanes) if (l.owner >= 0) add(l.owner, -P.laneUpkeep);
  for (int id : live) { const Agent* p = patronOf(W, id); if (p && flow[id] > 0) { const i64 t = flow[id] * P.tributePct / 100; flow[id] -= t; flow[p->id] += t; } }
  for (int id : live) { Agent& a = pw.ag.at(id); i64 pop = 0, cores = 0, n = 0;
    for (const auto& s : W.S) if (s.owner == a.id) { pop += s.pop; ++n; if (s.tier == 0) ++cores; }
    a.T = std::max<i64>(-500000, a.T + flow[id]); if (a.T > 0) a.T -= a.T / P.tDecay; a.n = n; a.peak = std::max(a.peak, n);
    a.S = std::max<i64>(0, pop / P.strengthPopDiv + std::min(P.tCapHouses, std::max<i64>(0, a.T)) / P.strengthTDivHouses + cores * P.coreStrength); }
  { std::map<PK, i64> near; const size_t N = W.S.size();
    for (size_t x = 0; x < N; ++x) for (size_t y = x + 1; y < N; ++y) { const int ox = W.S[x].owner, oy = W.S[y].owner; if (ox != oy && net.dist[x][y] <= P.hopNpc) near[pk(ox, oy)]++; }
    for (size_t i = 0; i < live.size(); ++i) for (size_t j = i + 1; j < live.size(); ++j) { const int a = live[i], b = live[j]; const PK k = pk(a, b);
      auto ni = near.find(k); const i64 border = ni == near.end() ? 0 : ni->second; i64& trd = pw.trade[k]; const i64 tr = std::min(P.relTradeMax, trd / P.relTradeDiv); trd = 0;
      const i64 v = relOf(W, a, b); relAdd(W, a, b, tr - border * P.relBorderHouses - v / P.relDecay); }
    pw.relN++; }
  { std::vector<i64> uids; for (const auto& c : pw.con) uids.push_back(c.uid);
    for (i64 uid : uids) { auto it = std::find_if(pw.con.begin(), pw.con.end(), [&](const Contest& c) { return c.uid == uid; }); if (it == pw.con.end()) continue;
      Contest& c = *it; Station& s = W.S[c.x]; Agent* A = ag(W, c.a);
      if (s.owner != c.d || !A || !A->alive) { pw.con.erase(it); continue; }
      const i64 dfn = defence(W, c.x); c.m++; c.dsum += dfn;
      c.margin += presence(W, *A, c.x) - dfn + (c.luck * P.luckPct + ((i64)(hashN(W.seed, K_FOUND, 4242, c.x, mo) % 201) - 100) * P.fogPct) * dfn / 10000;
      A->T -= P.contestCost; if (Agent* D = ag(W, c.d)) D->T -= P.contestCost / 2;
      s.pop = std::max(s.basePop / 10, s.pop - s.pop / 200);
      if (A->T < -50000) { pw.con.erase(it); pw.holds++; continue; }
      if (c.m < c.min) continue;
      const Contest cc = c; const i64 avg = cc.margin / cc.m, win = std::max(cc.core ? P.coreWinT : P.winT, cc.dsum / cc.m * P.winPct / 100);
      pw.con.erase(it);
      if (avg > win) { pw.wins++; relAdd(W, cc.a, cc.d, -P.relCaptureHit); transfer(W, s, cc.a, 0); }
      else if (avg < -win || !isFac(cc.d)) pw.holds++;
      else { pw.breaks++; transfer(W, s, IND, 1); } } }
  std::vector<int> facs; for (int id : live) { const int k = pw.ag.at(id).kind; if (k != K_HOUSE && k != K_GUILD) facs.push_back(id); }
  if (!W.rules.intents && mo % P.decideEvery == 0) for (int id : facs) decide(W, pw.ag.at(id), cr);
  if (Agent* H = ag(W, HOUSE); H && !W.rules.intents) { i64 own = 0; for (const auto& s : W.S) if (s.owner == HOUSE) ++own;
    std::vector<int> cand; for (const auto& s : W.S) { if (s.owner != IND || s.tier <= 0 || s.dropped == INT64_MIN || mo - s.dropped > P.houseRecent) continue; bool dep = false; for (const auto& d : s.dep) if (depOut(d) > 0) dep = true; if (!dep) continue;
      bool con = false; for (const auto& c : pw.con) if (c.x == s.id) con = true; if (!con) cand.push_back(s.id); }
    std::stable_sort(cand.begin(), cand.end(), [&](int a, int b) { const i64 ra = crv(cr.ret, a), rb = crv(cr.ret, b); return ra != rb ? ra > rb : a < b; });
    if (own < P.houseMax && !cand.empty()) { Station& s = W.S[cand[0]]; const i64 price = s.pop * P.housePriceK; if (H->T >= price) { H->T -= price; H->alive = 1; pw.buys++; transfer(W, s, HOUSE, 0); } } }
  { i64 ups = 0; for (int id : facs) { const Agent& a = pw.ag.at(id); if (a.up && a.kind != K_BASE) ++ups; }
    if (mo % P.upEvery == 6 && ups < P.upMax && hashN(W.seed, K_FOUND, 991, mo) % 100 < (uint32_t)P.upChance) {
      std::vector<int> cand; std::vector<i64> wt;
      for (const auto& s : W.S) { if (s.tier <= 0 || s.owner == ABAN || s.owner == HOUSE) continue; bool con = false; for (const auto& c : pw.con) if (c.x == s.id) con = true; if (con) continue;
        bool ok = s.owner == IND; if (!ok && isFac(s.owner)) for (const auto& y : W.S) if (&y != &s && y.owner == s.owner) { ok = true; break; } if (ok) cand.push_back(s.id); }
      for (int x : cand) { const Station& s = W.S[x]; wt.push_back(s.owner == IND ? 4 : presence(W, pw.ag.at(s.owner), s.id) * 100 < s.pop / P.indMilDiv * P.upWeakT ? 3 : 1); }
      if (!cand.empty()) { i64 tot = 0; for (auto v : wt) tot += v; i64 pick = hashN(W.seed, K_FOUND, 992, mo) % tot; size_t j = 0; while (pick >= wt[j]) pick -= wt[j++];
        Station& s = W.S[cand[j]]; const int id = pw.nextId++, prev = s.owner;
        Agent na; na.id = id; na.kind = K_MINOR; na.up = 1; na.T = P.upT0; na.born = mo; na.peak = 1; na.seat = s.id; pw.ag[id] = na;
        for (int lid : live) if (lid != id) { const Agent& a = pw.ag.at(lid); pw.rel[pk(lid, id)] = lid == prev ? P.upOwnerHit : a.kind == K_HOUSE ? 0 : P.upRelOthers; }
        s.owner = id; { const int p = choosePatron(W, pw.ag.at(id), prev); setPatron(W, pw.ag.at(id), p); } s.owner = prev;
        pw.upSpawn++; transfer(W, s, id, 0); } } }
  for (int id : aliveIds(W)) { Agent& a = pw.ag.at(id);
    if (!(a.up && a.kind != K_BASE && mo - a.born < P.upProve && a.seat >= 0 && W.S[a.seat].owner == a.id)) continue;
    int top = -1; i64 tp = 0;
    for (int bid : aliveIds(W)) { const Agent& b = pw.ag.at(bid); if (b.id != a.id && b.kind != K_HOUSE && b.id != a.patron && presence(W, b, a.seat) > 0) { const i64 pb = b.S * 100 / (100 + coreDist(W, b.id, a.seat) * P.upDepthK); if (pb > tp) { tp = pb; top = b.id; } } }
    const i64 ud = (W.S[a.seat].pop / P.strengthPopDiv) * (100 + P.homePct) / 100 + P.upMilitia;
    a.crush = std::max<i64>(0, a.crush + (tp * 100 > ud * P.upCrushT ? 1 : -1));
    if (a.crush >= P.upCrushMonths) { Station& s = W.S[a.seat]; const bool ok = top >= 0 && canTarget(W, pw.ag.at(top), s, IND, &cr); transfer(W, s, ok ? top : IND, 0); } }
  for (int id : aliveIds(W)) { const Agent& a = pw.ag.at(id); if ((a.kind == K_UPSTART || a.kind == K_MINOR) && a.T < -P.upDebt) for (auto& s : W.S) if (s.owner == a.id) transfer(W, s, IND, 1); }
  for (int id : aliveIds(W)) { Agent& a = pw.ag.at(id); i64 n = 0; for (const auto& s : W.S) if (s.owner == a.id) ++n; if (!n && a.kind != K_HOUSE && a.kind != K_GUILD) { a.alive = 0; a.died = mo; } }
  houseYear(W, cr);
  tollYear(W);
}

}  // namespace apsis::world
