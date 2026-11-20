#include "bots.hpp"
#include "../world/ops.hpp"
#include "law.hpp"
#include <algorithm>
namespace apsis::world {
const char* kStyleName[] = {"hauler", "crisis", "pathfinder", "careful", "bold", "reckless", "payer", "forgetful", "double agent"};
bool ready(const World& W) { const Pilot& p = W.pl; return p.alive && !p.trip.active && p.busy <= W.day && p.at >= 0; }
namespace {
// today's routes, shared by every candidate a bot weighs (a cache: same inputs, same answer)
const Routes& routesC(const World& W, int from, i64 rEff, bool push, i64 dh) {
  if (W.routeCacheDay != W.day) { W.routeCache.clear(); W.routeCacheDay = W.day; }
  const std::string k = std::to_string(from) + ':' + std::to_string(rEff) + ':' + std::to_string(push) + ':' + std::to_string(dh);
  auto it = W.routeCache.find(k); if (it == W.routeCache.end()) it = W.routeCache.emplace(k, std::make_shared<const Routes>(routesFrom(W, from, rEff, push, dh))).first;
  return *static_cast<const Routes*>(it->second.get());
}
bool okDock(const World& W, int s) { return dockable(W, s) && !exiled(W, W.S[(size_t)s].owner); }   // a bot never docks where it is exiled
const Agent* great(const World& W, int o) { auto it = W.pw.ag.find(o); return it != W.pw.ag.end() && it->second.alive && it->second.kind == K_BASE ? &it->second : nullptr; }
Action act(int32_t k, i64 a = 0, i64 b = 0, i64 c = 0, i64 d = 0, i64 e = 0) { Action x; x.kind = k; x.f = {a, b, c, d, e, 0}; return x; }
const std::vector<i64>& tollAt(const World& W) {   // toll per 100 units on arrival at each system (lit tollgates), once per day
  auto& t = W.tollCache;   // a cache in World, not state (no thread_local: the core stays free of toolchain-specific runtime support)
  if (W.tollCacheDay != W.day || t.size() != W.S.size()) { t.assign(W.S.size(), 0); for (const auto& ln : W.net.lanes) if (ln.j >= 0 && (size_t)ln.j < t.size() && !t[(size_t)ln.j] && lit(W, ln.j)) t[(size_t)ln.j] = std::max<i64>(0, laneToll(W, ln, -1)); W.tollCacheDay = W.day; }
  return t; }
i64 tollPer100(const World& W, const Routes& R, int from, int to) {   // tolls on arrival at lit tollgates along the cheapest path
  const auto& T = tollAt(W); i64 t = 0; for (int x = to; x != from && x >= 0; x = R.via[x]) t += T[(size_t)x]; return t; }
struct Haul { int k = -1, b = -1; i64 n = 0, profit = 0, rate = 0; };
Haul bestHaul(const World& W, int at, const Routes& R, bool anyProfit) {
  const Pilot& p = W.pl; const auto& P = W.CP; const Station& A = W.S[at]; Haul best; bool have = false;
  std::vector<int> goods; if (p.holdN > 0) goods.push_back(p.holdK); else goods = {0, 1, 2, 3};
  for (int k : goods) {
    const i64 spare = std::max<i64>(0, A.stock[k] - A.cap[k] * P.plReservePct / 100), n = p.holdN > 0 ? p.holdN : std::min(p.ship.hold, spare); if (n <= 0) continue;
    const i64 cost = p.holdN > 0 ? 0 : lotValue(W, A, k, n); if (cost > p.wallet) continue;
    for (const auto& B : W.S) { const int b = B.id; if (b == at || !okDock(W, b) || R.via[b] < 0 || R.jumps[b] > p.fuel) continue; const i64 m = std::min(n, B.cap[k] - B.stock[k]); if (m <= 0) continue;
      bool con = false; for (const auto& c : W.pw.con) if (c.x == b) con = true;
      const i64 sale = lotValue(W, B, k, m), toll = n * tollPer100(W, R, at, b) / 100, fuel = R.jumps[b] * (P.refuelPerJump + P.subLight), loss = con ? sale * P.blockadeRiskPm / 1000 : 0;
      const i64 days = R.days[b] + 2 * P.dockDays, profit = sale - cost - toll - fuel - loss, rate = profit * 1000 / std::max<i64>(1, days);
      if (!anyProfit && profit <= 0) continue;
      if (!have || (anyProfit ? profit > best.profit : rate > best.rate)) { have = true; best = Haul{k, b, n, profit, rate}; } } }
  return best;
}
std::vector<Action> haulOrMove(const World& W) {
  const Pilot& p = W.pl; const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, false, p.dh);
  Haul h = bestHaul(W, p.at, R, false); if (h.b >= 0) return {act(A_FLY, h.b, h.k, h.n, 0, 0)};
  if (p.holdN > 0) { h = bestHaul(W, p.at, R, true); if (h.b >= 0) return {act(A_FLY, h.b, h.k, h.n, 0, 0)}; }
  // nothing here: move empty to a station within two jumps with the best outbound haul
  int pick = -1; i64 pr = 0; std::vector<int> near;   // the four nearest candidates, by days
  for (const auto& B : W.S) if (B.id != p.at && okDock(W, B.id) && R.via[B.id] >= 0 && R.jumps[B.id] <= std::min<i64>(2, p.fuel)) near.push_back(B.id);
  std::stable_sort(near.begin(), near.end(), [&](int a, int b) { return R.days[a] != R.days[b] ? R.days[a] < R.days[b] : a < b; }); if (near.size() > 4) near.resize(4);
  for (int bi : near) { const Station& B = W.S[bi];
    const Routes& RB = routesC(W, B.id, rEff, false, p.dh); const Haul hb = bestHaul(W, B.id, RB, false); const i64 v = hb.rate * 1000 / std::max<i64>(1, 1000 + R.days[B.id] * 50);
    if (hb.b >= 0 && (pick < 0 || v > pr)) { pick = B.id; pr = v; } }
  if (pick >= 0) return {act(A_FLY, pick, 0, 0, 0, 0)};
  for (const auto& j : board(W).jobs) if (j.kind == 2 && !p.closed.count(j.id) && p.job.id < 0) return {act(A_TAKE, j.id), act(A_FLY, j.to, 0, 0, 1, 0)};   // the floor job is always there (pushing if it must)
  if (p.job.id >= 0 && p.job.kind == 2) return {act(A_FLY, p.job.to, 0, 0, 1, 0)};
  return {};
}
struct Step { int part, drive; };
std::vector<std::vector<Step>> refitOrder(int style) {
  if (style == S_PATHFINDER) return {{{P_HOLD, 0}}, {{P_DRIVE, 3}}, {{P_HULL_LIGHT, 0}}, {{P_SHIELD, 0}}};
  if (style >= S_CAREFUL) {   // the law styles: a hidden compartment first (stealth has one role: beating scans), then the hauler's order
    std::vector<std::vector<Step>> o = {{{P_COMP, 0}}, {{P_HOLD, 0}}, {{P_HULL_HEAVY, 0}}}; for (int i = 0; i < 4; ++i) o.push_back({{P_HOLD, 0}}); o.push_back({{P_DRIVE, 3}}); for (int i = 0; i < 3; ++i) o.push_back({{P_HOLD, 0}}); return o; }
  std::vector<std::vector<Step>> o = {{{P_HOLD, 0}}, {{P_HOLD, 0}}, {{P_HULL_HEAVY, 0}}}; for (int i = 0; i < 4; ++i) o.push_back({{P_HOLD, 0}}); o.push_back({{P_DRIVE, 3}}); for (int i = 0; i < 3; ++i) o.push_back({{P_HOLD, 0}}); return o;
}
std::vector<Action> maintain(const World& W, int style) {
  const Pilot& p = W.pl; const auto& P = W.CP; const Station& s = W.S[p.at];
  if (p.dh < P.repairAt || p.lsh < P.repairLsAt) { const i64 cost = P.repairFee + P.repairFull * (1000 - p.dh) / 1000 + P.repairLsFull * (1000 - p.lsh) / 1000; if (p.wallet >= cost || rangeMap(W, p.ship, true) < W.net.J || p.lsh < P.repairLsAt / 2) return {act(A_REPAIR)}; }
  if (!p.debt.empty() && p.wallet > P.refitReserve) return {act(A_REPAY, p.wallet - P.refitReserve)};   // pay debt from cash over the reserve (the bankruptcy clock)
  const auto it = W.pw.ag.find(s.owner); if (it == W.pw.ag.end() || !it->second.alive || p.holdN) return {};
  for (const auto& combo : refitOrder(style)) {   // the first step in the style's order that is still possible
    Ship sh = p.ship; i64 price = 0; bool ok = true;
    for (const auto& st : combo) { Ship nx; if (!refitShip(W, sh, st.part, st.drive, nx)) { ok = false; break; } price += refitPrice(W, st.part, st.drive); sh = nx; }
    if (!ok) continue;
    if (rEffPm(W, sh) < 900 || p.wallet < price + P.refitReserve || !p.debt.empty()) return {};
    std::vector<Action> out; for (const auto& st : combo) out.push_back(act(A_REFIT, st.part, st.drive)); return out;
  }
  return {};
}
std::vector<Action> opening(const World& W) {   // nearby bodies first (no jump), then one dark system out and back, then the board
  const Pilot& p = W.pl; const Station& s = W.S[p.at]; const auto& P = W.CP; const Agent* h = great(W, s.owner);
  if (h && h->T >= P.bodyPay) for (size_t b = 0; b < s.dep.size(); ++b) if (std::find(p.surveyed.begin(), p.surveyed.end(), (i64)p.at * 64 + (i64)b) == p.surveyed.end()) return {act(A_SURVEY, (i64)b)};
  if (!p.charts.empty() && h && h->T >= P.chartPrice) return {act(A_SELL_CHART, s.owner)};
  if (p.firstDarkRun < 0 && p.charted.empty() && W.day - p.born < 360 && p.holdN == 0) {
    const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, false, p.dh); int pick = -1;
    for (const auto& y : W.S) { if (lit(W, y.id) || R.via[y.id] < 0) continue; const i64 need = dockable(W, y.id) ? R.jumps[y.id] : 2 * R.jumps[y.id]; if (need > p.fuel) continue; if (pick < 0 || R.cost[y.id] < R.cost[pick]) pick = y.id; }
    if (pick >= 0) return {act(A_FLY, pick, 0, 0, 0, 1)};
  }
  if (!p.charts.empty()) { const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, false, p.dh); int pick = -1;   // take the chart to a house that can pay
    for (const auto& y : W.S) { const Agent* g = great(W, y.owner); if (!g || g->T < P.chartPrice || !lit(W, y.id) || R.via[y.id] < 0 || R.jumps[y.id] > p.fuel || y.id == p.at) continue; if (pick < 0 || R.cost[y.id] < R.cost[pick]) pick = y.id; }
    if (pick >= 0 && p.holdN == 0) return {act(A_FLY, pick, 0, 0, 0, 0)}; }
  return {};
}
// ---------- the law styles (E1 frozen intents) ----------
struct LawStyle { i64 riskLimitPm; bool courier, spy, run, backOff, payNow, ignore, inform; };
LawStyle lawStyle(int s) {
  switch (s) {
    case S_CAREFUL:   return {50, true, false, false, false, false, false, false};    // packages and smuggling inside a low risk limit; avoids known patrols
    case S_BOLD:      return {150, true, false, false, true, false, false, false};    // a higher risk limit; backs off a house that warned it
    case S_RECKLESS:  return {1000, true, true, true, false, false, false, false};    // any profitable job; runs from scans
    case S_PAYER:     return {150, true, false, false, true, true, false, false};     // like bold, but pays every fine at once
    case S_FORGETFUL: return {50, true, false, false, false, false, true, false};     // like careful, but leaves warrants unpaid and forgets them
    case S_DOUBLE:    return {300, false, true, false, false, false, false, true};    // spy work for any house; sells either side out when it pays
  }
  return {0, false, false, false, false, false, false, false};
}
int lawPolicyOf(int s) { const LawStyle L = lawStyle(s); return L.inform ? 3 : L.ignore ? 2 : L.payNow ? 1 : 0; }
// the chance of being caught on this job: scans on every logged arrival and the dock check, unless hidden; plus a warrant the origin's heat would trigger
i64 catchPm(const World& W, const Routes& R, int from, const Job& j) {
  const Pilot& p = W.pl; const int target = j.meet >= 0 ? j.meet : j.to; i64 pm = 0;
  if (!hidden(p)) { for (int x = target; x != from && x >= 0; x = R.via[x]) if (x != j.meet) pm += scanChancePm(W, x); if (j.meet < 0) pm += scanChancePm(W, j.to); }
  const int O = W.S[(size_t)from].owner; const HouseLaw* L = lawOf(p, O);
  if (L && L->heat + W.CP.heatDepart >= W.CP.heatWarrant) pm += 500;
  return std::min<i64>(1000, pm);
}
bool warnedBy(const World& W, int h) { const HouseLaw* L = lawOf(W.pl, h); return wanted(W.pl, h) || (L && (L->heat >= W.CP.heatT2 || L->off > 0)); }
std::vector<Action> lawMoves(const World& W, int style) {
  const Pilot& p = W.pl; const auto& P = W.CP; const LawStyle L = lawStyle(style);
  if (p.lawPolicy != lawPolicyOf(style) || p.runPolicy != (L.run ? 1 : 0)) return {act(A_POLICY, lawPolicyOf(style), L.run ? 1 : 0)};
  for (const auto& w : p.warrants) {   // ways out, by style: payers at once; the rest when they can spare it; forgetful never; a double agent informs when it pays
    if (L.ignore) break;
    if (L.inform && p.informUntil <= W.day && P.informPay > 0) return {act(A_LAW, 1, w.house)};
    if (p.wallet >= w.fine + (L.payNow ? 0 : P.refitReserve)) return {act(A_LAW, 0, w.house)}; }
  if (p.job.id >= 0 && p.job.kind >= 3) {   // a package or papers in hand: deliver, or drop it if the way is shut
    const int target = p.job.meet >= 0 ? p.job.meet : p.job.to; const Routes& RP = routesC(W, p.at, rEffPm(W, p.ship), true, p.dh);
    const bool open = target != p.at && RP.via[target] >= 0 && (dockable(W, target) ? RP.jumps[target] : 2 * RP.jumps[target]) <= p.fuel && (p.job.meet >= 0 || okDock(W, p.job.to)) && W.day + RP.days[target] <= p.job.deadline;
    return {open ? act(A_FLY, target, 0, 0, 1, 0) : act(A_DROP)}; }
  if (p.job.id >= 0 || p.holdN > 0) return {};
  const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, false, p.dh); const Board bd = board(W); const Job* best = nullptr; i64 bestRate = 0;
  for (const auto& j : bd.jobs) {
    if ((j.kind == 3 && !L.courier) || (j.kind == 4 && !L.spy) || j.kind < 3 || j.from != p.at || p.closed.count(j.id)) continue;
    const int target = j.meet >= 0 ? j.meet : j.to; if (target == p.at || R.via[target] < 0) continue;
    if (j.meet < 0 && !okDock(W, j.to)) continue;   // never deliver into a house that exiled the pilot
    const i64 need = dockable(W, target) ? R.jumps[target] : 2 * R.jumps[target]; if (need > p.fuel || W.day + R.days[target] > j.deadline) continue;
    if (L.backOff && (warnedBy(W, W.S[(size_t)j.to].owner) || warnedBy(W, W.S[(size_t)p.at].owner))) continue;
    if (style == S_CAREFUL || style == S_FORGETFUL) { bool hot = false; for (int x = target; x != p.at && x >= 0; x = R.via[x]) { const HouseLaw* H = lawOf(p, W.S[(size_t)x].owner); if (H && H->heat >= P.heatT1) hot = true; } if (hot) continue; }   // avoids known patrols
    if (catchPm(W, R, p.at, j) > L.riskLimitPm) continue;
    const i64 days = j.meet >= 0 ? std::max(R.days[target], j.deadline - P.meetWait - W.day) : R.days[target];   // a meet: wait for the contact's window
    const i64 rate = j.reward * 1000 / std::max<i64>(1, days + 2 * P.dockDays);
    if (!best || rate > bestRate) { best = &j; bestRate = rate; } }
  if (!best) {   // nothing to pick up here: go to a pickup within two jumps if its job pays better per day than hauling from here
    int go = -1; i64 goRate = 0;
    for (const auto& j : bd.jobs) {
      if ((j.kind == 3 && !L.courier) || (j.kind == 4 && !L.spy) || j.kind < 3 || j.from < 0 || j.from == p.at || p.closed.count(j.id) || !okDock(W, j.from)) continue;
      if (R.via[j.from] < 0 || R.jumps[j.from] > std::min<i64>(2, p.fuel)) continue;
      if (L.backOff && (warnedBy(W, W.S[(size_t)j.to].owner) || warnedBy(W, W.S[(size_t)j.from].owner))) continue;
      if (W.day + R.days[j.from] >= (W.day / P.underWindow + 1) * P.underWindow) continue;   // the offer would be gone before we got there
      { const Routes& RJ = routesC(W, j.from, rEff, false, p.dh); const int tg = j.meet >= 0 ? j.meet : j.to; if (RJ.via[tg] < 0 || RJ.jumps[tg] > p.ship.tank || catchPm(W, RJ, j.from, j) > L.riskLimitPm) continue; }
      const i64 dist = edist(W.net.st[(size_t)j.from].pos, W.net.st[(size_t)(j.meet >= 0 ? j.meet : j.to)].pos), legDaysEst = std::max<i64>(1, P.daysPerJ * dist / std::max<i64>(1, W.net.J));
      const i64 rate = j.reward * 1000 / std::max<i64>(1, R.days[j.from] + legDaysEst + 3 * P.dockDays);
      if (go < 0 || rate > goRate) { go = j.from; goRate = rate; } }
    if (go < 0) return {};
    const Haul h = bestHaul(W, p.at, R, false); if (h.b >= 0 && h.rate >= goRate) return {};
    return {act(A_FLY, go, 0, 0, 0, 0)}; }
  // compare with the best legal haul from here: smuggle only when it pays better per day
  const Haul h = bestHaul(W, p.at, R, false); if (h.b >= 0 && h.rate >= bestRate) return {};
  return {act(A_TAKE, best->id), act(A_FLY, best->meet >= 0 ? best->meet : best->to, 0, 0, 0, 0)};
}
}  // namespace

