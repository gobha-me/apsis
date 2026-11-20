// Layer 3 experiments: styles x tank sizes x 7 curated seeds x 3 starts x 40 years, with the opening, rescue and debt.
//   pilot_exp [years]      writes pilot_l3.json
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "../src/pilot/bots.hpp"
using namespace apsis::world;
struct Seed { uint32_t seed; i64 majors; int starts[3]; };
static const Seed SEEDS[] = {{333860, 4, {25, 95, 160}}, {110478, 4, {10, 80, 160}}, {697804, 4, {30, 90, 180}}, {57206, 4, {53, 64, 75}}, {632322, 4, {10, 90, 165}}, {517908, 4, {80, 160, 230}}, {552723, 5, {75, 120, 200}}};
static const int TANKS[3] = {3, 4, 6};
struct Career { int style = 0, tank = 0; uint32_t seed = 0; int start = 0; i64 worth0 = 0, worth1 = 0, years = 40; std::array<i64, 3> step{-1, -1, -1};
  i64 strand = 0, mis = 0, legs = 0, pushLegs = 0, blindLegs = 0, done = 0, refits = 0, surveys = 0, darkRuns = 0, advances = 0, fuelDebt = 0, lost = 0, maxIdle = 0, idleY1 = 0; bool stuck = false, ledger = true, longDebt = false; int endState = 0; i64 endDay = -1, bad = 0, lsRefits = 0; std::string law;   // stuck = softlock (no move for > 20 days); longDebt = a debt older than 10 years
  std::vector<i64> rescueCost, debtClear; i64 debtOpenDays = -1, firstSurvey = -1, firstDarkRun = -1, y1Strand = 0, y1Rescue = 0; std::array<i64, 3> y1Jobs{}; i64 sales = 0, rewards = 0, charts = 0, bodies = 0, costs = 0, contested = 0, bids = 0, ties = 0; std::string end; };
