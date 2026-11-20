// Core speed: sim-days per second of the layer-1 test world, by world size and thread count.
#include <chrono>
#include <cstdio>
#include "../src/proto/proto.hpp"
using namespace apsis; namespace pr = apsis::proto;
int main() {
  struct Size { int st, fa; } sizes[] = {{26, 9}, {200, 64}};
  for (auto sz : sizes) for (int th : {1, 2, 8}) {
    double best = 1e18; const int64_t days = sz.st > 100 ? 7200 : 36000;
    for (int rep = 0; rep < 3; ++rep) {
      pr::World w = pr::create(333860, sz.st, sz.fa, 4); w.threads = th;
      const auto t0 = std::chrono::steady_clock::now();
      for (int64_t d = 0; d < days; ++d) { for (const auto& a : pr::bot(w)) pr::submit(w, a); pr::tick(w); }
      const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); if (s < best) best = s;
    }
    std::printf("{\"stations\":%d,\"factions\":%d,\"threads\":%d,\"years_per_s\":%.0f,\"us_per_day\":%.2f}\n", sz.st, sz.fa, th, days / 360.0 / best, best * 1e6 / days);
  }
}
