// Layer-2 gates and reports: world character with intents off (= v0.20) and on, per curated seed.
#include <chrono>
#include <cstdio>
#include <format>
#include <map>
#include <string>
#include <vector>
#include "../src/world/sim.hpp"
#include "../src/world/intents.hpp"
using namespace apsis::world;
struct Seed { uint32_t seed; i64 majors; int starts[3]; const char* cond; };
static const Seed SEEDS[] = {{333860, 4, {25, 95, 160}, "worst-10% churn"}, {110478, 4, {10, 80, 160}, "stable Core, quiet lanes"}, {697804, 4, {30, 90, 180}, "split Core, prices cycling"},
  {57206, 4, {53, 64, 75}, "weakened Core that recovers"}, {632322, 4, {10, 90, 165}, "contested tollgate that churns"}, {517908, 4, {80, 160, 230}, "shortage-heavy economy"}, {552723, 5, {75, 120, 200}, "5-major stress world"}};
static std::string J;   // json out
#include <cstdlib>
static i64 envI(const char* k, i64 d) { const char* v = std::getenv(k); return v ? std::atoll(v) : d; }
// sensitivity runs (vetoable placeholders): APSIS_STANCE (stance-type delay), APSIS_SUPPLY (0 = no supply intents)
static World mk(const Seed& s, bool intents, i64 bidStepPm = 50, int threads = 1) { apsis::Rules R; R.intents = intents; WParams P; P.bidStepPm = bidStepPm; if (!envI("APSIS_SUPPLY", 1)) P.supplyLinePct = 0;
  World W = create(s.seed, s.majors, R, P); W.CP.delayStrategic = envI("APSIS_STANCE", W.CP.delayStrategic); W.threads = threads; return W; }

struct Char { i64 jflips = 0, weakOn = 0, shortOn = 0, falls = 0, rises = 0, ups = 0, contests = 0, wins = 0, breaks = 0, rateCh = 0, contractCh = 0, majorsEnd = 0, stationsEnd = 0, lapsed = 0, relit = 0, warMonths = 0, ledgerBad = 0; std::string monthDigest; };
static Char character(World W, i64 years) {
  Char c; std::vector<int> jprev; for (int j : W.net.junctions) jprev.push_back(W.S[j].owner); int weak = 0; std::vector<i64> stg(W.S.size(), 0); std::map<int, i64> rate = W.pw.rate; auto contract = W.pw.contract;
  std::string all;
  for (i64 d = 1; d <= years * 360; ++d) { step(W); if (!ledgerOk(W)) c.ledgerBad++;
    if (d % 30) continue;
    all += hashState(W);
    bool fl = false; for (size_t i = 0; i < W.net.junctions.size(); ++i) { const int o = W.S[W.net.junctions[i]].owner; if (o != jprev[i]) { fl = true; jprev[i] = o; } } c.jflips += fl;
    const int wk = W.pw.ag.count(0) ? W.pw.ag.at(0).weak : 0; if (wk && !weak) c.weakOn++; weak = wk;
    bool sh = false; if (stg.size() < W.S.size()) stg.resize(W.S.size(), 0); for (const auto& s : W.S) { if (s.owner != 3 && s.stage > 0 && !stg[s.id]) sh = true; stg[s.id] = s.owner != 3 ? s.stage : 0; } c.shortOn += sh;
    if (W.pw.rate != rate) { c.rateCh++; rate = W.pw.rate; }
    bool cc = W.pw.contract.size() != contract.size(); if (!cc) for (auto a = W.pw.contract.begin(), b = contract.begin(); a != W.pw.contract.end(); ++a, ++b) if (a->first != b->first || a->second.carrier != b->second.carrier) cc = true; if (cc) { c.contractCh++; contract = W.pw.contract; }
    int p = 0; const int h = generalWarPairs(W, &p); if (h * 2 >= p && p >= 2) c.warMonths++; }
  c.falls = W.pw.falls; c.rises = W.pw.rises; c.ups = W.pw.upSpawn; c.contests = W.pw.contests; c.wins = W.pw.wins; c.breaks = W.pw.breaks; c.lapsed = W.bk.lapsed; c.relit = W.bk.relit;
  for (const auto& [id, a] : W.pw.ag) if (a.alive && a.kind == K_BASE) c.majorsEnd++; for (const auto& s : W.S) if (s.owner != 3) c.stationsEnd++;
  c.monthDigest = apsis::hashBytes(std::vector<uint8_t>(all.begin(), all.end()));
  return c;
}
static std::string cj(const Char& c) { return std::format("{{\"jflips\":{},\"weakOn\":{},\"shortOn\":{},\"falls\":{},\"rises\":{},\"upstarts\":{},\"contests\":{},\"wins\":{},\"breaks\":{},\"rateCh\":{},\"contractCh\":{},\"majorsEnd\":{},\"stationsEnd\":{},\"lapsed\":{},\"relit\":{},\"warMonths\":{},\"ledgerBad\":{}}}",
  c.jflips, c.weakOn, c.shortOn, c.falls, c.rises, c.ups, c.contests, c.wins, c.breaks, c.rateCh, c.contractCh, c.majorsEnd, c.stationsEnd, c.lapsed, c.relit, c.warMonths, c.ledgerBad); }

