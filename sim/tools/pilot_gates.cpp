// Layer-3 standing gates with a pilot aboard: replay = live, checkpoint resume, input order (with teeth), and a digest for
// native = WebAssembly. State is compared by full checkpoint bytes (world + pilot).
//   pilot_gates [gates|digest]
#include <algorithm>
#include <cstdio>
#include "../src/pilot/law.hpp"
#include <format>
#include <map>
#include <string>
#include <vector>
#include "../src/pilot/bots.hpp"
using namespace apsis::world;
struct C { uint32_t seed; i64 majors; int start; int style; };
static const C CASES[] = {{333860, 4, 25, 0}, {697804, 4, 30, 5}, {632322, 4, 10, 2}, {552723, 5, 75, 3}, {110478, 4, 10, 8}};   // styles: hauler, reckless, pathfinder, careful, double agent (layer 4 adds the law styles)
static std::string H(const World& W) { const auto b = checkpoint(W); return apsis::hashBytes(b); }
static World base(const C& c) { apsis::Rules R; World W = create(c.seed, c.majors, R); while (W.day < (i64)c.start * 360) step(W); submit(W, apsis::Action{A_START, {0}}); tick(W); return W; }
static int W0house(const C& c) { World W = base(c); return W.S[(size_t)W.pl.at].owner; }
static std::vector<std::string> live(World& W, int style, i64 days, int order /*0 as given, 1 reversed, 2 raw reversed*/) {
  std::vector<std::string> hs; if (order == 2) W.inbox.canonical = false;
  for (i64 d = 0; d < days; ++d) { if (ready(W)) { auto a = decideBot(W, style); if (order) std::reverse(a.begin(), a.end()); for (auto& x : a) submit(W, x); } tick(W); if (W.day % 30 == 0) hs.push_back(H(W)); }
  return hs;
}
int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "gates"; const i64 days = 10 * 360;
  if (mode == "digest") { std::string all; for (const auto& c : CASES) { World W = base(c); for (auto& h : live(W, c.style, days, 0)) all += h; } std::printf("digest %s\n", apsis::hashBytes(std::vector<uint8_t>(all.begin(), all.end())).c_str()); return 0; }
  if (mode == "endstates") {   // layer 4: the three end states (owner, 2026-11-20); each test has its rule-off twin
    int pass = 0, n = 0; std::string J = "{"; auto rec = [&](const char* name, bool ok) { ++n; pass += ok; std::printf("%-78s %s\n", name, ok ? "PASS" : "FAIL"); J += std::format("{}\"{}\":{}", J.size() > 1 ? "," : "", name, ok ? "true" : "false"); };
    auto stranded = [&](bool lsOn, i64 lsh) { World W = base(CASES[0]); W.rules.lifesupport = lsOn; W.CP.lsSuitDays = 5; Pilot& p = W.pl; p.at = -1; p.fuel = 0; p.lsh = lsh; tick(W); return W; };   // suit reserve 5 days, crew response 10 days
    { World W = stranded(true, 1000); rec("stranded next to home, life support healthy: rescued", W.pl.alive && W.pl.end == E_NONE && W.pl.n.strand == 1); }
    { World W = stranded(true, 0); rec("stranded, life support failed (suit only, 5 days; crew needs 10): death lost in space", !W.pl.alive && W.pl.end == E_LOST); }
    { World W = stranded(false, 0); rec("  rule lifesupport off: same case is rescued", W.pl.alive && W.pl.end == E_NONE); }
    auto debtCase = [&](bool on) { World W = base(CASES[1]); W.rules.bankrupt = on; W.pl.debt.push_back(Debt{1, 1000000000}); i64 warned = 0; for (i64 d = 0; d < 11 * 360 && W.pl.alive; ++d) { if (ready(W)) for (auto& x : decideBot(W, 0)) submit(W, x); tick(W); }
      for (const auto& e : W.pl.ev) if (e[1] == EV_DEBT_WARN) warned++; return std::pair{W, warned}; };
    { auto [W, w] = debtCase(true); rec("unpayable debt: warnings at 5, 8 and 9 years, bankrupt at 10 years to the day", !W.pl.alive && W.pl.end == E_BANKRUPT && w == 3 && W.pl.endDay - W.pl.born == 10 * 360 + 1); }
    { auto [W, w] = debtCase(false); rec("  rule bankrupt off: same pilot still flying after 11 years", W.pl.alive && W.pl.end == E_NONE); }
    { World W = base(CASES[1]); W.pl.debt.push_back(Debt{1, 500}); W.pl.wallet = 2000; tick(W); const bool clockOn = W.pl.debtFrom >= 0; submit(W, apsis::Action{A_REPAY, {500}}); tick(W);
      rec("repay clears the debt and resets the bankruptcy clock", clockOn && W.pl.debt.empty() && W.pl.debtFrom < 0 && W.pl.wallet <= 1500); }
    auto hazard = [&](bool on) { World W = base(CASES[2]); W.rules.hazdeath = on; W.CP.fatalPm = 1000; for (i64 d = 0; d < 40 * 360 && W.pl.alive && W.pl.n.bad == 0; ++d) { if (ready(W)) for (auto& x : decideBot(W, 2)) submit(W, x); tick(W); } return W; };
    { World W = hazard(true); rec("fatal chance 1000 per mille: the first bad landing kills", !W.pl.alive && W.pl.end == E_HAZARD && W.pl.n.bad == 1); }
    { World W = hazard(false); rec("  rule hazdeath off: the same landing doesn't", W.pl.alive && W.pl.n.bad >= 1); }
    { World W = base(CASES[0]); Ship a = W.pl.ship, b; const bool ok = refitShip(W, a, P_LS, 0, b); Ship c; const bool sw = refitShip(W, b, P_HULL_HEAVY, 0, c);
      rec("life support refit adds days; a hull swap keeps it", ok && lsDays(W, b, 1000) == lsDays(W, a, 1000) + W.CP.lsPartDays && sw && c.ls == 1); }
    J += "}\n"; if (std::FILE* o = std::fopen("pilot_endstates_l4.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); } std::printf("%d / %d\n", pass, n); return pass == n ? 0 : 1; }
  if (mode == "law") {   // layer 4: the law's standing gates (spec A law, B3, C4) on fixtures
    int pass = 0, n = 0; std::string J = "{"; auto rec = [&](const char* name, bool ok) { ++n; pass += ok; std::printf("%-86s %s\n", name, ok ? "PASS" : "FAIL"); J += std::format("{}\"{}\":{}", J.size() > 1 ? "," : "", name, ok ? "true" : "false"); };
    auto withWarrant = [&](int h) { World W = base(CASES[0]); W.pl.warrants.push_back(Warrant{h, 0, W.day, 300}); W.pl.wallet = 5000; return W; };
    const int h = W0house(CASES[0]);
    { World W = withWarrant(h); const i64 w0 = W.pl.wallet; submit(W, apsis::Action{A_LAW, {0, h}}); tick(W); rec("way out 1: pay the fine clears the warrant", W.pl.warrants.empty() && W.pl.wallet <= w0 - 300); }
    { World W = withWarrant(h); submit(W, apsis::Action{A_LAW, {1, h}}); tick(W); std::vector<Job> u; W.pl.delivTo[0] = 100; underJobs(W, u);
      rec("way out 2: inform clears it, pays, and locks courier and spy work", W.pl.warrants.empty() && W.pl.informUntil > W.day && W.pl.bk.informPay == W.CP.informPay && u.empty()); }
    { World W = withWarrant(h); W.pl.wallet = 0; submit(W, apsis::Action{A_LAW, {2, h}}); tick(W); i64 owed = 0; for (auto& d : W.pl.debt) if (d.house == h) owed += d.owed;
      rec("way out 3: debt clears it; the fine is owed to the house, garnished", W.pl.warrants.empty() && owed >= 300); }
    { World W = base(CASES[0]); Job j; j.id = 1; j.kind = 3; j.issuer = h; j.to = 0; rec("a contraband departure always stands (interest on)", logClass(W, W.pl.at, j, true, true) == LC_STANDS); }
    { World W = base(CASES[0]); const int O = W.S[(size_t)W.pl.at].owner; int R = -1; for (const auto& [id, a] : W.pw.ag) if (a.alive && isRival(W, id, O)) { R = id; break; }
      bool ok = R >= 0; if (ok) { Job j; j.id = 7; j.kind = 0; j.issuer = R; j.to = W.pl.at; ok = logClass(W, W.pl.at, j, false, false) == LC_ODD; }
      for (int i = 0; i < 40 && ok; ++i) { Job j; j.id = 100 + i; j.kind = 0; j.issuer = R; j.good = 0; j.to = W.pl.at; W.pl.job = j; lawDepart(W, W.pl.at, W.pl.at); W.pl.job = Job{}; }
      const HouseLaw* L = lawOf(W.pl, O); rec("an empty departure on a rival's mission is odd: heat, but never evidence or a warrant (40 in a row)", ok && L && L->heat > 0 && W.pl.warrants.empty() && W.pl.n.warrants == 0); }
    { World W = base(CASES[0]); const int O = W.S[(size_t)W.pl.at].owner; W.rules.interest = false; Job j; j.id = 7; j.kind = 0; j.issuer = O; j.to = W.pl.at; rec("  rule interest off: every departure stands", logClass(W, W.pl.at, j, true, false) == LC_STANDS); }
    { World W = base(CASES[0]); const int O = W.S[(size_t)W.pl.at].owner; lawOf(W.pl, O)->exileUntil = W.day + 100; W.pl.dockAnyway = 1; const i64 c2 = W.pl.n.charges2;
      const bool held = lawDock(W, W.pl.at, -1); const HouseLaw* L = lawOf(W.pl, O);
      rec("docking where exiled is a breach: a serious charge and a term restart, never an ending", held && W.pl.alive && W.pl.n.charges2 == c2 + 1 && L->exileUntil == W.day + W.CP.exileDays && W.pl.n.breaches == 1); }
    { World W = base(CASES[0]); const int O = W.S[(size_t)W.pl.at].owner; lawOf(W.pl, O)->exileUntil = W.day + 100; W.CP.scanPm = 1000; Leg2 l; l.from = W.pl.at; l.to = W.pl.at; l.blind = 0; bool held = false; for (int i = 0; i < 50; ++i) { W.pl.n.legs++; held = held || lawArriveSystem(W, l); }
      rec("exile is docking only: 50 scanned arrivals in the exiled house's system, clean ship: no charge", !held && W.pl.n.scans == 50 && W.pl.n.charges1 + W.pl.n.charges2 == 0 && W.pl.n.breaches == 0); }
    { World W = base(CASES[0]); const int O = W.S[(size_t)W.pl.at].owner; for (int i = 0; i < W.CP.graveAt; ++i) { lawOf(W.pl, O)->heat = 0; W.pl.warrants.clear(); lawOf(W.pl, O)->evidence = W.day; lawOf(W.pl, O)->heat = W.CP.heatWarrant - 1; Job j; j.id = 50 + i; j.kind = 3; j.issuer = 9999; W.pl.job = j; W.pl.lastLog = -1; lawDepart(W, W.pl.at, 0); W.pl.job = Job{}; }
      rec("the graveAt-th offence with one house is grave: exile and deportation, the pilot keeps flying", exiled(W, O) && W.pl.alive && W.pl.n.charges3 == 1); }
    J += "}\n"; if (std::FILE* o = std::fopen("pilot_law_l4.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); } std::printf("%d / %d\n", pass, n); return pass == n ? 0 : 1; }
  if (mode == "switches") {   // each layer-3 rule switched off: careers still run with exact ledgers, and the switch has teeth (some careers change)
    struct Sw { const char* name; bool apsis::Rules::*f; }; const Sw SW[] = {{"rescue", &apsis::Rules::rescue}, {"shields", &apsis::Rules::shields}, {"weapons", &apsis::Rules::weapons}, {"law", &apsis::Rules::law}, {"interest", &apsis::Rules::interest}, {"meet", &apsis::Rules::meet},
      {"pirates", &apsis::Rules::pirates}, {"lifesupport", &apsis::Rules::lifesupport}, {"hazdeath", &apsis::Rules::hazdeath}, {"bankrupt", &apsis::Rules::bankrupt}};
    const int STY[3] = {S_HAULER, S_PATHFINDER, S_RECKLESS};
    std::string J = "{"; bool allOk = true;
    for (const auto& sw : SW) { int differ = 0, ledgerOk = 0, n = 0;
      for (const auto& c : CASES) for (int style : STY) { ++n; std::string hOn, hOff; bool led = true;
        for (int off = 0; off < 2; ++off) { apsis::Rules R; if (off) R.*(sw.f) = false; World W = create(c.seed, c.majors, R); while (W.day < (i64)c.start * 360) step(W); submit(W, apsis::Action{A_START, {0}}); tick(W);
          if (sw.f != &apsis::Rules::rescue) { W.pl.ship.shields = 2; W.pl.ship.weapons = 2; }   // fixture, both arms: no bot buys these yet, so fit them to give the switch something to bite on
          for (i64 d = 0; d < 40 * 360; ++d) { if (ready(W)) for (auto& x : decideBot(W, style)) submit(W, x); tick(W); if (W.day % 360 == 0 && !ledgerAll(W)) led = false; }
          World Q = W; Q.rules = apsis::Rules{}; (off ? hOff : hOn) = H(Q); }   // compare the world and pilot, not the switch itself
        differ += hOn != hOff; ledgerOk += led; }
      std::printf("rule %-8s off: ledgers exact %d/%d %s; careers changed by the switch %d/%d\n", sw.name, ledgerOk, n, ledgerOk == n ? "PASS" : "FAIL", differ, n);
      // teeth (changed > 0) is reported, not gated: weapons only bite on a decisive resist roll inside a rogue-body zone
      // owner, layer-3 review note 6: weapons off must change outcomes once pirates attack pilots
      allOk = allOk && ledgerOk == n && (sw.f != &apsis::Rules::weapons || differ > 0); J += std::format("{}\"{}\":{{\"ledger\":[{},{}],\"changed\":[{},{}]}}", J.size() > 1 ? "," : "", sw.name, ledgerOk, n, differ, n); }
    J += "}\n"; if (std::FILE* o = std::fopen("pilot_switches_l4.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); } return allOk ? 0 : 1; }
  int rep = 0, ck = 0, rt = 0, ord = 0, teeth = 0, multi = 0; const int N = (int)std::size(CASES);
  for (const auto& c : CASES) {
    World A = base(c); const World A0 = A; const auto hs = live(A, c.style, days, 0);
    { World B = A0; size_t li = 0; std::vector<std::string> hb; const auto& L = A.log; while (li < L.size() && L[li].day < B.day) ++li;   // replay: the same inputs on the same days, no bot
      for (i64 d = 0; d < days; ++d) { while (li < L.size() && L[li].day == B.day) submit(B, L[li++].a); tick(B); if (B.day % 30 == 0) hb.push_back(H(B)); }
      rep += hb == hs; }
    { World X = A0; live(X, c.style, days / 2, 0); const auto b = checkpoint(X); World Y; restore(b, Y); rt += checkpoint(Y) == b; live(X, c.style, days / 2, 0); live(Y, c.style, days / 2, 0); ck += H(X) == H(Y); }
    { World X = A0; ord += live(X, c.style, days, 1).back() == hs.back(); World Z = A0; teeth += live(Z, c.style, days, 2).back() != hs.back(); }
    i64 m = 0; { std::map<i64, int> per; for (const auto& e : A.log) per[e.day]++; for (auto& [d, k] : per) if (k > 1) ++m; } multi += m > 0;
  }
  std::printf("replay (inputs only, no bot) = live, every month, full state: %d/%d %s\n", rep, N, rep == N ? "PASS" : "FAIL");
  std::printf("checkpoint mid-career resume = uninterrupted: %d/%d %s\n", ck, N, ck == N ? "PASS" : "FAIL");
  std::printf("checkpoint round trip byte-exact (with pilot): %d/%d %s\n", rt, N, rt == N ? "PASS" : "FAIL");
  std::printf("same-tick actions reversed, canonical order: same world: %d/%d %s\n", ord, N, ord == N ? "PASS" : "FAIL");
  std::printf("  teeth: in raw arrival order the reversed actions change the world: %d/%d (careers with several actions in one tick: %d)\n", teeth, N, multi);
  if (std::FILE* o = std::fopen("pilot_gates_l3.json", "wb")) { std::fprintf(o, "{\"replay\":[%d,%d],\"resume\":[%d,%d],\"roundTrip\":[%d,%d],\"order\":[%d,%d],\"teeth\":[%d,%d]}\n", rep, N, ck, N, rt, N, ord, N, teeth, N); std::fclose(o); }
  return rep == N && ck == N && rt == N && ord == N ? 0 : 1;
}
