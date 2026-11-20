// Layer-2 follow-up: 1,000 seeds, start at year 20, 40-year window, intents off (= v0.20) vs on.
// General war, Core stability, supply (fed rate after a dip below the supply line) and station failures.
// Seeds run in parallel as independent experiment branches.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <thread>
#include <vector>
#include "../src/world/sim.hpp"
using namespace apsis::world;
struct R { int war = 0, stableStart = 0, stableWar = 0, coreWeak = 0, coreHomeLost = 0, coreFell = 0, coreContested = 0, coreStationsStart = 0, coreStationsEnd = 0;
  i64 episodes = 0, fed = 0, stationDays = 0, shortDays = 0, fails = 0, contests = 0, wins = 0, relief = 0, asks = 0;
  i64 slow = 0, slowLowSum = 0, slowEmpty = 0, slowDaysSum = 0, fastLowSum = 0; std::array<i64, 1001> slowLowH{};   // dips NOT back within 30 days: lowest stock (per mille of capacity), days below
};
static R run(uint32_t seed, i64 majors, bool on) {
  apsis::Rules Ru; Ru.intents = on; World W = create(seed, majors, Ru); R r;
  while (W.day < 20 * 360) step(W);
  auto coreN = [&] { int n = 0; for (const auto& s : W.S) if (s.owner == 0) ++n; return n; };
  bool contested0 = false; for (const auto& c : W.pw.con) if (c.d == 0) contested0 = true;
  const bool weak0 = W.pw.ag.count(0) && W.pw.ag.at(0).weak; r.stableStart = !weak0 && W.S[W.net.coreHome].owner == 0 && !contested0;
  r.coreStationsStart = coreN(); const i64 f0 = W.cov.stationFails, c0 = W.pw.contests, w0 = W.pw.wins;
  std::vector<std::array<i64, 4>> dip, low;   // day each station/good went below the supply line (or -1), and the lowest stock seen in that dip (per mille of capacity)
  auto slowDip = [&](i64 lowPm, i64 days) { r.slow++; r.slowLowSum += lowPm; r.slowEmpty += lowPm == 0; r.slowDaysSum += days; r.slowLowH[(size_t)std::clamp<i64>(lowPm, 0, 1000)]++; };
  int first = -1;
  for (i64 d = 1; d <= 40 * 360; ++d) { step(W);
    if (dip.size() < W.S.size()) { dip.resize(W.S.size(), {-1, -1, -1, -1}); low.resize(W.S.size(), {1000, 1000, 1000, 1000}); }
    for (const auto& s : W.S) { if (s.owner == 3) continue; r.stationDays++; if (s.stage >= 2) r.shortDays++;
      for (int k = 0; k < 4; ++k) { const bool below = s.stock[k] * 10000 < s.cap[k] * W.P.supplyLinePct; auto& t = dip[s.id][k];
        const i64 pm = s.cap[k] > 0 ? std::clamp<i64>(s.stock[k] * 1000 / s.cap[k], 0, 1000) : 1000; auto& lo = low[s.id][k];
        if (below && t < 0) { t = W.day; r.episodes++; lo = pm; } else if (below) lo = std::min(lo, pm);
        else if (t >= 0) { if (W.day - t <= 30) { r.fed++; r.fastLowSum += lo; } else slowDip(lo, W.day - t); t = -1; } } }
    if (d % 30) continue;
    int p = 0; const int h = generalWarPairs(W, &p); if (h * 2 >= p && p >= 2 && first < 0) first = (int)(d / 30);
    const Agent* c = W.pw.ag.count(0) ? &W.pw.ag.at(0) : nullptr; if (c && c->weak) r.coreWeak = 1; if (W.S[W.net.coreHome].owner != 0) r.coreHomeLost = 1;
    for (const auto& x : W.pw.con) if (x.d == 0) r.coreContested = 1; }
  for (size_t i = 0; i < dip.size(); ++i) for (int k = 0; k < 4; ++k) if (dip[i][k] >= 0 && W.day - dip[i][k] > 30) slowDip(low[i][k], W.day - dip[i][k]);   // still open at the end and already past 30 days
  const Agent* c = W.pw.ag.count(0) ? &W.pw.ag.at(0) : nullptr; r.coreFell = !c || !c->alive || c->kind != K_BASE; r.coreStationsEnd = coreN();
  r.war = first > 0; r.stableWar = r.stableStart && r.war; r.fails = W.cov.stationFails - f0; r.contests = W.pw.contests - c0; r.wins = W.pw.wins - w0; r.relief = W.cov.cleared.count(4) ? W.cov.cleared.at(4) : 0; r.asks = W.cov.byKind.count(4) ? W.cov.byKind.at(4) : 0;
  return r;
}
static std::string ci(int k, int n) { const double z = 1.96, p = (double)k / n, d = 1 + z * z / n, c = (p + z * z / (2 * n)) / d, h = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n)) / d; return std::format("{:.1f}% [{:.1f}, {:.1f}]", 100 * p, 100 * (c - h), 100 * (c + h)); }
int main(int argc, char** argv) {
  const int N = argc > 1 ? std::atoi(argv[1]) : 1000, T = std::max(1u, std::thread::hardware_concurrency());
  std::vector<R> res[2]; res[0].resize((size_t)N); res[1].resize((size_t)N);
  { std::vector<std::jthread> js; for (int t = 0; t < T; ++t) js.emplace_back([&, t] { for (int i = t; i < N; i += T) { const uint32_t seed = apsis::hashN(20261118, i); const i64 m = i % 5 == 4 ? 5 : 4; res[0][(size_t)i] = run(seed, m, false); res[1][(size_t)i] = run(seed, m, true); } }); }
  std::string J = "{";
  for (int on = 0; on < 2; ++on) { R t; for (const auto& r : res[on]) { t.war += r.war; t.stableStart += r.stableStart; t.stableWar += r.stableWar; t.coreWeak += r.coreWeak; t.coreHomeLost += r.coreHomeLost; t.coreFell += r.coreFell; t.coreContested += r.coreContested;
      t.coreStationsStart += r.coreStationsStart; t.coreStationsEnd += r.coreStationsEnd; t.episodes += r.episodes; t.fed += r.fed; t.stationDays += r.stationDays; t.shortDays += r.shortDays; t.fails += r.fails; t.contests += r.contests; t.wins += r.wins; t.relief += r.relief; t.asks += r.asks; t.slow += r.slow; t.slowLowSum += r.slowLowSum; t.slowEmpty += r.slowEmpty; t.slowDaysSum += r.slowDaysSum; t.fastLowSum += r.fastLowSum; for (size_t b = 0; b <= 1000; ++b) t.slowLowH[b] += r.slowLowH[b]; }
    i64 p10 = 0; { i64 acc = 0; for (size_t b = 0; b <= 1000; ++b) { acc += t.slowLowH[b]; if (acc * 10 >= t.slow) { p10 = (i64)b; break; } } }
    const char* lab = on ? "on " : "off";
    std::printf("intents %s | general war %s | Core: weakened %s, home lost %s, fell %s, contested %s, stations %d -> %d | stable-start Cores %d, of which at general war %s\n", lab, ci(t.war, N).c_str(), ci(t.coreWeak, N).c_str(), ci(t.coreHomeLost, N).c_str(), ci(t.coreFell, N).c_str(), ci(t.coreContested, N).c_str(), t.coreStationsStart, t.coreStationsEnd, t.stableStart, ci(t.stableWar, std::max(1, t.stableStart)).c_str());
    std::printf("            supply: %lld dips below the line, %.1f%% back above within 30 days | station-days in shortage (stage 2+) %.2f%% | station failures %.3f per station-year | contests %lld, captures %lld | relief lots fed %lld of %lld asked\n", (long long)t.episodes, 100.0 * t.fed / std::max<i64>(1, t.episodes), 100.0 * t.shortDays / std::max<i64>(1, t.stationDays), 360.0 * t.fails / std::max<i64>(1, t.stationDays), (long long)t.contests, (long long)t.wins, (long long)t.relief, (long long)t.asks);
    std::printf("            dips NOT back within 30 days: %lld (%.1f%%) | lowest stock mean %.1f%% of capacity (deepest tenth at or below %.1f%%) | hit empty %.1f%% | mean days below the line %.0f || recovered dips: lowest stock mean %.1f%%\n", (long long)t.slow, 100.0 * t.slow / std::max<i64>(1, t.episodes), 0.1 * t.slowLowSum / std::max<i64>(1, t.slow), 0.1 * p10, 100.0 * t.slowEmpty / std::max<i64>(1, t.slow), 1.0 * t.slowDaysSum / std::max<i64>(1, t.slow), 0.1 * t.fastLowSum / std::max<i64>(1, t.fed));
    J += std::format("\"{}\":{{\"n\":{},\"war\":{},\"coreWeak\":{},\"coreHomeLost\":{},\"coreFell\":{},\"coreContested\":{},\"coreStationsStart\":{},\"coreStationsEnd\":{},\"stableStart\":{},\"stableWar\":{},\"episodes\":{},\"fed\":{},\"stationDays\":{},\"shortDays\":{},\"fails\":{},\"contests\":{},\"wins\":{},\"relief\":{},\"asks\":{},\"slow\":{},\"slowLowMeanPm\":{},\"slowLowP10Pm\":{},\"slowEmpty\":{},\"slowDaysMean\":{},\"fedLowMeanPm\":{}}},",
      on ? "on" : "off", N, t.war, t.coreWeak, t.coreHomeLost, t.coreFell, t.coreContested, t.coreStationsStart, t.coreStationsEnd, t.stableStart, t.stableWar, t.episodes, t.fed, t.stationDays, t.shortDays, t.fails, t.contests, t.wins, t.relief, t.asks, t.slow, t.slowLowSum / std::max<i64>(1, t.slow), p10, t.slowEmpty, t.slowDaysSum / std::max<i64>(1, t.slow), t.fastLowSum / std::max<i64>(1, t.fed)); }
  J.back() = '}'; J += "\n"; if (std::FILE* o = std::fopen("world_wide_l2.json", "wb")) { std::fputs(J.c_str(), o); std::fclose(o); }
}