static int sample(int N) {   // a wide sample: N seeds, start at year 20, 40-year window; general war and conflict, off vs on
  struct T { int war = 0; i64 contests = 0, wins = 0, flips = 0, warMonths = 0; } tot[2]; std::string rows;
  for (int i = 0; i < N; ++i) { const uint32_t seed = apsis::hashN(20261118, i); const i64 majors = i % 5 == 4 ? 5 : 4; int first[2] = {-1, -1};
    for (int on = 0; on < 2; ++on) { apsis::Rules R; R.intents = on; World W = create(seed, majors, R); W.CP.delayStrategic = envI("APSIS_STANCE", W.CP.delayStrategic);
      while (W.day < 20 * 360) step(W); const i64 c0 = W.pw.contests, w0 = W.pw.wins; std::vector<int> jp; for (int j : W.net.junctions) jp.push_back(W.S[j].owner);
      for (int m = 1; m <= 480; ++m) { for (int d = 0; d < 30; ++d) step(W); int p = 0; const int h = generalWarPairs(W, &p); if (h * 2 >= p && p >= 2) { tot[on].warMonths++; if (first[on] < 0) first[on] = m; }
        bool fl = false; for (size_t k = 0; k < jp.size(); ++k) { const int o = W.S[W.net.junctions[k]].owner; if (o != jp[k]) { fl = true; jp[k] = o; } } tot[on].flips += fl; }
      tot[on].war += first[on] > 0; tot[on].contests += W.pw.contests - c0; tot[on].wins += W.pw.wins - w0; }
    rows += std::format("{}[{},{},{}]", i ? "," : "", seed, first[0], first[1]); }
  for (int on = 0; on < 2; ++on) std::printf("sample %d seeds, intents %s: general war in %d careers (%.1f%%), contests %lld, captures %lld, junction-change months %lld, months at general war %lld\n", N, on ? "on " : "off", tot[on].war, 100.0 * tot[on].war / N, (long long)tot[on].contests, (long long)tot[on].wins, (long long)tot[on].flips, (long long)tot[on].warMonths);
  std::string js = std::format("{{\"n\":{},\"off\":{{\"war\":{},\"contests\":{},\"wins\":{},\"flips\":{},\"warMonths\":{}}},\"on\":{{\"war\":{},\"contests\":{},\"wins\":{},\"flips\":{},\"warMonths\":{}}},\"rows\":[{}]}}\n",
    N, tot[0].war, tot[0].contests, tot[0].wins, tot[0].flips, tot[0].warMonths, tot[1].war, tot[1].contests, tot[1].wins, tot[1].flips, tot[1].warMonths, rows);
  if (std::FILE* o = std::fopen(std::format("world_sample_{}.json", envI("APSIS_STANCE", 10)).c_str(), "wb")) { std::fputs(js.c_str(), o); std::fclose(o); }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 2 && std::string(argv[1]) == "sample") return sample(std::stoi(argv[2]));
  const i64 years = argc > 1 ? std::stoll(argv[1]) : 300; J = "{";
  // 1. general war from the v0.20 start points (burn20 idle): 40 years, success = half of all great-house pairs hostile in one month
  for (int mode = 0; mode < 3; ++mode) { const bool on = mode > 0; const i64 bsp = mode == 2 ? 0 : 50; int wars = 0, n = 0, stableWars = 0; std::string rows;
    for (const auto& s : SEEDS) for (int st : s.starts) { World W = mk(s, on, bsp); while (W.day < (i64)st * 360) step(W); int first = -1;
      for (int m = 1; m <= 480; ++m) { for (int d = 0; d < 30; ++d) step(W); int p = 0; const int h = generalWarPairs(W, &p); if (h * 2 >= p && p >= 2 && first < 0) first = m; }
      ++n; wars += first > 0; if (s.seed == 110478) stableWars += first > 0; rows += std::format("{}[{},{},{}]", rows.empty() ? "" : ",", s.seed, st, first); }
    const char* lab = mode == 0 ? "off" : mode == 1 ? "on" : "on_exact";
    std::printf("general war, intents %-9s %d/%d careers (stable Core %d/3)\n", lab, wars, n, stableWars);
    J += std::format("\"war_{}\":{{\"wars\":{},\"n\":{},\"stable\":{},\"rows\":[{}]}},", lab, wars, n, stableWars, rows); }
  // 2. character over the whole run, per seed: off (= v0.20), on, on with exact bids
  J += "\"character\":["; i64 offMs = 0, onMs = 0;
  for (size_t i = 0; i < std::size(SEEDS); ++i) { const auto& s = SEEDS[i];
    auto t0 = std::chrono::steady_clock::now(); const Char off = character(mk(s, false), years); auto t1 = std::chrono::steady_clock::now(); const Char on = character(mk(s, true), years); auto t2 = std::chrono::steady_clock::now();
    const Char ex = character(mk(s, true, 0), years);
    offMs += std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(); onMs += std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::printf("%u off %s\n%u on  %s\n%u ex  %s  (same as rounded: %s)\n", s.seed, cj(off).c_str(), s.seed, cj(on).c_str(), s.seed, cj(ex).c_str(), ex.monthDigest == on.monthDigest ? "yes" : "no");
    J += std::format("{}{{\"seed\":{},\"cond\":\"{}\",\"off\":{},\"on\":{},\"exact\":{},\"exactSameWorld\":{}}}", i ? "," : "", s.seed, s.cond, cj(off), cj(on), cj(ex), ex.monthDigest == on.monthDigest ? "true" : "false"); }
  J += std::format("],\"charMs\":{{\"off\":{},\"on\":{},\"years\":{}}},", offMs, onMs, years * (i64)std::size(SEEDS));
  // 3. intents: what was decided and cleared, feed-one cases, delays, bids, threads
  { i64 intents = 0, zero = 0, feedOne = 0, contested = 0, ties = 0, moved = 0, below = 0, bids = 0, deciderDays = 0, maxDec = 0, pool = 0, ns = 0; std::map<int, i64> byKind, cleared; std::vector<std::string> log; int thrOk = 0;
    for (const auto& s : SEEDS) { World W = mk(s, true); for (i64 d = 0; d < years * 360; ++d) step(W); const auto& c = W.cov;
      intents += c.intents; zero += c.zeroDelay; feedOne += c.feedOne; contested += c.contested; ties += c.ties; moved += c.rankMoved; below += c.belowStep; bids += c.bids; deciderDays += c.deciderDays; maxDec = std::max(maxDec, c.maxDeciders); ns += c.decideNs;
      for (auto& [k, v] : c.byKind) byKind[k] += v; for (auto& [k, v] : c.cleared) cleared[k] += v; for (auto& l : c.log) if (log.size() < 3) log.push_back(std::format("seed {} {}", s.seed, l));
      std::string ref = hashState(W); bool same = true;
      for (int t : {2, 8}) { World X = mk(s, true, 50, t); for (i64 d = 0; d < years * 360; ++d) step(X); if (hashState(X) != ref) same = false; pool += X.cov.poolRuns; }
      thrOk += same; }
    std::printf("intents %lld (zero-delay %lld); feed-one cases %lld; contested clearings %lld, ties %lld, winner moved by rounding %lld, bids below one step %lld of %lld\n", (long long)intents, (long long)zero, (long long)feedOne, (long long)contested, (long long)ties, (long long)moved, (long long)below, (long long)bids);
    std::printf("threads 1/2/8 identical: %d/7; pool ran on %lld decision days (2 and 8 threads); %lld decision days, most deciders on one day %lld; decide time %.1f ms total\n", thrOk, (long long)pool, (long long)deciderDays, (long long)maxDec, ns / 1e6);
    for (auto& l : log) std::printf("  %s\n", l.c_str());
    std::string bk, cl; for (auto& [k, v] : byKind) bk += std::format("{}\"{}\":{}", bk.empty() ? "" : ",", kIntentName[k], v); for (auto& [k, v] : cleared) cl += std::format("{}\"{}\":{}", cl.empty() ? "" : ",", kIntentName[k], v);
    std::string lj; for (auto& l : log) lj += std::format("{}\"{}\"", lj.empty() ? "" : ",", l);
    J += std::format("\"intents\":{{\"n\":{},\"zeroDelay\":{},\"feedOne\":{},\"feedLog\":[{}],\"contested\":{},\"ties\":{},\"rankMoved\":{},\"belowStep\":{},\"bids\":{},\"byKind\":{{{}}},\"cleared\":{{{}}},\"threadsOk\":{},\"poolRuns\":{},\"deciderDays\":{},\"maxDeciders\":{},\"decideMs\":{:.1f}}}", intents, zero, feedOne, lj, contested, ties, moved, below, bids, bk, cl, thrOk, pool, deciderDays, maxDec, ns / 1e6); }
  J += "}\n"; if (std::FILE* o = std::fopen("world_l2.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); }
}
