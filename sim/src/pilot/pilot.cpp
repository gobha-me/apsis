#include "pilot.hpp"
#include <algorithm>
#include "../world/ops.hpp"
#include "law.hpp"

namespace apsis::world {
static const uint32_t K_PL = strHash("pilot"), K_JOB = strHash("job");
static const i64 RATING[5] = {500, 1000, 1500, 2000, 3000};

i64 driveRatingPm(int idx) { return RATING[clampI(idx, 0, 4)]; }
static i64 driveSpace(const Params& P, int i) { const i64 v[5] = {P.drive05Space, P.drive10Space, P.drive15Space, P.drive20Space, P.drive30Space}; return v[clampI(i, 0, 4)]; }
static i64 driveMass(const Params& P, int i) { const i64 v[5] = {P.drive05Mass, P.drive10Mass, P.drive15Mass, P.drive20Mass, P.drive30Mass}; return v[clampI(i, 0, 4)]; }
static i64 hullSpace(const Params& P, int h) { return h == 0 ? P.lightSpace : h == 2 ? P.heavySpace : P.mediumSpace; }
static i64 hullMass(const Params& P, int h) { return h == 0 ? P.lightMass : h == 2 ? P.heavyMass : P.mediumMass; }
i64 shipSpace(const World& W, const Ship& s) { const auto& P = W.CP; return driveSpace(P, s.drive) + s.tank * P.tankSpace + s.hold / 10 * P.holdSpace + s.comp / 10 * P.compSpace + s.shields * P.shieldSpace + s.weapons * P.weaponSpace + s.ls * P.lsSpace; }
i64 shipMass(const World& W, const Ship& s) { const auto& P = W.CP; return hullMass(P, s.hull) + driveMass(P, s.drive) + s.tank * P.tankMass + s.hold / 10 * P.holdMass + s.comp / 10 * P.compMass + s.shields * P.shieldMass + s.weapons * P.weaponMass + s.ls * P.lsMass; }
i64 rEffPm(const World& W, const Ship& s) { return driveRatingPm(s.drive) * W.CP.massRef / std::max<i64>(1, shipMass(W, s)); }
i64 rangeMap(const World& W, const Ship& s, bool push) { return rEffPm(W, s) * W.net.J * (push ? W.CP.overreachPm : 1000) / 1000000; }
i64 legRiskPm(const World& W, i64 len, bool litDest, i64 rEff, i64 dh) {
  const auto& P = W.CP; const i64 x = len * 1000000 / std::max<i64>(1, rEff * W.net.J);
  const i64 base = P.misBasePm + P.misSlopePm * std::max<i64>(0, x - 1000) / 1000;
  return base * (litDest ? P.beaconRiskPm : P.blindRiskPm) / 1000 * (2000 - dh) / 1000;
}
i64 legDays(const World& W, i64 len, bool blind, int dest) {
  const auto& P = W.CP; i64 d = std::max<i64>(1, (P.daysPerJ * len + W.net.J - 1) / W.net.J); if (blind) d = d * P.blindTimePct / 100;
  if (W.wellD.size() != W.net.st.size()) { W.wellD.assign(W.net.st.size(), 0); for (size_t s = 0; s < W.net.st.size(); ++s) for (const auto& h : W.net.hz) if (h.t == 1) { const i64 dx = W.net.st[s].pos.x - h.p.x, dy = W.net.st[s].pos.y - h.p.y; if (dx * dx + dy * dy <= h.r * h.r) W.wellD[s] += h.sub; } }
  return d + W.wellD[(size_t)dest];   // gravity-well delay at the destination (cached per map)
}
bool dockable(const World& W, int s) { return s >= 0 && (size_t)s < W.S.size() && W.S[s].owner != ABAN; }
bool lit(const World& W, int s) { return !W.bk.ds[(size_t)s]; }

static const std::vector<i64>& distM(const World& W) { const size_t N = W.net.st.size(); if (W.distN != N) { W.distM.assign(N * N, 0); for (size_t a = 0; a < N; ++a) for (size_t b = 0; b < N; ++b) W.distM[a * N + b] = edist(W.net.st[a].pos, W.net.st[b].pos); W.distN = N; } return W.distM; }
Routes routesFrom(const World& W, int from, i64 rEff, bool push, i64 dh) {
  const int N = (int)W.S.size(); const auto& DM = distM(W); const size_t NN = W.distN; const i64 INF = INT64_MAX / 4, reach = rEff * W.net.J * (push ? W.CP.overreachPm : 1000) / 1000000, plain = rEff * W.net.J / 1000;
  Routes R; R.cost.assign(N, INF); R.days.assign(N, 0); R.jumps.assign(N, 0); R.risk.assign(N, 0); R.via.assign(N, -1); R.pushed.assign(N, 0);
  std::vector<char> done(N, 0); R.cost[from] = 0;
  for (;;) { int u = -1; for (int i = 0; i < N; ++i) if (!done[i] && R.cost[i] < INF && (u < 0 || R.cost[i] < R.cost[u])) u = i; if (u < 0) break; done[u] = 1;
    for (int v = 0; v < N; ++v) { if (v == u || done[v]) continue; const i64 L = DM[(size_t)u * NN + (size_t)v]; if (L > reach) continue;
      const bool lt = lit(W, v); const i64 rk = legRiskPm(W, L, lt, rEff, dh), dd = legDays(W, L, !lt, v);
      const i64 c = R.cost[u] + 1000000 + rk * 1000 + dd;
      if (c < R.cost[v]) { R.cost[v] = c; R.via[v] = u; R.days[v] = R.days[u] + dd; R.jumps[v] = R.jumps[u] + 1; R.risk[v] = R.risk[u] + rk; R.pushed[v] = R.pushed[u] || L > plain; } } }
  return R;
}
Plan planOf(const World& W, const Routes& R, int from, int to, i64, i64) {
  Plan p; if (to == from || R.via[to] < 0) return p;
  std::vector<int> chain; for (int x = to; x != from; x = R.via[x]) chain.push_back(x); chain.push_back(from); std::reverse(chain.begin(), chain.end());
  for (size_t i = 0; i + 1 < chain.size(); ++i) { Leg2 l; l.from = chain[i]; l.to = chain[i + 1]; l.len = edist(W.net.st[l.from].pos, W.net.st[l.to].pos); l.blind = lit(W, l.to) ? 0 : 1; p.legs.push_back(l); }
  p.days = R.days[to] + 2 * W.CP.dockDays; p.riskPm = R.risk[to]; p.ok = true; return p;
}
i64 lotValue(const World& W, const Station& s, int k, i64 n) { const i64 r = std::min<i64>(10000, s.stock[k] * 10000 / std::max<i64>(1, s.cap[k])); return n * W.P.valuePerUnit[k] * (20000 - r) / 10000; }
static int worstGood(const Station& s) { int m = 0; for (int k = 1; k < 4; ++k) if (s.stock[k] * s.cap[m] < s.stock[m] * s.cap[k]) m = k; return m; }
static i64 jobId(int kind, i64 x, i64 t) { return hashN(K_JOB, kind, x, t); }

Board board(const World& W) {
  const auto& P = W.CP; const Pilot& pl = W.pl; Board b; const i64 d = W.day; const bool fresh = pl.alive && d - pl.born < P.newPilotDays; const i64 rng = pl.alive ? rangeMap(W, pl.ship, false) : 0;
  auto safe = [&](int to) { return !fresh || pl.at < 0 || (lit(W, to) && edist(W.net.st[pl.at].pos, W.net.st[to].pos) <= rng); };
  for (const auto& s : W.S) { if (s.owner == ABAN || s.stage <= 0 || !safe(s.id)) continue; const int k = worstGood(s); const i64 n = std::min(pl.ship.hold, s.cap[k] - s.stock[k]); if (n <= 0) continue;
    Job j; j.id = jobId(1, s.id, s.leftNormal); j.kind = 0; j.to = s.id; j.good = k; j.units = n; j.reward = n * W.P.valuePerUnit[k] * P.emergencyBonus; j.deadline = d + P.jobDeadline; b.jobs.push_back(j); }
  for (const auto& c : W.pw.con) { const Station& s = W.S[c.x]; if (s.owner == ABAN || !safe(s.id)) continue; const int k = worstGood(s); const i64 n = std::max<i64>(0, std::min(pl.ship.hold, s.cap[k] - s.stock[k]));
    Job j; j.id = jobId(2, c.x, c.a); j.kind = 1; j.to = c.x; j.good = k; j.units = n; j.reward = n * W.P.valuePerUnit[k] * P.blockadeBonus; j.deadline = d + P.jobDeadline; b.jobs.push_back(j); }
  if (pl.alive && pl.at >= 0) { int q = -1; i64 qd = 0; const Pt& a0 = W.net.st[pl.at].pos;   // the nearest station this ship reaches in one jump (pushing if it must, else the nearest at all)
    for (int pass = 0; pass < 3 && q < 0; ++pass) { const i64 lim = pass == 0 ? rangeMap(W, pl.ship, false) : pass == 1 ? rangeMap(W, pl.ship, true) : INT64_MAX;
      // never into a house that exiled the pilot
      for (const auto& y : W.S) { if (y.id == pl.at || y.owner == ABAN || exiled(W, y.owner)) continue; const i64 dd = edist(a0, W.net.st[y.id].pos); if (dd > lim) continue; if (q < 0 || dd < qd || (dd == qd && y.id < q)) { q = y.id; qd = dd; } } }
    if (q >= 0) { Job j; j.id = jobId(6, pl.at * 1024 + q, d / 30); j.kind = 2; j.to = q; j.reward = P.floorBase + qd * P.floorPerLen; j.deadline = d + P.jobDeadline; b.jobs.push_back(j); } }
  for (auto& j : b.jobs) if (j.issuer < 0 && j.to >= 0) j.issuer = W.S[(size_t)j.to].owner;   // posted by the station in need
  underJobs(W, b.jobs);
  std::stable_sort(b.jobs.begin(), b.jobs.end(), [](const Job& x, const Job& y) { return x.id < y.id; });
  return b;
}

static i64 sizeBandPrice(const Params& P, i64 space) { return space <= P.sizeSmallMax ? P.refitSmall : space <= P.sizeMidMax ? P.refitMid : P.refitLarge; }
i64 refitPrice(const World& W, int part, int driveIdx) {
  const auto& P = W.CP;
  switch (part) { case P_HULL_LIGHT: case P_HULL_MEDIUM: case P_HULL_HEAVY: return P.refitLarge; case P_DRIVE: return sizeBandPrice(P, driveSpace(P, driveIdx));
    case P_TANK: return sizeBandPrice(P, 2 * P.tankSpace); case P_HOLD: return sizeBandPrice(P, P.holdSpace); case P_COMP: return sizeBandPrice(P, P.compSpace);
    case P_SHIELD: return sizeBandPrice(P, P.shieldSpace); case P_WEAPON: return sizeBandPrice(P, P.weaponSpace); case P_LS: return sizeBandPrice(P, P.lsSpace); }
  return P.refitLarge;
}
bool refitShip(const World& W, const Ship& cur, int part, int driveIdx, Ship& s) {
  const auto& P = W.CP; s = cur; const i64 maxPts = P.partMaxPoints;
  if (part <= P_HULL_HEAVY) { if (part == cur.hull) return false; s = Ship{}; s.hull = part; s.drive = cur.drive; s.tank = 0; s.hold = 0;   // parts that fit carry over, in this order
    const i64 room = hullSpace(P, part); auto fits = [&] { return shipSpace(W, s) <= room; };
    if (!fits()) return false;
    for (i64 t = 0; t < cur.tank; ++t) { s.tank++; if (!fits()) { s.tank--; break; } }
    for (i64 h = 0; h < cur.hold; h += 10) { s.hold += 10; if (!fits()) { s.hold -= 10; break; } }
    for (i64 c = 0; c < cur.comp; c += 10) { s.comp += 10; if (!fits()) { s.comp -= 10; break; } }
    for (i64 k = 0; k < cur.shields; ++k) { s.shields++; if (!fits()) { s.shields--; break; } }
    for (i64 k = 0; k < cur.weapons; ++k) { s.weapons++; if (!fits()) { s.weapons--; break; } }
    for (i64 k = 0; k < cur.ls; ++k) { s.ls++; if (!fits()) { s.ls--; break; } }
    return s.tank >= 2; }
  if (part == P_DRIVE) { if (driveIdx <= cur.drive || driveIdx > 4) return false; s.drive = driveIdx; }
  else if (part == P_TANK) s.tank += 2; else if (part == P_HOLD) s.hold += 10; else if (part == P_COMP) s.comp += 10;
  else if (part == P_SHIELD) { if (!W.rules.shields || s.shields >= maxPts) return false; s.shields++; } else if (part == P_WEAPON) { if (!W.rules.weapons || s.weapons >= maxPts) return false; s.weapons++; }
  else if (part == P_LS) { if (!W.rules.lifesupport || s.ls >= maxPts) return false; s.ls++; }
  else return false;
  return shipSpace(W, s) <= hullSpace(P, s.hull);
}
i64 debtOf(const Pilot& p) { i64 t = 0; for (const auto& d : p.debt) t += d.owed; return t; }
i64 lsDays(const World& W, const Ship& s, i64 lsh) {
  const auto& P = W.CP; const i64 hull = s.hull == 0 ? P.lsLightDays : s.hull == 2 ? P.lsHeavyDays : P.lsMediumDays, cut = s.hull == 0 ? 0 : P.lsCutPct;
  return (hull * (100 + cut) / 100 + s.ls * P.lsPartDays) * std::clamp<i64>(lsh, 0, 1000) / 1000 + P.lsSuitDays;
}

// ---------- money ----------
Agent* liveAgent(World& W, int o) { auto it = W.pw.ag.find(o); return it != W.pw.ag.end() && it->second.alive ? &it->second : nullptr; }
static void owe(World& W, int house, i64 amt) { if (amt <= 0) return; Pilot& p = W.pl; for (auto& d : p.debt) if (d.house == house) { d.owed += amt; return; } p.debt.push_back(Debt{house, amt}); }
static void settleDebtClock(World& W) { Pilot& p = W.pl; std::erase_if(p.debt, [](const Debt& d) { return d.owed <= 0; }); if (p.debt.empty() && p.rescueDebtFrom >= 0) { p.debtClear.push_back(W.day - p.rescueDebtFrom); p.rescueDebtFrom = -1; }
  if (p.debt.empty()) { p.debtFrom = -1; p.debtWarned = 0; } else if (p.debtFrom < 0) p.debtFrom = W.day; }   // the bankruptcy clock: all debt, resets only at zero
static void endCareer(World& W, int kind, int evKind) {   // a true end state: the ship and its cargo are gone, the debt stays on the books
  Pilot& p = W.pl; if (p.holdN > 0) { p.bk.lostUnits += p.holdN; W.led.ploss += p.holdN; p.holdN = 0; p.holdK = -1; }
  p.ev.push_back({W.day, evKind, p.sys}); p.end = kind; p.endDay = W.day; p.alive = 0; p.trip = Trip{}; p.job = Job{}; p.at = -1; }
void pay(World& W, i64 amt, int creditor, i64* book) {   // a cost: from the wallet, the rest as debt
  Pilot& p = W.pl; if (amt <= 0) return; if (book) *book += amt; const i64 cash = std::min(amt, std::max<i64>(0, p.wallet)); p.wallet -= cash; owe(W, creditor, amt - cash);
}
void income(World& W, i64 amt, bool delivery) {   // income: a fuel advance is repaid from delivery pay first, then debt is garnished
  Pilot& p = W.pl; if (amt <= 0) return;
  if (delivery && p.advance > 0) { const i64 a = std::min(amt, p.advance); p.advance -= a; amt -= a; }
  if (!p.debt.empty()) { i64 g = amt * W.CP.garnishPct / 100; p.bk.garnished += g; amt -= g;
    for (auto& d : p.debt) { const i64 x = std::min(g, d.owed); d.owed -= x; g -= x; if (Agent* a = liveAgent(W, d.house)) a->T += x; if (!g) break; } amt += g; settleDebtClock(W); }
  p.wallet += amt;
}

// ---------- docking, refuel, rescue ----------
static void refuel(World& W, int s) {
  Pilot& p = W.pl; const i64 need = p.ship.tank - p.fuel; if (need <= 0 || !dockable(W, s)) return; const i64 cost = need * W.CP.refuelPerJump; const int o = W.S[s].owner;
  p.fuel = p.ship.tank; p.bk.fuel += cost;
  if (p.wallet >= cost) { p.wallet -= cost; if (Agent* a = liveAgent(W, o)) a->T += cost; return; }
  const i64 cash = std::max<i64>(0, p.wallet), rest = cost - cash; p.wallet -= cash;
  if (p.holdN > 0 || p.job.id >= 0) { const i64 adv = rest * (100 + W.CP.advancePct) / 100; p.advance += adv; p.bk.advFees += adv - rest; p.n.advances++; }
  else { owe(W, o, rest); p.n.fuelDebt++; }
}
static void chartIfDark(World& W, int s) { Pilot& p = W.pl; if (!lit(W, s) && std::find(p.charted.begin(), p.charted.end(), s) == p.charted.end()) { p.charted.push_back(s); p.charts.push_back(s); } }
static void arrive(World& W) {
  Pilot& p = W.pl; const int dest = p.trip.dest; const auto& P = W.CP; Station& B = W.S[dest]; const bool runOut = p.trip.purpose == 1;
  const int prevSys = p.trip.legs.empty() ? -1 : p.trip.legs.back().from;
  p.trip = Trip{}; p.at = dest; p.sys = dest; p.busy = W.day + P.dockDays;
  chartIfDark(W, dest);
  settleUnder(W, dest, true);   // a rendezvous: handed over at the meet, no docking needed
  if (!dockable(W, dest)) { p.at = -1; return; }   // a dead station: nowhere to dock (chart runs plan the way back)
  // exiled from this house (it can begin mid-trip): the pilot doesn't dock and heads for the nearest place that will have it. A player may choose to dock and breach (lawDock).
  if (exiled(W, B.owner) && !p.dockAnyway) { p.at = -1; p.busy = W.day; return; }
  if (lawDock(W, dest, prevSys)) return;   // held: a breach of exile, a served warrant, or a dock inspection
  settleUnder(W, dest, false);
  if (!runOut && p.firstDarkRun < 0 && !p.charts.empty() && lit(W, dest)) { p.firstDarkRun = W.day - p.born; p.n.darkRuns++; }
  else if (!runOut && !p.charts.empty() && lit(W, dest)) p.n.darkRuns++;
  bool contested = false; for (const auto& c : W.pw.con) if (c.x == dest) contested = true;
  i64 sold = 0; const int k = p.holdK;
  if (p.holdN > 0) {
    if (contested && hashN(W.seed, K_PL, 0xb10c, p.n.trips) % 1000 < (uint32_t)P.blockadeRiskPm) { p.bk.lostUnits += p.holdN; W.led.ploss += p.holdN; p.holdN = 0; p.holdK = -1; }
    else { sold = std::min(p.holdN, std::max<i64>(0, B.cap[k] - B.stock[k])); if (sold > 0) { const i64 v = lotValue(W, B, k, sold); B.stock[k] += sold; p.holdN -= sold; p.bk.sales += v; income(W, v, true); if (p.job.id < 0 || p.job.to != dest) p.delivTo[B.owner]++; } if (!p.holdN) p.holdK = -1; } }
  Job& j = p.job;
  if (j.id >= 0 && j.to == dest) { bool ok = W.day <= j.deadline; i64 r = j.reward;
    if (j.kind != 2) { ok = ok && sold > 0 && k == j.good; r = j.reward * std::min(sold, j.units) / std::max<i64>(1, j.units); }
    if (ok) { p.bk.rewards += r; income(W, r, true); p.n.done++; p.delivTo[B.owner]++; } else p.n.failed++;
    p.closed[j.id] = W.day; j = Job{}; }
  refuel(W, dest);   // after the sale: a ship docks, sells, then pays for fuel
}
static void rescue(World& W) {
  Pilot& p = W.pl; const auto& P = W.CP; const int at = p.sys; const Pt& pos = W.net.st[at].pos;
  int r = -1; i64 rd = 0;   // the nearest lit, served station sends the crew
  for (const auto& s : W.S) { if (!dockable(W, s.id) || !lit(W, s.id)) continue; const i64 d = edist(pos, W.net.st[s.id].pos); if (r < 0 || d < rd || (d == rd && s.id < r)) { r = s.id; rd = d; } }
  if (r < 0) for (const auto& s : W.S) if (dockable(W, s.id)) { r = s.id; break; }
  const i64 crew = P.rescueCrewPm;   // a standard crew ship (1.0 J), pushing as far as it must
  const Routes R = routesFrom(W, r, crew, true, 1000); Plan out = planOf(W, R, r, at, crew, 1000);
  i64 price = P.rescueTow, days = 0, outDays = 0, blindOut = 0;
  // rule `rescue` off: the flat tow (v0.20), no crew legs, no blind delay
  if (W.rules.rescue) for (const auto& l : out.legs) { const i64 pm = std::min(legRiskPm(W, l.len, !l.blind, crew, 1000), P.rescueRiskCapPm); price += 2 * P.rescueCrewLeg * (P.rescueRiskDivPm + pm) / P.rescueRiskDivPm; { const i64 ld = legDays(W, l.len, l.blind, l.to); days += 2 * ld; outDays += ld; } blindOut += l.blind; }
  bool nearLit = lit(W, at); for (const auto& s : W.S) if (lit(W, s.id) && edist(pos, W.net.st[s.id].pos) <= W.net.J) nearLit = true;
  const i64 resp = P.rescueRespDays + (nearLit || !W.rules.rescue ? 0 : P.rescueBlindDays * blindOut); days += resp;
  if (W.rules.lifesupport && resp + outDays > lsDays(W, p.ship, p.lsh)) { p.n.strand++; endCareer(W, E_LOST, EV_DEATH_LOST); return; }   // lost in space: the crew can't arrive in time
  p.ev.push_back({W.day, EV_RESCUED, price});
  p.n.strand++; p.rescueCost.push_back(price); if (W.day - p.born < 360) { p.y1Strand++; p.y1Rescue += price; }
  const i64 before = debtOf(p); pay(W, price, W.S[r].owner, &p.bk.rescue); if (debtOf(p) > before && p.rescueDebtFrom < 0) p.rescueDebtFrom = W.day;
  if (Agent* a = liveAgent(W, W.S[r].owner)) a->T += std::min(price, std::max<i64>(0, price - (debtOf(p) - before)));
  p.trip = Trip{}; p.at = r; p.sys = r; p.busy = W.day + days; refuel(W, r);
}

// ---------- actions ----------
static bool startTrip(World& W, int to, bool push, int purpose) {
  Pilot& p = W.pl; const i64 rEff = rEffPm(W, p.ship); const Routes R = routesFrom(W, p.at, rEff, push, p.dh); Plan pl = planOf(W, R, p.at, to, rEff, p.dh);
  if (!pl.ok) return false;
  const i64 legs = (i64)pl.legs.size(), need = dockable(W, to) ? legs : 2 * legs;   // point of no return: a dead destination needs the way back too
  if (need > p.fuel) return false;
  lawDepart(W, p.at, to);
  p.trip.legs = pl.legs; p.trip.idx = 0; p.trip.dest = to; p.trip.purpose = purpose; p.trip.active = 1;
  p.trip.legDue = W.day + legDays(W, pl.legs[0].len, pl.legs[0].blind, pl.legs[0].to);
  p.at = -1; p.n.trips++;
  const i64 dJ = edist(W.net.st[pl.legs.front().from].pos, W.net.st[to].pos) * 1000 / std::max<i64>(1, W.net.J);
  if (W.day - p.born < 360) p.y1Jobs[dJ <= 1000 ? 0 : dJ <= 2000 ? 1 : 2]++;
  return true;
}
static void applyAction(World& W, const Action& a) {
  Pilot& p = W.pl; const auto& P = W.CP;
  if (a.kind == A_START) { if (p.alive || (size_t)a.f[0] >= W.S.size()) return; p = Pilot{}; p.alive = 1; p.at = p.sys = (int)a.f[0]; p.born = W.day; p.wallet = P.plStartWallet;
    p.ship.hull = P_HULL_MEDIUM; p.ship.drive = P.stockDrivePm >= 3000 ? 4 : P.stockDrivePm >= 2000 ? 3 : P.stockDrivePm >= 1500 ? 2 : P.stockDrivePm >= 1000 ? 1 : 0; p.ship.tank = P.stockTank; p.ship.hold = P.stockHold; p.fuel = p.ship.tank; return; }
  if (a.kind == A_LAW) { lawAction(W, a); return; }
  if (a.kind == A_DROP) { if (p.alive && p.job.id >= 0 && !p.trip.active) { p.n.failed++; p.closed[p.job.id] = W.day; p.job = Job{}; } return; }
  if (a.kind == A_POLICY) { if (p.alive) { p.lawPolicy = (int)clampI(a.f[0], 0, 3); p.runPolicy = a.f[1] ? 1 : 0; } return; }
  if (a.kind == A_REPAY) { if (!p.alive) return; i64 g = std::min({a.f[0], std::max<i64>(0, p.wallet), debtOf(p)}); if (g <= 0) return; p.wallet -= g;
    for (auto& d : p.debt) { const i64 x = std::min(g, d.owed); d.owed -= x; g -= x; if (Agent* h = liveAgent(W, d.house)) h->T += x; if (!g) break; } settleDebtClock(W); return; }
  if (!p.alive || p.trip.active || p.busy > W.day || p.at < 0) return;
  Station& s = W.S[p.at];
  if (a.kind == A_REFIT) { Agent* o = liveAgent(W, s.owner); if (!o || p.holdN) return; Ship ns; if (!refitShip(W, p.ship, (int)a.f[0], (int)a.f[1], ns)) return;
    const i64 price = refitPrice(W, (int)a.f[0], (int)a.f[1]); if (p.wallet < price) return;
    p.wallet -= price; o->T += price; p.bk.refits += price; p.ship = ns; p.fuel = std::min(p.fuel, p.ship.tank); p.refitSpend += price; p.n.refits++;
    const i64 steps[3] = {2500, 7500, 15000}; for (int k = 0; k < 3; ++k) if (p.stepDay[k] < 0 && p.refitSpend >= steps[k]) p.stepDay[k] = W.day - p.born; return; }
  if (a.kind == A_REPAIR) { if (p.dh >= 1000 && p.lsh >= 1000) return; const i64 cost = P.repairFee + P.repairFull * (1000 - p.dh) / 1000 + P.repairLsFull * (1000 - p.lsh) / 1000; pay(W, cost, s.owner, &p.bk.repairs); p.dh = 1000; p.lsh = 1000; p.n.repairs++; return; }
  if (a.kind == A_SELL_CHART) { Agent* h = liveAgent(W, (int)a.f[0]); if (p.charts.empty() || !h || h->kind != K_BASE || s.owner != h->id || h->T < P.chartPrice) return;
    h->T -= P.chartPrice; p.charts.pop_back(); p.bk.charts += P.chartPrice; income(W, P.chartPrice, false); if (auto f = W.fac.find(h->id); f != W.fac.end()) f->second.chart += P.chartMonths; return; }
  if (a.kind == A_TAKE) { if (p.job.id >= 0 || p.closed.count(a.f[0])) return; for (const auto& j : board(W).jobs) if (j.id == a.f[0]) { if (j.from >= 0 && j.from != p.at) return; p.job = j; p.job.taken = W.day; return; } return; }
  if (a.kind == A_SURVEY) { const int b = (int)a.f[0]; const i64 key = (i64)p.at * 64 + b; Agent* h = liveAgent(W, s.owner);
    if (b < 0 || (size_t)b >= s.dep.size() || std::find(p.surveyed.begin(), p.surveyed.end(), key) != p.surveyed.end() || !h || h->kind != K_BASE || h->T < P.bodyPay) return;
    h->T -= P.bodyPay; p.surveyed.push_back(key); p.bk.bodies += P.bodyPay; pay(W, 2 * P.subLight, s.owner, &p.bk.fuel); income(W, P.bodyPay, false); p.busy = W.day + P.surveyDays; p.n.surveys++;
    if (p.firstSurvey < 0) p.firstSurvey = W.day - p.born; return; }
  if (a.kind == A_FLY) { const int to = (int)a.f[0], k = (int)a.f[1]; if (to < 0 || (size_t)to >= W.S.size() || to == p.at) return;
    if (p.holdN > 0 && k != p.holdK && a.f[2] > 0) return;
    i64 n = 0; if (a.f[2] > 0 && k >= 0 && k < 4) { const i64 spare = std::max<i64>(0, s.stock[k] - s.cap[k] * P.plReservePct / 100); n = std::max<i64>(0, std::min({a.f[2], p.ship.hold - p.holdN, spare}));
      while (n > 0 && lotValue(W, s, k, n) > p.wallet) n /= 2; }
    const i64 cost = n > 0 ? lotValue(W, s, k, n) : 0; const int at0 = p.at;
    if (!startTrip(W, to, a.f[3] != 0, (int)a.f[4])) return;
    if (n > 0) { W.S[at0].stock[k] -= n; p.holdK = k; p.holdN += n; p.wallet -= cost; p.bk.buys += cost; }
    return; }
}

// ---------- the pilot's day ----------
static void legDone(World& W) {
  Pilot& p = W.pl; const auto& P = W.CP; Leg2 l = p.trip.legs[(size_t)p.trip.idx]; const i64 rEff = rEffPm(W, p.ship);
  p.fuel--; p.n.legs++; if (l.blind) p.n.blindLegs++; if (l.len > rEff * W.net.J / 1000) p.n.pushLegs++;
  p.dh = std::max<i64>(0, p.dh - P.wearPerLeg); pay(W, P.subLight, W.S[l.from].owner, &p.bk.fuel);
  const i64 risk = legRiskPm(W, l.len, !l.blind, rEff, p.dh);
  const i64 cut = W.rules.shields ? std::min<i64>(100, p.ship.shields * P.shieldCutPct) : 0, dmg = P.misDmg * (100 - cut) / 100;
  const i64 lsCut = W.rules.lifesupport ? 100 - cut : 0;
  if (hashN(W.seed, K_PL, p.n.legs, 1) % 1000 < (uint32_t)risk) { p.n.mis++; p.dh = std::max<i64>(0, p.dh - dmg); p.lsh = std::max<i64>(0, p.lsh - P.lsMisDmg * lsCut / 100); }   // misjump: the jump is spent, the ship stays put
  else {
    p.sys = l.to; p.trip.idx++;
    i64 acc = 0; bool rogue = false; for (const auto& h : W.net.hz) { const i64 dx = W.net.st[l.to].pos.x - h.p.x, dy = W.net.st[l.to].pos.y - h.p.y; if (dx * dx + dy * dy > h.r * h.r) continue; if (h.t == 0) acc += h.acc; if (h.t == 3) { acc += h.acc; rogue = true; } }
    if (acc && hashN(W.seed, K_PL, p.n.legs, 2) % 10000 < (uint32_t)acc) { const bool resisted = rogue && hashN(W.seed, K_PL, p.n.legs, 3) % 1000 < (uint32_t)(W.rules.weapons ? p.ship.weapons * P.weaponResistPm : 0); if (!resisted) { p.n.bad++; p.dh = std::max<i64>(0, p.dh - dmg); p.lsh = std::max<i64>(0, p.lsh - P.lsHazDmg * lsCut / 100);
      if (W.rules.hazdeath && hashN(W.seed, K_PL, p.n.legs, 4) % 1000 < (uint32_t)(P.fatalPm * (100 - cut) / 100)) { endCareer(W, E_HAZARD, EV_DEATH_HAZARD); return; } } }
    if (lawArriveSystem(W, l)) return;   // held after a scan
    if (!l.blind && p.holdN > 0) for (const auto& ln : W.net.lanes) if (ln.j == l.to) { const i64 r = laneToll(W, ln, -1); if (r > 0) { const i64 c = p.holdN * r / 100; pay(W, c, ln.owner, &p.bk.tolls); if (Agent* a = liveAgent(W, ln.owner)) a->T += c; } break; }
  }
  if (p.trip.idx >= (int)p.trip.legs.size()) { arrive(W); return; }
  const i64 left = (i64)p.trip.legs.size() - p.trip.idx;
  if (p.fuel < left && p.fuel > 0) {   // short of jumps: divert to the nearest place that can refuel
    const Routes R = routesFrom(W, p.sys, rEff, true, p.dh); int best = -1;
    for (const auto& s : W.S) if (dockable(W, s.id) && !exiled(W, s.owner) && s.id != p.sys && R.via[s.id] >= 0 && R.jumps[s.id] <= p.fuel && (best < 0 || R.cost[s.id] < R.cost[best])) best = s.id;
    if (best >= 0) { const Plan pl = planOf(W, R, p.sys, best, rEff, p.dh); p.trip.legs = pl.legs; p.trip.idx = 0; p.trip.dest = best; p.trip.purpose = 0; }
  }
  if (p.fuel <= 0) { rescue(W); return; }
  const Leg2& nx = p.trip.legs[(size_t)p.trip.idx]; p.trip.legDue = W.day + legDays(W, nx.len, nx.blind, nx.to);
}
static void pilotDay(World& W) {
  Pilot& p = W.pl; if (!p.alive) return; const auto& P = W.CP;
  lawDay(W);
  if ((W.day - p.born) % 30 == 0 && W.day > p.born) { const int cred = p.at >= 0 ? W.S[p.at].owner : 0; pay(W, P.upkeepYr / 12, isAgent(cred) ? cred : 0, &p.bk.upkeep); }
  if (p.trip.active && W.day >= p.trip.legDue) legDone(W);
  if (p.job.id >= 0 && !p.trip.active && W.day > p.job.deadline) { p.n.failed++; p.closed[p.job.id] = W.day; p.job = Job{}; }
  if (!p.trip.active && p.at < 0 && p.fuel <= 0) rescue(W);   // stranded at a dead station with no fuel
  if (!p.trip.active && p.at < 0 && p.fuel > 0 && p.busy <= W.day) {   // undocked at a dead station: head for the nearest place to refuel
    const i64 rEff = rEffPm(W, p.ship); const Routes R = routesFrom(W, p.sys, rEff, true, p.dh); int best = -1;
    for (const auto& s : W.S) if (dockable(W, s.id) && !exiled(W, s.owner) && R.via[s.id] >= 0 && R.jumps[s.id] <= p.fuel && (best < 0 || R.cost[s.id] < R.cost[best])) best = s.id;
    if (best < 0) rescue(W); else { const Plan pl = planOf(W, R, p.sys, best, rEff, p.dh); p.trip.legs = pl.legs; p.trip.idx = 0; p.trip.dest = best; p.trip.purpose = 0; p.trip.active = 1; p.trip.legDue = W.day + legDays(W, pl.legs[0].len, pl.legs[0].blind, pl.legs[0].to); }
  }
  settleDebtClock(W);
  if (p.alive && p.debtFrom >= 0) { const i64 y = (W.day - p.debtFrom) / 360; const i64 warn[3] = {P.debtWarn1, P.debtWarn2, P.debtWarn3};
    if (p.debtWarned < 3 && y >= warn[p.debtWarned]) { p.ev.push_back({W.day, EV_DEBT_WARN, warn[p.debtWarned]}); p.debtWarned++; }
    if (W.rules.bankrupt && W.day - p.debtFrom >= P.bankruptYears * 360) endCareer(W, E_BANKRUPT, EV_BANKRUPT); }
}

void submit(World& W, const Action& a) { W.inbox.submit(a); }
void tick(World& W) {
  for (const auto& a : W.inbox.drain()) { W.log.push_back({W.day, a}); applyAction(W, a); }
  step(W);
  pilotDay(W);
}
bool ledgerAll(const World& W) {
  i64 stocks = 0, cargo = 0, rawHeld = 0; for (const auto& s : W.S) { for (auto v : s.stock) stocks += v; rawHeld += s.raw[0] + s.raw[1] + s.raw[2]; }
  for (const auto& f : W.F) { for (auto v : f.ret) cargo += v; for (const auto& c : f.cargo) cargo += c[0] + c[1] + c[2] + c[3]; rawHeld += f.raw[0] + f.raw[1] + f.raw[2]; }
  const auto& L = W.led; return L.init + L.prod - L.burn - L.spill - L.waste - L.loot - L.ploss == stocks + cargo + W.pl.holdN && L.rawExt == rawHeld + L.rawConv + L.rawLoot + L.rawSpill;
}
}  // namespace apsis::world
