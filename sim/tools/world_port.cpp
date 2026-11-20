// Layer-2 port gate: the C++ world with intents off must give v0.20's monthly state hashes.
//   world_port seed majors years [--dump D]       prints monthly hashes (or the dump at day D)
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/world/sim.hpp"
using namespace apsis::world;
int main(int argc, char** argv) {
  const uint32_t seed = (uint32_t)std::stoul(argv[1]); const i64 majors = std::stoll(argv[2]), years = std::stoll(argv[3]);
  apsis::Rules R; R.intents = false;
  WParams P; if (std::getenv("APSIS_TEETH")) P.relDecay += 1;   // teeth: one number off by one must break the match
  World W = create(seed, majors, R, P);
  if (argc > 5 && std::string(argv[4]) == "--dump") { const i64 D = std::stoll(argv[5]); while (W.day < D) step(W); std::fputs(dump(W).c_str(), stdout); return 0; }
  std::printf("%s\n", hashState(W).c_str()); int bad = 0;
  for (i64 d = 1; d <= years * 360; ++d) { step(W); if (!ledgerOk(W)) ++bad; if (d % 30 == 0) std::printf("%s\n", hashState(W).c_str()); }
  std::fprintf(stderr, "ledger bad ticks %d\n", bad);
}