std::vector<Action> decideBot(const World& W, int style) {
  if (!ready(W)) return {};
  const Pilot& p = W.pl; const auto& P = W.CP;
  if (style >= S_CAREFUL && (p.lawPolicy != lawPolicyOf(style) || p.runPolicy != (lawStyle(style).run ? 1 : 0))) return lawMoves(W, style);
  if (auto m = maintain(W, style); !m.empty()) return m;
  if (style >= S_CAREFUL) { if (auto l = lawMoves(W, style); !l.empty()) return l; }
  if (auto o = opening(W); !o.empty()) return o;
  if (style == S_CRISIS && p.job.id < 0) {
    const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, false, p.dh); const Station& A = W.S[p.at]; i64 bestRate = 0; const Job* best = nullptr; i64 bn = 0; const Board bd = board(W);
    for (const auto& j : bd.jobs) { if (j.kind > 1 || j.units <= 0 || j.to == p.at || p.closed.count(j.id) || R.via[j.to] < 0 || R.jumps[j.to] > p.fuel || W.day + R.days[j.to] > j.deadline) continue;
      const int k = j.good; const i64 n = p.holdN > 0 ? (p.holdK == k ? p.holdN : 0) : std::min(j.units, std::max<i64>(0, A.stock[k] - A.cap[k] * P.plReservePct / 100)); if (n <= 0) continue;
      const i64 cost = p.holdN > 0 ? 0 : lotValue(W, A, k, n); if (cost > p.wallet) continue;
      const i64 gain = j.reward * std::min(n, j.units) / std::max<i64>(1, j.units) + lotValue(W, W.S[j.to], k, n) - cost - n * tollPer100(W, R, p.at, j.to) / 100, rate = gain * 1000 / std::max<i64>(1, R.days[j.to] + 2 * P.dockDays);
      if (!best || rate > bestRate) { best = &j; bestRate = rate; bn = n; } }
    if (best && bestRate > 0) return {act(A_TAKE, best->id), act(A_FLY, best->to, best->good, bn, 0, 0)};
  }
  if (style == S_PATHFINDER && p.holdN == 0) {
    const i64 rEff = rEffPm(W, p.ship); const Routes& R = routesC(W, p.at, rEff, true, p.dh); int pick = -1;   // pathfinders push their drive
    for (const auto& y : W.S) { if (lit(W, y.id) || R.via[y.id] < 0 || std::find(p.charted.begin(), p.charted.end(), y.id) != p.charted.end()) continue; const i64 need = dockable(W, y.id) ? R.jumps[y.id] : 2 * R.jumps[y.id]; if (need > p.fuel) continue; if (pick < 0 || R.cost[y.id] < R.cost[pick]) pick = y.id; }
    if (pick >= 0) return {act(A_FLY, pick, 0, 0, 1, 1)};
    int sv = -1; for (const auto& y : W.S) { const Agent* g = great(W, y.owner); if (!g || g->T < P.bodyPay || y.id == p.at || R.via[y.id] < 0 || R.jumps[y.id] > p.fuel) continue; bool left = false;
      for (size_t b = 0; b < y.dep.size(); ++b) if (std::find(p.surveyed.begin(), p.surveyed.end(), (i64)y.id * 64 + (i64)b) == p.surveyed.end()) left = true; if (left && (sv < 0 || R.cost[y.id] < R.cost[sv])) sv = y.id; }
    if (sv >= 0) return {act(A_FLY, sv, 0, 0, 1, 0)};
  }
  return haulOrMove(W);
}
}  // namespace apsis::world
