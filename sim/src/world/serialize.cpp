// World checkpoint. One field list per struct, visited by a writer or a reader, so both directions share it.
// Distances are rebuilt from the lanes on restore (floyd is deterministic). The trade-path cache is state:
// v0.20 reuses cached paths until the path version changes, so a fresh cache after a restore would change the world.
#include <climits>
#include <string>
#include "gen.hpp"
#include "sim.hpp"
#include "../core/serial.hpp"

namespace apsis {   // found by argument-dependent lookup on apsis::Params
template <class IO> void fields(IO& f, Params& x) {
#define RULE(n, d, l, m) f(x.rules.n);
#define PARAM(n, d, u, m, fr) f(x.n);
#include "../core/params.def"
#undef RULE
#undef PARAM
}
template <class IO> void fields(IO& f, Intent& x) { f.all(x.due, x.kind, x.object, x.faction, x.priority, x.seq, x.aux, x.amount, x.bid); }
}  // namespace apsis

namespace apsis::world {
namespace {
template <class S> struct IO {
  S& s;
  void operator()(int64_t& v) { s.num(v); }
  void operator()(int& v) { int64_t x = v; s.num(x); if (x < INT32_MIN || x > INT32_MAX) s.bad(); v = (int)x; }   // reading is strict: a value that would not write back the same bytes is refused
  void operator()(uint32_t& v) { int64_t x = v; s.num(x); if (x < 0 || x > (int64_t)UINT32_MAX) s.bad(); v = (uint32_t)x; }
  void operator()(bool& v) { int64_t x = v; s.num(x); if (x != 0 && x != 1) s.bad(); v = x != 0; }
  void operator()(char& v) { int64_t x = v; s.num(x); if (x < CHAR_MIN || x > CHAR_MAX) s.bad(); v = (char)x; }
  void operator()(std::string& v) { int64_t n = (int64_t)v.size(); s.num(n); v.resize((size_t)n); for (auto& c : v) (*this)(c); }
  template <class T> void operator()(std::vector<T>& v) { int64_t n = (int64_t)v.size(); s.num(n); if (n < 0 || n > (1 << 26)) { s.bad(); return; } v.resize((size_t)n); for (auto& x : v) (*this)(x); }
  template <class T, size_t N> void operator()(std::array<T, N>& a) { for (auto& x : a) (*this)(x); }
  template <class A, class B> void operator()(std::pair<A, B>& p) { (*this)(p.first); (*this)(p.second); }
  template <class K, class V> void operator()(std::map<K, V>& m) {
    int64_t n = (int64_t)m.size(); s.num(n);
    if (S::reading) { m.clear(); for (int64_t i = 0; i < n && !s.isBad(); ++i) { K k{}; V v{}; (*this)(k); (*this)(v); if (!m.emplace(k, v).second) s.bad(); } }
    else for (auto& [k, v] : m) { K kk = k; (*this)(kk); (*this)(v); } }
  template <class T> requires requires(IO& io, T& t) { fields(io, t); } void operator()(T& t) { fields(*this, t); }
  template <class... T> void all(T&... t) { ((*this)(t), ...); }
};
struct W_ { static constexpr bool reading = false; Writer w; void num(int64_t& v) { w.i(v); } void bad() {} bool isBad() const { return false; } };
struct R_ { static constexpr bool reading = true; Reader r; void num(int64_t& v) { v = r.i(); } void bad() { r.bad = true; } bool isBad() const { return r.bad; } };
}  // namespace

template <class IO> void fields(IO& f, Pt& x) { f.all(x.x, x.y, x.ok); }
template <class IO> void fields(IO& f, NetSt& x) { f.all(x.id, x.tier, x.parent, x.owner, x.region, x.pos, x.name); }
template <class IO> void fields(IO& f, Lane& x) { f.all(x.a, x.b, x.len, x.lng, x.hw, x.hd, x.ha, x.j, x.owner, x.bypass); }
template <class IO> void fields(IO& f, Leg& x) { f.all(x.a, x.b, x.from, x.len, x.stopIdx); }
template <class IO> void fields(IO& f, Route& x) { f.all(x.id, x.hub, x.owner, x.stops, x.legs, x.len, x.far, x.freighters, x.base, x.relay); }
template <class IO> void fields(IO& f, Hazard& x) { f.all(x.t, x.p, x.r, x.acc, x.sub, x.per, x.win, x.rift, x.id); }
template <class IO> void fields(IO& f, Net& x) { f.all(x.st, x.lanes, x.routes, x.hubs, x.majors, x.fairHubs, x.junctions, x.sectorHomes, x.n0, x.coreHome, x.side, x.laneN0, x.J, x.hz, x.spec); }
template <class IO> void fields(IO& f, Dep& x) { f.all(x.t, x.rate0, x.res0, x.res, x.half, x.quarter, x.b); }
template <class IO> void fields(IO& f, Acct& x) { f.all(x.cost, x.ret, x.dv, x.rawOut, x.owned); }
template <class IO> void fields(IO& f, Station& x) { f.all(x.id, x.tier, x.owner, x.basePop, x.pop, x.burn0, x.cap, x.prod0, x.stock, x.outNow, x.ema, x.stage, x.dwell, x.wear, x.mood, x.leftNormal, x.dep, x.raw, x.rawCap, x.acct, x.so3, x.born, x.dropped); }
template <class IO> void fields(IO& f, Freighter& x) { f.all(x.id, x.r, x.leg, x.t, x.cargo, x.ret, x.raw, x.dead, x.retire); }
template <class IO> void fields(IO& f, Unit& x) { f.all(x.s); }
template <class IO> void fields(IO& f, Group& x) { f.all(x.s, x.pos, x.last); }
template <class IO> void fields(IO& f, RouteSt& x) { f.all(x.P, x.Q, x.V, x.dv, x.loot, x.tHist, x.groups, x.units); }
template <class IO> void fields(IO& f, Agent& x) { f.all(x.id, x.kind, x.alive, x.seat, x.patron, x.up, x.deep, x.weak, x.T, x.S, x.born, x.died, x.peak, x.n, x.crush, x.fallC, x.weakC, x.riseC, x.fell, x.rose, x.fort, x.drop); }
template <class IO> void fields(IO& f, Contest& x) { f.all(x.uid, x.x, x.a, x.d, x.m, x.margin, x.dsum, x.luck, x.min, x.core); }
template <class IO> void fields(IO& f, Contract& x) { f.all(x.carrier, x.until); }
template <class IO> void fields(IO& f, PathSig& x) { f.all(x.owners, x.rate, x.contract, x.alive); }
template <class IO> void fields(IO& f, Pw& x) { f.all(x.ag, x.rel, x.con, x.trade, x.paid, x.nextId, x.relVol, x.relN, x.clogN, x.conUid, x.split, x.rate, x.tollAcc, x.contract, x.jPrev, x.trend, x.majors0, x.pathV, x.hasPathSig, x.pathSig,
  x.contests, x.wins, x.holds, x.breaks, x.upSpawn, x.falls, x.rises, x.buys, x.pathsV, x.paths); }
template <class IO> void fields(IO& f, Fac& x) { f.all(x.base, x.survey, x.t, x.chart, x.charted); }
template <class IO> void fields(IO& f, Bk& x) { f.all(x.ds, x.u, x.kp, x.upkeep, x.lapsed, x.relit, x.lit, x.darkCut, x.prosp); }
template <class IO> void fields(IO& f, Dark& x) { f.all(x.edgeUntil, x.g, x.cur, x.curOn, x.flow, x.lastNpc, x.edge, x.edgeHit, x.ps, x.edgeN, x.edgeCost); }
template <class IO> void fields(IO& f, Led& x) { f.all(x.init, x.prod, x.burn, x.spill, x.waste, x.loot, x.rawExt, x.rawConv, x.rawLoot, x.rawSpill, x.ploss); }
template <class IO> void fields(IO& f, Ship& x) { f.all(x.hull, x.drive, x.tank, x.hold, x.comp, x.shields, x.weapons, x.ls); }
template <class IO> void fields(IO& f, Leg2& x) { f.all(x.from, x.to, x.len, x.blind); }
template <class IO> void fields(IO& f, Job& x) { f.all(x.id, x.kind, x.to, x.good, x.units, x.reward, x.deadline, x.issuer, x.about, x.meet, x.from, x.taken); }
template <class IO> void fields(IO& f, HouseLaw& x) { f.all(x.house, x.heat, x.rem, x.off, x.exileUntil, x.evidence, x.evSerious); }
template <class IO> void fields(IO& f, Warrant& x) { f.all(x.house, x.serious, x.day, x.fine); }
template <class IO> void fields(IO& f, Recov& x) { f.all(x.tier, x.house, x.day, x.w0); }
template <class IO> void fields(IO& f, Trip& x) { f.all(x.legs, x.idx, x.legDue, x.dest, x.purpose, x.active); }
template <class IO> void fields(IO& f, Debt& x) { f.all(x.house, x.owed); }
template <class IO> void fields(IO& f, Pilot::Books& x) { f.all(x.fines, x.informPay, x.underPay, x.payDirect, x.daysDirect, x.payMeet, x.daysMeet, x.sales, x.buys, x.tolls, x.rewards, x.charts, x.bodies, x.upkeep, x.fuel, x.advFees, x.repairs, x.rescue, x.refits, x.garnished, x.lostUnits); }
template <class IO> void fields(IO& f, Pilot::N& x) { f.all(x.deps, x.smugDeps, x.smugLogged, x.logsStand, x.logsTol, x.logsOdd, x.logsLost, x.oddCharges, x.scans, x.charges1, x.charges2, x.charges3, x.warrants, x.seized, x.runs, x.escapes, x.breaches, x.informs, x.served,
    x.pirates, x.resisted, x.looted, x.meets, x.meetsMissed, x.under, x.trips, x.legs, x.blindLegs, x.pushLegs, x.mis, x.bad, x.strand, x.surveys, x.darkRuns, x.done, x.failed, x.refits, x.advances, x.fuelDebt, x.repairs, x.dockedIdle); }
template <class IO> void fields(IO& f, Pilot& x) { f.all(x.alive, x.at, x.sys, x.born, x.wallet, x.busy, x.dh, x.fuel, x.advance, x.lsh, x.end, x.endDay, x.debtFrom, x.debtWarned, x.ev, x.law, x.warrants, x.recOpen, x.recDone, x.informUntil, x.under, x.lastLog, x.tripWorth, x.jailUntil, x.lawPolicy, x.runPolicy, x.dockAnyway, x.delivTo, x.ship, x.holdK, x.holdN, x.job, x.trip, x.debt, x.rescueDebtFrom, x.charts, x.charted, x.surveyed,
  x.refitSpend, x.stepDay, x.closed, x.bk, x.n, x.rescueCost, x.debtClear, x.firstSurvey, x.firstDarkRun, x.y1Jobs, x.y1Strand, x.y1Rescue, x.y1Idle); }
template <class IO> void fields(IO& f, WParams& x) {
#define WP(T, n, d, m) f(x.n);
#include "wparams.def"
#undef WP
}
template <class IO> void fields(IO& f, World& W) {
  W.CP.rules = W.rules;
  f.all(W.P, W.CP, W.seed, W.net, W.day, W.n0, W.S, W.F, W.RT, W.fac, W.facIds, W.pw, W.bk, W.dark, W.led, W.pl, W.queue, W.crCost, W.crRet, W.crDv, W.crOe, W.crSet);
  W.rules = W.CP.rules;
}

std::vector<uint8_t> checkpoint(const World& W0) { World& W = const_cast<World&>(W0); W_ s; IO<W_> io{s}; fields(io, W); return s.w.b; }
bool restore(const std::vector<uint8_t>& b, World& out) {
  R_ s{{b}}; IO<R_> io{s}; World W; fields(io, W);
  if (s.r.bad || s.r.p != b.size()) return false;
  floyd(W.net); out = std::move(W); return true;
}

}  // namespace apsis::world
