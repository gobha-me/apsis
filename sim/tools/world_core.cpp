// Layer-2 standing gates on the real world: checkpoint resume and round trip (intents off and on), a teeth test,
// a digest for native = WebAssembly, and world-only speed.
//   world_core gates | digest | bench
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include "../src/world/sim.hpp"
using namespace apsis::world;
static const std::pair<uint32_t, int64_t> SEEDS[] = {{333860, 4}, {110478, 4}, {697804, 4}, {57206, 4}, {632322, 4}, {517908, 4}, {552723, 5}};
static World mk(uint32_t s, int64_t m, bool on) { apsis::Rules R; R.intents = on; return create(s, m, R); }
int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "gates";
  if (mode == "digest") { std::string all; for (auto [s, m] : SEEDS) { World W = mk(s, m, true); for (int64_t d = 1; d <= 100 * 360; ++d) { step(W); if (d % 30 == 0) all += hashState(W); } }
    std::printf("digest %s\n", apsis::hashBytes(std::vector<uint8_t>(all.begin(), all.end())).c_str()); return 0; }
  if (mode == "J") { for (auto [s, m] : SEEDS) { World W = mk(s, m, true); std::printf("%u J=%lld map units (side %lld, v0.20 stock reach %lld = %.2f J)\n", s, (long long)W.net.J, (long long)W.net.side, (long long)W.P.hopNpc, (double)W.P.hopNpc / (double)W.net.J); } return 0; }
  if (mode == "bench") { for (int on = 0; on < 2; ++on) { double best = 1e18; for (int rep = 0; rep < 3; ++rep) { auto t0 = std::chrono::steady_clock::now();
        for (auto [s, m] : SEEDS) { World W = mk(s, m, on); for (int64_t d = 0; d < 300 * 360; ++d) step(W); }
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); if (t < best) best = t; }
      std::printf("{\"mode\":\"%s\",\"years\":2100,\"s\":%.3f,\"yearsPerS\":%.0f,\"msPerYear\":%.3f}\n", on ? "v2 intents on" : "v2 intents off (= v0.20)", best, 2100 / best, best * 1000 / 2100); }
    return 0; }
  int ok = 0, okB = 0, n = 0; std::string notes;
  for (int on = 0; on < 2; ++on) for (auto [s, m] : SEEDS) { ++n;
    World A = mk(s, m, on); while (A.day < 100 * 360) step(A); const auto ck = checkpoint(A);
    World B; restore(ck, B); okB += checkpoint(B) == ck;
    while (A.day < 200 * 360) step(A); while (B.day < 200 * 360) step(B);
    ok += checkpoint(A) == checkpoint(B); }
  std::printf("checkpoint at year 100, resume to year 200 = uninterrupted (full state, byte for byte), intents off and on: %d/%d %s\n", ok, n, ok == n ? "PASS" : "FAIL");
  std::printf("checkpoint round trip byte-exact: %d/%d %s\n", okB, n, okB == n ? "PASS" : "FAIL");
  World T = mk(333860, 4, true); while (T.day < 50 * 360) step(T); auto ck = checkpoint(T); int caught = 0;
  for (int k = 0; k < 20; ++k) { auto bad = ck; bad[(size_t)apsis::hashN(7, k) % bad.size()] ^= 0x10; World X; if (!restore(bad, X) || checkpoint(X) != ck) ++caught; }
  std::printf("  teeth: 20 single-bit corruptions, each refused or a different world: %d/20 %s\n", caught, caught == 20 ? "PASS" : "FAIL");
  if (std::FILE* o = std::fopen("world_core_l2.json", "wb")) { std::fprintf(o, "{\"checkpointResume\":[%d,%d],\"roundTrip\":[%d,%d],\"teeth\":[%d,20]}\n", ok, n, okB, n, caught); std::fclose(o); }
  return ok == n && okB == n && caught == 20 ? 0 : 1;
}
