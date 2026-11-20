// v2 per-pilot-year speed, single thread, career time only (the world before the start is excluded), tank 6.
#include <chrono>
#include <cstdio>
#include "../src/pilot/bots.hpp"
using namespace apsis::world;
struct Seed { uint32_t seed; i64 majors; int starts[3]; };
static const Seed SEEDS[] = {{333860, 4, {25, 95, 160}}, {110478, 4, {10, 80, 160}}, {697804, 4, {30, 90, 180}}, {57206, 4, {53, 64, 75}}, {632322, 4, {10, 90, 165}}, {517908, 4, {80, 160, 230}}, {552723, 5, {75, 120, 200}}};
int main() {
  for (int style = 0; style < 3; ++style) { double s = 0; int n = 0;
    for (const auto& sd : SEEDS) for (int y : sd.starts) { apsis::Rules R; World W = create(sd.seed, sd.majors, R); while (W.day < (i64)y * 360) step(W);
      const auto t0 = std::chrono::steady_clock::now(); submit(W, apsis::Action{A_START, {0}}); tick(W);
      for (i64 d = 0; d < 40 * 360; ++d) { if (ready(W)) for (const auto& a : decideBot(W, style)) submit(W, a); tick(W); }
      s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); ++n; }
    std::printf("{\"version\":\"v2 native\",\"style\":\"%s\",\"careers\":%d,\"pilotYears\":%d,\"msPerPilotYear\":%.2f}\n", kStyleName[style], n, n * 40, s * 1000 / (n * 40)); }
}
