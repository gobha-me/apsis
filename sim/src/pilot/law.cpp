// Layer 4: law and logs, pirates vs pilots, courier and spy work, rendezvous, recovery. See law.hpp and NOTES.md (layer 4a law design).
#include "law.hpp"
#include <algorithm>
#include "../world/ops.hpp"

namespace apsis::world {
static const uint32_t K_LAW = strHash("law"), K_UND = strHash("under");

i64 worthOf(const Pilot& p) { return p.wallet - debtOf(p) - p.advance + p.refitSpend; }

// ---------- relations (B3) ----------
static bool patronLink(const World& W, int a, int b) {
  auto ia = W.pw.ag.find(a), ib = W.pw.ag.find(b);
  return (ia != W.pw.ag.end() && ia->second.patron == b) || (ib != W.pw.ag.end() && ib->second.patron == a);
}
bool isAlly(const World& W, int a, int b) { return a == b || patronLink(W, a, b) || relOf(W, a, b) >= W.CP.allyRel; }
bool isRival(const World& W, int a, int b) {
  if (a == b) return false;
  for (const auto& c : W.pw.con) if ((c.a == a && c.d == b) || (c.a == b && c.d == a)) return true;
  return relOf(W, a, b) <= W.CP.rivalRel;
}
static bool contraband(const Pilot& p) { return p.job.id >= 0 && (p.job.kind == 3 || p.job.kind == 4); }
bool hidden(const Pilot& p) { return p.ship.comp >= 10; }

int logClass(const World& W, int origin, const Job& j, bool cargoOfJobAboard, bool contra) {
  if (!W.rules.interest) return LC_STANDS;
  const int O = W.S[(size_t)origin].owner, I = j.id >= 0 ? j.issuer : O;
  if (contra) return LC_STANDS;   // contraband departures always stand
  if (j.id >= 0 && isRival(W, I, O) && !cargoOfJobAboard) return LC_ODD;
  if (I == O) return LC_OWN;
  const int D = j.id >= 0 && j.to >= 0 ? W.S[(size_t)j.to].owner : O;
  return D == O || isAlly(W, D, O) ? LC_TOLERATED : LC_STANDS;
}

// ---------- the record ----------
HouseLaw* lawOf(Pilot& p, int house) {
  for (auto& l : p.law) if (l.house == house) return &l;
  auto it = std::lower_bound(p.law.begin(), p.law.end(), house, [](const HouseLaw& l, int h) { return l.house < h; });
  HouseLaw n; n.house = house; return &*p.law.insert(it, n);
}
const HouseLaw* lawOf(const Pilot& p, int house) { for (const auto& l : p.law) if (l.house == house) return &l; return nullptr; }
bool wanted(const Pilot& p, int house) { for (const auto& w : p.warrants) if (w.house == house) return true; return false; }
bool exiled(const World& W, int house) { const HouseLaw* l = lawOf(W.pl, house); return l && l->exileUntil > W.day; }
static void dropWarrants(Pilot& p, int h) { std::erase_if(p.warrants, [h](const Warrant& w) { return w.house == h; }); }

i64 depthJ(const World& W, int s) {
  i64 best = INT64_MAX; const Pt& a = W.net.st[(size_t)s].pos;
  for (const auto& y : W.S) if (!isAgent(y.owner)) best = std::min(best, edist(a, W.net.st[(size_t)y.id].pos));
  return best == INT64_MAX ? 10000 : best * 1000 / std::max<i64>(1, W.net.J);
}
i64 scanChancePm(const World& W, int s) {
  const int h = W.S[(size_t)s].owner; if (!W.rules.law || !isAgent(h)) return 0;
  i64 pm = W.CP.scanPm * psOf(W, h) / 100; const HouseLaw* l = lawOf(W.pl, h);
  if (l && l->heat >= W.CP.heatT2) pm *= 4; else if (l && l->heat >= W.CP.heatT1) pm *= 2;
  return std::min<i64>(1000, pm);
}
i64 pirateThreatPm(const World& W, int s) {
  i64 str = 0;
  for (size_t r = 0; r < W.net.routes.size(); ++r) { const auto& R = W.net.routes[r]; if (std::find(R.stops.begin(), R.stops.end(), s) == R.stops.end()) continue;
    i64 g = 0; for (const auto& x : W.RT[r].groups) g += std::max<i64>(0, x.s); str += g / std::max<i64>(1, (i64)R.stops.size()); }
  return std::min(W.CP.pirEncMaxPm, W.CP.pirEncPm * str / std::max<i64>(1, W.P.pirGroup));
}

static void openRecovery(World& W, int tier, int h) { Pilot& p = W.pl; p.recOpen.push_back(Recov{tier, h, W.day, p.tripWorth}); }
static bool offence(World& W, int h) {   // every warrant and charge counts toward escalation (paid ones too); the graveAt-th is grave: exile, never an ending
  Pilot& p = W.pl; HouseLaw* L = lawOf(p, h); L->off++;
  if (L->off < W.CP.graveAt || L->exileUntil > W.day) return false;
  L->exileUntil = W.day + W.CP.exileDays; L->heat = 0; L->evidence = -1; L->evSerious = 0; L->off = 0; dropWarrants(p, h);
  p.n.charges3++; openRecovery(W, 3, h);
  // deported: out of the house's space to the nearest station that will have the pilot, held deportDays
  if (p.at >= 0 || !p.trip.active) { const Pt& a = W.net.st[(size_t)p.sys].pos; int best = -1; i64 bd = 0;
    for (const auto& y : W.S) { if (!dockable(W, y.id) || exiled(W, y.owner)) continue; const i64 d = edist(a, W.net.st[(size_t)y.id].pos); if (best < 0 || d < bd || (d == bd && y.id < best)) { best = y.id; bd = d; } }
    if (best >= 0) { p.trip = Trip{}; p.at = best; p.sys = best; } p.busy = std::max(p.busy, W.day + W.CP.deportDays); p.jailUntil = std::max(p.jailUntil, W.day + W.CP.deportDays); }
  return true;
}
static int jailAt(const World& W, int h) {   // the house's nearest station holds the pilot
  const Pt& a = W.net.st[(size_t)W.pl.sys].pos; int best = -1; i64 bd = 0;
  for (int pass = 0; pass < 2 && best < 0; ++pass)
    for (const auto& y : W.S) { if (!dockable(W, y.id) || (pass == 0 && y.owner != h)) continue; const i64 d = edist(a, W.net.st[(size_t)y.id].pos); if (best < 0 || d < bd || (d == bd && y.id < best)) { best = y.id; bd = d; } }
  return best;
}
static void fineTo(World& W, i64 amt, int h) { Pilot& p = W.pl; const i64 cash = std::min(amt, std::max<i64>(0, p.wallet)); pay(W, amt, h, &p.bk.fines); if (Agent* a = liveAgent(W, h)) a->T += cash; }
static void failJob(Pilot& p, i64 day) { if (p.job.id >= 0) { p.n.failed++; p.closed[p.job.id] = day; p.job = Job{}; } }
// a charge: contraband seized, the fine (wallet, then debt to the house), serious = held for jailDays at the house's nearest station
static void charge(World& W, int h, bool serious, bool countIt) {
  Pilot& p = W.pl; const auto& P = W.CP;
  if (contraband(p)) { p.n.seized++; failJob(p, W.day); }
  fineTo(W, std::min<i64>(P.fineCap, serious ? P.seriousFine : P.minorFine), h);
  (serious ? p.n.charges2 : p.n.charges1)++; HouseLaw* L = lawOf(p, h); L->heat = serious ? P.heatT1 : 0;
  openRecovery(W, serious ? 2 : 1, h);
  if (serious) { const int j = jailAt(W, h); if (j >= 0) { p.trip = Trip{}; p.at = j; p.sys = j; } p.jailUntil = W.day + P.jailDays; p.busy = std::max(p.busy, p.jailUntil); }
  if (countIt) offence(W, h);
}
static void addHeat(World& W, int h, i64 v, bool evidence, bool serious) {
  Pilot& p = W.pl; if (!W.rules.law || !isAgent(h) || v <= 0) return; HouseLaw* L = lawOf(p, h); L->heat += v;
  if (evidence) { L->evidence = W.day; L->evSerious = L->evSerious || serious; }
  if (L->heat >= W.CP.heatWarrant && L->evidence >= 0 && W.day - L->evidence <= W.CP.evidenceDays && !wanted(p, h) && L->exileUntil <= W.day) {
    p.warrants.push_back(Warrant{h, L->evSerious ? 1 : 0, W.day, std::min<i64>(W.CP.fineCap, L->evSerious ? W.CP.seriousFine : W.CP.minorFine)}); p.n.warrants++; L->evidence = -1; L->evSerious = 0;
    offence(W, h); }
}
static void inform(World& W, int h) {
  Pilot& p = W.pl; dropWarrants(p, h); lawOf(p, h)->heat = 0; p.informUntil = W.day + W.CP.informLockDays; p.n.informs++; p.bk.informPay += W.CP.informPay; income(W, W.CP.informPay, false);
  if (Agent* a = liveAgent(W, h)) a->T -= W.CP.informPay;
}
// a warrant is served (a patrol scan or a dock check): the pilot's standing policy answers at once (C4); the game gives the player a window
static bool served(World& W, int h) {
  Pilot& p = W.pl; p.n.served++; Warrant w; for (const auto& x : p.warrants) if (x.house == h) { w = x; break; }
  if (p.lawPolicy == 3 && p.informUntil <= W.day) { inform(W, h); return false; }   // an informer sells out its contacts instead
  // a served warrant is a charge at its tier: minor = the fine (wallet, then debt) and keep flying; serious = the fine and held. It was already counted toward escalation.
  dropWarrants(p, h); charge(W, h, w.serious != 0, false); return w.serious != 0;
}

// ---------- hooks ----------
void lawDepart(World& W, int origin, int dest) {
  Pilot& p = W.pl; const auto& P = W.CP; p.lastLog = origin; p.n.deps++; p.tripWorth = worthOf(p);
  const int O = W.S[(size_t)origin].owner; if (!W.rules.law || !isAgent(O)) return;   // unclaimed stations keep no log
  // a rendezvous beats the departure log (B3): the package never passes through a station, so the departure carries nothing illegal
  const bool contra = contraband(p) && p.job.meet < 0;
  if (contraband(p) && p.job.meet >= 0) p.n.smugDeps++;   // a smuggling departure all the same, just not logged
  const bool cargoOfJob = p.job.id >= 0 && (contra || (p.job.good >= 0 && p.holdK == p.job.good && p.holdN > 0) || p.job.kind == 2);
  Job j = p.job; if (j.id >= 0 && j.to < 0) j.to = dest;
  const int cls = logClass(W, origin, j, cargoOfJob, contra); if (contra) p.n.smugDeps++;
  if (cls == LC_OWN) return;
  if (cls == LC_STANDS) {
    const auto it = p.delivTo.find(O); const bool friendly = it != p.delivTo.end() && it->second >= P.friendDel, discontent = W.S[(size_t)origin].mood < P.discontentMood;
    if ((friendly || discontent) && hashN(W.seed, K_LAW, 1, p.n.deps) % 1000 < (uint32_t)P.logLostPm) { p.n.logsLost++; return; } }
  const i64 base = contra ? (p.job.kind == 4 ? P.heatSpy : P.heatDepart) : P.heatLegal, mult = cls == LC_STANDS ? 1000 : cls == LC_TOLERATED ? P.toleratedPct * 10 : P.oddPct * 10;
  (cls == LC_STANDS ? p.n.logsStand : cls == LC_TOLERATED ? p.n.logsTol : p.n.logsOdd)++;
  if (contra) p.n.smugLogged++;
  addHeat(W, O, base * mult / 1000, contra && cls == LC_STANDS, p.job.kind == 4);   // only a standing contraband log is evidence; odd and legal logs never are
}
bool lawArriveSystem(World& W, const Leg2& l) {
  Pilot& p = W.pl; const auto& P = W.CP;
  const bool atMeet = p.job.id >= 0 && p.job.meet == l.to && p.trip.dest == l.to; if (atMeet) return false;   // no trace at the meet
  if (!l.blind) p.lastLog = l.to;   // a beacon logs the arrival
  if (W.rules.pirates && (p.holdN > 0 || contraband(p))) {
    const i64 pm = pirateThreatPm(W, l.to);
    if (pm > 0 && hashN(W.seed, K_LAW, 2, p.n.legs) % 1000 < (uint32_t)pm) { p.n.pirates++;
      const i64 resist = P.resistBasePm + (W.rules.weapons ? p.ship.weapons * P.weaponResistPm : 0);
      if (hashN(W.seed, K_LAW, 3, p.n.legs) % 1000 < (uint32_t)resist) p.n.resisted++;
      else { p.n.looted++; if (p.holdN > 0) { p.bk.lostUnits += p.holdN; W.led.ploss += p.holdN; p.holdN = 0; p.holdK = -1; } if (contraband(p)) failJob(p, W.day);
        const i64 cut = W.rules.shields ? std::min<i64>(100, p.ship.shields * P.shieldCutPct) : 0, d = P.pirDmg * (100 - cut) / 100;
        p.dh = std::max<i64>(0, p.dh - d); if (W.rules.lifesupport) p.lsh = std::max<i64>(0, p.lsh - d); } } }
  if (!W.rules.law) return false;
  const int h = W.S[(size_t)l.to].owner; const i64 sc = scanChancePm(W, l.to);
  if (sc <= 0 || hashN(W.seed, K_LAW, 4, p.n.legs) % 1000 >= (uint32_t)sc) return false;
  p.n.scans++; const bool want = wanted(p, h), found = contraband(p) && !hidden(p);
  if (!want && !found) return false;
  if (p.runPolicy && p.fuel >= 1) { p.n.runs++; p.fuel--;   // run: a blind jump out (C4)
    if (hashN(W.seed, K_LAW, 5, p.n.legs) % 1000 < (uint32_t)P.runEscapePm) { p.n.escapes++; addHeat(W, h, P.heatRun, true, true); return false; }
    if (want) dropWarrants(p, h); charge(W, h, true, true); return true; }
  if (found) { charge(W, h, true, true); return true; }
  return served(W, h);
}
bool lawDock(World& W, int s, int prevSys) {
  Pilot& p = W.pl; const auto& P = W.CP; const int h = W.S[(size_t)s].owner;
  if (!W.rules.law || !isAgent(h)) { p.lastLog = s; return false; }
  if (exiled(W, h)) { p.n.breaches++; charge(W, h, true, false); lawOf(p, h)->exileUntil = W.day + P.exileDays; return true; }   // a breach: serious charge plus a term restart, never an ending
  if (wanted(p, h) && served(W, h)) return true;
  if (contraband(p) && !hidden(p) && p.job.to != s && hashN(W.seed, K_LAW, 6, p.n.legs) % 1000 < (uint32_t)scanChancePm(W, s)) { charge(W, h, true, true); return true; }
  if (p.lastLog != prevSys) addHeat(W, h, P.heatDepthPerJ * depthJ(W, s) / 1000, false, false);   // arrival with no origin (C7)
  p.lastLog = s; return false;
}
void lawDay(World& W) {
  Pilot& p = W.pl; const auto& P = W.CP; if (!p.alive) return;
  for (auto& l : p.law) { if (l.heat <= 0) continue; const i64 t = l.heat * P.heatFadePm + l.rem; l.heat -= t / 1000; l.rem = t % 1000; if (l.heat < 0) l.heat = 0; }
  for (auto& l : p.law) if (l.exileUntil >= 0 && l.exileUntil <= W.day) l.exileUntil = -1;
  std::erase_if(p.warrants, [&](const Warrant& w) { return !w.serious && W.day - w.day >= P.minorLapseDays; });
  if (p.recOpen.empty()) return;
  const i64 wnow = worthOf(p);
  std::erase_if(p.recOpen, [&](const Recov& r) { const bool free = p.jailUntil <= W.day && !wanted(p, r.house);
    if (free && wnow >= r.w0) { p.recDone.push_back({r.tier, W.day - r.day, r.house}); return true; } return false; });
}
void lawAction(World& W, const Action& a) {
  Pilot& p = W.pl; const int h = (int)a.f[1]; if (!p.alive || !wanted(p, h)) return;
  Warrant w; for (const auto& x : p.warrants) if (x.house == h) { w = x; break; }
  if (a.f[0] == 0) { if (p.wallet < w.fine) return; fineTo(W, w.fine, h); dropWarrants(p, h); }
  else if (a.f[0] == 1) { if (p.informUntil > W.day) return; inform(W, h); }
  else if (a.f[0] == 2) { fineTo(W, w.fine, h); dropWarrants(p, h); }
}

// ---------- courier and spy work (issued by parties other than the station owner, B3) ----------
static int meetFor(const World& W, int to) {   // a system within 1 J of the destination, not the destination
  const Pt& a = W.net.st[(size_t)to].pos; int best = -1; i64 bd = 0;
  for (const auto& y : W.S) { if (y.id == to) continue; const i64 d = edist(a, W.net.st[(size_t)y.id].pos); if (d > W.net.J * W.CP.meetRangePm / 1000) continue; if (best < 0 || d < bd || (d == bd && y.id < best)) { best = y.id; bd = d; } }
  return best;
}
static void withMeet(const World& W, Job j, std::vector<Job>& out) {
  out.push_back(j);
  if (!W.rules.meet) return;
  const bool deep = depthJ(W, j.to) >= W.CP.meetDepthPm;
  const bool stands = true;   // a courier or spy departure always stands (contraband)
  if (!(stands || deep)) return;
  const int m = meetFor(W, j.to); if (m < 0) return;
  const auto& P = W.CP; const i64 extra = P.refuelPerJump + P.daysPerJ * P.refDaily;   // one extra leg: a jump's fuel and its days at the reference rate
  // the contact is due when a stock ship would get there (2 days per J plus docking), and waits meetWait days; the job's deadline is the end of that window
  const i64 dJ = edist(W.net.st[(size_t)j.from].pos, W.net.st[(size_t)m].pos) * 1000 / std::max<i64>(1, W.net.J), due = W.day + P.daysPerJ * dJ / 1000 + 2 * P.dockDays + P.meetWait;
  j.id ^= 0x4d454554; j.meet = m; j.reward = (j.reward + extra) * (100 + P.meetPremiumPct) / 100; j.deadline = due + P.meetWait; out.push_back(j);
}
void underJobs(const World& W, std::vector<Job>& out) {
  const Pilot& p = W.pl; const auto& P = W.CP; if (!p.alive || !W.rules.law || p.informUntil > W.day) return;
  std::vector<int> gs; for (const auto& [id, a] : W.pw.ag) if (a.alive && a.kind == K_BASE) gs.push_back(id);
  if (gs.size() < 2) return;
  i64 deliveries = 0; for (const auto& [o, n] : p.delivTo) deliveries += n;   // every delivery counts (trade and jobs), as the underworld sees a working pilot
  const i64 w = W.day / P.underWindow;
  if (deliveries >= P.courierUnlock)
    for (const auto& s : W.S) { if (!dockable(W, s.id) || !isAgent(s.owner)) continue; if (hashN(W.seed, K_UND, 1, s.id, w) % 100 >= (uint32_t)P.courierPct) continue;
      int I = gs[hashN(W.seed, K_UND, 2, s.id, w) % gs.size()]; if (I == s.owner) I = gs[(std::find(gs.begin(), gs.end(), I) - gs.begin() + 1) % gs.size()];
      if (I == s.owner) continue;
      std::vector<int> dests; for (const auto& y : W.S) if (y.id != s.id && dockable(W, y.id) && isAgent(y.owner)) dests.push_back(y.id);
      if (dests.empty()) continue; const int q = dests[hashN(W.seed, K_UND, 3, s.id, w) % dests.size()];
      Job j; j.id = (i64)hashN(K_UND, 4, s.id, w); j.kind = 3; j.from = s.id; j.to = q; j.issuer = I; j.reward = P.courierBase + edist(W.net.st[(size_t)s.id].pos, W.net.st[(size_t)q].pos) * P.courierPerLen; j.deadline = W.day + P.jobDeadline;
      withMeet(W, j, out); }
  if (deliveries >= P.spyUnlock) {
    auto seat = [&](int h) { int best = -1; for (const auto& y : W.S) if (y.owner == h && dockable(W, y.id) && (best < 0 || y.tier < W.S[(size_t)best].tier)) best = y.id; return best; };
    for (int X : gs) { std::vector<int> o; for (int y : gs) if (y != X) o.push_back(y); if (o.size() < 2) continue;
      const int A = *std::min_element(o.begin(), o.end(), [&](int a, int b) { const i64 ra = relOf(W, X, a), rb = relOf(W, X, b); return ra < rb || (ra == rb && a < b); });
      std::vector<int> o2; for (int y : o) if (y != A) o2.push_back(y);
      const int B = *std::min_element(o2.begin(), o2.end(), [&](int a, int b) { const i64 ra = relOf(W, a, A), rb = relOf(W, b, A); return ra < rb || (ra == rb && a < b); });
      const int from = seat(X), to = seat(B); if (from < 0 || to < 0 || from == to) continue;
      Job j; j.id = (i64)hashN(K_UND, 5, X, w); j.kind = 4; j.from = from; j.to = to; j.issuer = X; j.about = A; j.reward = P.spyReward; j.deadline = W.day + P.jobDeadline;
      withMeet(W, j, out); } }
}
void settleUnder(World& W, int at, bool meetArrival) {
  Pilot& p = W.pl; const auto& P = W.CP; Job& j = p.job; if (j.id < 0 || (j.kind != 3 && j.kind != 4)) return;
  if (meetArrival ? j.meet != at : (j.meet >= 0 || j.to != at)) return;
  if (meetArrival) { const i64 opens = j.deadline - P.meetWait; if (W.day < opens) p.busy = std::max(p.busy, opens); }   // wait for the contact
  const bool ok = W.day <= j.deadline;
  const i64 took = std::max<i64>(1, W.day - j.taken + W.CP.dockDays); (meetArrival ? p.bk.daysMeet : p.bk.daysDirect) += took;
  if (ok) (meetArrival ? p.bk.payMeet : p.bk.payDirect) += j.reward;
  if (ok) { p.bk.underPay += j.reward; p.bk.rewards += j.reward; income(W, j.reward, true); p.n.done++; p.n.under++; if (meetArrival) p.n.meets++; p.delivTo[W.S[(size_t)j.to].owner]++; }
  else { p.n.failed++; if (meetArrival) p.n.meetsMissed++; }   // a missed meet fails the job, with no charge
  p.closed[j.id] = W.day; j = Job{};
}
}  // namespace apsis::world