static i64 worth(const Pilot& p) { return p.wallet - debtOf(p) - p.advance + p.refitSpend; }
static Career career(World W, int style, int tank, i64 years) {
  W.CP.stockTank = tank; if (const char* v = std::getenv("APSIS_DRIVE")) W.CP.stockDrivePm = std::atoll(v); if (const char* v = std::getenv("APSIS_MSPACE")) W.CP.mediumSpace = std::atoll(v); if (const char* v = std::getenv("APSIS_MMASS")) W.CP.mediumMass = std::atoll(v); Career c; c.style = style; c.tank = tank; c.seed = W.seed; c.start = (int)(W.day / 360); c.years = years;
  const i64 con0 = W.cov.contested, bid0 = W.cov.bids, tie0 = W.cov.ties;
  submit(W, apsis::Action{A_START, {0}}); tick(W); c.worth0 = worth(W.pl); i64 idle = 0;
  const i64 end = W.day + years * 360;
  while (W.day < end && W.pl.alive) { if (ready(W)) { const auto acts = decideBot(W, style); if (acts.empty()) { ++idle; if (W.day - W.pl.born < 360) c.idleY1++; c.maxIdle = std::max(c.maxIdle, idle); } else idle = 0; for (const auto& a : acts) submit(W, a); }
    tick(W); if (!ledgerAll(W)) c.ledger = false; }
  const Pilot& p = W.pl; c.worth1 = worth(p); c.step = p.stepDay; c.strand = p.n.strand; c.mis = p.n.mis; c.legs = p.n.legs; c.pushLegs = p.n.pushLegs; c.blindLegs = p.n.blindLegs; c.done = p.n.done; c.refits = p.n.refits; c.surveys = p.n.surveys; c.darkRuns = p.n.darkRuns;
  c.advances = p.n.advances; c.fuelDebt = p.n.fuelDebt; c.lost = p.bk.lostUnits; c.rescueCost = p.rescueCost; c.debtClear = p.debtClear; if (p.rescueDebtFrom >= 0) c.debtOpenDays = W.day - p.rescueDebtFrom;
  c.firstSurvey = p.firstSurvey; c.firstDarkRun = p.firstDarkRun; c.y1Strand = p.y1Strand; c.y1Rescue = p.y1Rescue; c.y1Jobs = p.y1Jobs; c.sales = p.bk.sales; c.rewards = p.bk.rewards; c.charts = p.bk.charts; c.bodies = p.bk.bodies;
  c.costs = p.bk.buys + p.bk.tolls + p.bk.upkeep + p.bk.fuel + p.bk.advFees + p.bk.repairs + p.bk.rescue;
  const i64 lim = W.CP.stuckDebtYears * 360; for (auto d : c.debtClear) if (d > lim) c.longDebt = true; if (c.debtOpenDays > lim) c.longDebt = true;
  { const auto& n = p.n; std::string rec; for (const auto& r : p.recDone) rec += std::format("{}[{},{}]", rec.empty() ? "" : ",", r[0], r[1]); for (const auto& r : p.recOpen) rec += std::format("{}[{},-{}]", rec.empty() ? "" : ",", r.tier, W.day - r.day);
    c.law = std::format("{{\"deps\":{},\"smugDeps\":{},\"smugLogged\":{},\"stand\":{},\"tol\":{},\"odd\":{},\"lost\":{},\"oddCharges\":{},\"scans\":{},\"minor\":{},\"serious\":{},\"grave\":{},\"warrants\":{},\"seized\":{},\"runs\":{},\"escapes\":{},\"breaches\":{},\"informs\":{},\"served\":{},\"pirates\":{},\"resisted\":{},\"looted\":{},\"meets\":{},\"meetsMissed\":{},\"under\":{},\"fines\":{},\"underPay\":{},\"payDirect\":{},\"daysDirect\":{},\"payMeet\":{},\"daysMeet\":{},\"rec\":[{}]}}",
      n.deps, n.smugDeps, n.smugLogged, n.logsStand, n.logsTol, n.logsOdd, n.logsLost, n.oddCharges, n.scans, n.charges1, n.charges2, n.charges3, n.warrants, n.seized, n.runs, n.escapes, n.breaches, n.informs, n.served, n.pirates, n.resisted, n.looted, n.meets, n.meetsMissed, n.under, p.bk.fines, p.bk.underPay, p.bk.payDirect, p.bk.daysDirect, p.bk.payMeet, p.bk.daysMeet, rec); }
  c.stuck = c.maxIdle > 20; c.endState = p.end; c.endDay = p.end ? p.endDay - p.born : -1; c.bad = p.n.bad; c.lsRefits = p.ship.ls;
  c.contested = W.cov.contested - con0; c.bids = W.cov.bids - bid0; c.ties = W.cov.ties - tie0;
  c.end = std::format("ship hull {} drive {} tank {} hold {}", p.ship.hull, p.ship.drive, p.ship.tank, p.ship.hold);
  return c;
}
// owner item 5: per map and start, homes with a dark system within stock range (no push) / within overreach
struct Range { uint32_t seed; int start, tank; int homes = 0, withinEff = 0, withinPush = 0, roundTrip = 0; i64 darkSystems = 0; };
static Range rangeOf(const World& W, int tank) {
  Range r; r.seed = W.seed; r.start = (int)(W.day / 360); r.tank = tank; Ship s; s.tank = tank; s.drive = W.CP.stockDrivePm >= 3000 ? 4 : W.CP.stockDrivePm >= 2000 ? 3 : W.CP.stockDrivePm >= 1500 ? 2 : W.CP.stockDrivePm >= 1000 ? 1 : 0; s.hold = W.CP.stockHold;
  const i64 re = rangeMap(W, s, false), rp = rangeMap(W, s, true); for (const auto& y : W.S) if (!lit(W, y.id)) r.darkSystems++;
  for (const auto& h : W.S) { if (h.tier != 0) continue; r.homes++; bool e = false, pu = false;
    for (const auto& y : W.S) { if (lit(W, y.id)) continue; const i64 d = edist(W.net.st[h.id].pos, W.net.st[y.id].pos); if (d <= re) e = true; if (d <= rp) pu = true; }
    const Routes RR = routesFrom(W, h.id, rEffPm(W, s), false, 1000); bool rt = false;   // out and back on one tank, never past effective range
    for (const auto& y : W.S) { if (lit(W, y.id) || RR.via[y.id] < 0) continue; if ((dockable(W, y.id) ? RR.jumps[y.id] : 2 * RR.jumps[y.id]) <= tank) rt = true; }
    r.withinEff += e; r.withinPush += pu; r.roundTrip += rt; }
  return r;
}
int main(int argc, char** argv) {
  const i64 years = argc > 1 ? std::stoll(argv[1]) : 40; const int T = std::max(1u, std::thread::hardware_concurrency());
  // wide mode (argv[2] = N): N random seeds, one start each (year 20, 80 or 140), for samples the 7 curated maps can't give (e.g. ~100+ rescue debts)
  const int wide = argc > 2 ? std::atoi(argv[2]) : 0;
  std::vector<Seed> seeds(std::begin(SEEDS), std::end(SEEDS)); if (wide > 0) { seeds.clear(); for (int i = 0; i < wide; ++i) seeds.push_back({apsis::hashN(20261120, (uint64_t)i), i % 5 == 4 ? 5 : 4, {20 + 60 * (i % 3), 0, 0}}); }
  struct Task { int si, st; }; std::vector<Task> tasks; for (int si = 0; si < (int)seeds.size(); ++si) for (int st = 0; st < (wide > 0 ? 1 : 3); ++st) tasks.push_back({si, st});
  std::vector<std::vector<Career>> out(tasks.size()); std::vector<std::vector<Range>> rng(tasks.size()); std::vector<double> secs(tasks.size());
  { std::vector<std::jthread> js; for (int t = 0; t < T; ++t) js.emplace_back([&, t] { for (size_t i = (size_t)t; i < tasks.size(); i += (size_t)T) { const auto& sd = seeds[(size_t)tasks[i].si];
        apsis::Rules R; World W = create(sd.seed, sd.majors, R);
        if (const char* v = std::getenv("APSIS_DRIVE")) W.CP.stockDrivePm = std::atoll(v); if (const char* v = std::getenv("APSIS_MSPACE")) W.CP.mediumSpace = std::atoll(v); if (const char* v = std::getenv("APSIS_MMASS")) W.CP.mediumMass = std::atoll(v);
        if (const char* v = std::getenv("APSIS_OFF")) { std::string_view sv(v); while (!sv.empty()) { const auto c = sv.find(','); const auto nm = sv.substr(0, c); if (!apsis::setRule(W.rules, nm, false)) { std::fprintf(stderr, "APSIS_OFF: bad %.*s\n", (int)nm.size(), nm.data()); std::exit(2); } sv = c == std::string_view::npos ? std::string_view{} : sv.substr(c + 1); } }   // ablation: APSIS_OFF=rule,...
        if (const char* v = std::getenv("APSIS_SET")) { std::string_view sv(v); while (!sv.empty()) { const auto c = sv.find(','); const auto kv = sv.substr(0, c); const auto e = kv.find('='); if (e == std::string_view::npos || !apsis::setParam(W.CP, kv.substr(0, e), std::atoll(std::string(kv.substr(e + 1)).c_str()))) { std::fprintf(stderr, "APSIS_SET: bad %.*s\n", (int)kv.size(), kv.data()); std::exit(2); } sv = c == std::string_view::npos ? std::string_view{} : sv.substr(c + 1); } }   // what-if: APSIS_SET=name=value,...
        while (W.day < (i64)sd.starts[tasks[i].st] * 360) step(W);
        for (int tk : TANKS) rng[i].push_back(rangeOf(W, tk));
        const auto t0 = std::chrono::steady_clock::now();
        // layer-3 styles at tanks 3/4/6 (the sweep); the law styles at the stock tank
        for (int style = 0; style < S_COUNT; ++style) for (int tk : TANKS) if (style < 3 || tk == W.CP.stockTank) out[i].push_back(career(W, style, tk, years));
        secs[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); } }); }
  size_t nCareers = 0; for (auto& v : out) nCareers += v.size();
  std::string J = "{\"careers\":["; bool first = true; double tot = 0; for (auto s : secs) tot += s;
  for (auto& v : out) for (auto& c : v) { auto vec = [](const std::vector<i64>& x) { std::string s; for (size_t i = 0; i < x.size(); ++i) s += (i ? "," : "") + std::to_string(x[i]); return s; };
    J += std::format("{}{{\"style\":\"{}\",\"tank\":{},\"seed\":{},\"start\":{},\"net\":{},\"step\":[{},{},{}],\"strand\":{},\"mis\":{},\"legs\":{},\"push\":{},\"blind\":{},\"done\":{},\"refits\":{},\"surveys\":{},\"darkRuns\":{},\"advances\":{},\"fuelDebt\":{},\"lost\":{},\"maxIdle\":{},\"idleY1\":{},\"stuck\":{},\"ledger\":{},\"rescue\":[{}],\"debtClear\":[{}],\"debtOpen\":{},\"firstSurvey\":{},\"firstDark\":{},\"y1Strand\":{},\"y1Rescue\":{},\"y1Jobs\":[{},{},{}],\"sales\":{},\"rewards\":{},\"charts\":{},\"bodies\":{},\"costs\":{},\"contested\":{},\"bids\":{},\"ties\":{},\"longDebt\":{},\"endState\":{},\"endDay\":{},\"bad\":{},\"law\":{},\"end\":\"{}\"}}",
      first ? "" : ",", kStyleName[c.style], c.tank, c.seed, c.start, (c.worth1 - c.worth0) / c.years, c.step[0], c.step[1], c.step[2], c.strand, c.mis, c.legs, c.pushLegs, c.blindLegs, c.done, c.refits, c.surveys, c.darkRuns, c.advances, c.fuelDebt, c.lost, c.maxIdle, c.idleY1,
      c.stuck ? "true" : "false", c.ledger ? "true" : "false", vec(c.rescueCost), vec(c.debtClear), c.debtOpenDays, c.firstSurvey, c.firstDarkRun, c.y1Strand, c.y1Rescue, c.y1Jobs[0], c.y1Jobs[1], c.y1Jobs[2], c.sales, c.rewards, c.charts, c.bodies, c.costs, c.contested, c.bids, c.ties, c.longDebt ? "true" : "false", c.endState, c.endDay, c.bad, c.law, c.end); first = false; }
  J += "],\"range\":["; first = true;
  for (auto& v : rng) for (auto& r : v) { J += std::format("{}{{\"seed\":{},\"start\":{},\"tank\":{},\"homes\":{},\"eff\":{},\"push\":{},\"roundTrip\":{},\"dark\":{}}}", first ? "" : ",", r.seed, r.start, r.tank, r.homes, r.withinEff, r.withinPush, r.roundTrip, r.darkSystems); first = false; }
  J += std::format("],\"careerSeconds\":{:.2f},\"pilotYears\":{}}}\n", tot, (i64)nCareers * years);
  if (std::FILE* o = std::fopen(wide > 0 ? "pilot_wide.json" : "pilot_l3.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); }
  std::printf("careers %zu, %.1f s of career time (%.2f ms per pilot-year incl. world)\n", nCareers, tot, tot * 1000 / (double)(nCareers * years));
}
