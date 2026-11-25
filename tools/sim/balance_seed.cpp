// Read-only experiment adapter over the existing public simulation APIs.
#include "pilot/bots.hpp"
#include "pilot/law.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace apsis::world;

static i64 integer(std::string_view text, i64 low, i64 high) {
  i64 value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value < low || value > high)
    throw std::invalid_argument("invalid or out-of-range integer");
  return value;
}

static bool allowed(std::string_view name, i64 value) {
  if (name == "stockTank") return value >= 1 && value <= 12;
  if (name == "mediumMass") return value >= 20 && value <= 120;
  if (name == "heavyMass") return value >= 40 && value <= 200;
  if (name == "mediumSpace") return value >= 60 && value <= 240;
  if (name == "stockDrivePm")
    return value == 500 || value == 1000 || value == 1500 || value == 2000 || value == 3000;
  return false;
}

static void sample(std::ostream& out, const World& world, i64 elapsed) {
  const auto& p = world.pl;
  // Decimal strings preserve exact integral resources for downstream readers.
  out << "{\"elapsedDays\":" << elapsed << ",\"wallet\":\"" << p.wallet
      << "\",\"debt\":\"" << debtOf(p) << "\",\"fuel\":\"" << p.fuel
      << "\",\"worth\":\"" << worthOf(p)
      << "\",\"completedJobs\":\"" << p.n.done << "\"}";
}

int main(int argc, char** argv) {
  try {
    if (argc < 7)
      throw std::invalid_argument("usage: balance_seed SEED MAJORS START_DAY DAYS SAMPLE_EVERY STYLE [NAME=VALUE ...]");
    const auto seed = integer(argv[1], 0, 4294967295LL);
    const auto majors = integer(argv[2], 3, 5);
    const auto start = integer(argv[3], 0, 36000);
    const auto days = integer(argv[4], 1, 7200);
    const auto interval = integer(argv[5], 1, 7200);
    const auto style = integer(argv[6], 0, S_COUNT - 1);
    if ((days + interval - 1) / interval > 1000)
      throw std::invalid_argument("at most 1001 resource samples per run");
    for (const auto* name : {"APSIS_SET", "APSIS_OFF", "APSIS_DRIVE", "APSIS_MSPACE", "APSIS_MMASS"})
      if (std::getenv(name)) throw std::invalid_argument("unset inherited simulation overrides");
    World world = create(static_cast<uint32_t>(seed), majors, apsis::Rules{});
    std::set<std::string> seen;
    for (int i = 7; i < argc; ++i) {
      const std::string arg = argv[i];
      const auto split = arg.find('=');
      if (split == std::string::npos) throw std::invalid_argument("expected NAME=VALUE");
      const auto name = arg.substr(0, split);
      const auto value = integer(std::string_view(arg).substr(split + 1), 1, 3000);
      if (!seen.insert(name).second || !allowed(name, value) || !apsis::setParam(world.CP, name, value))
        throw std::invalid_argument("unknown, duplicate or unsupported parameter value");
    }
    bool ledger = ledgerAll(world);
    while (world.day < start) { step(world); ledger = ledger && ledgerAll(world); }
    submit(world, apsis::Action{A_START, {0}});
    tick(world);
    ledger = ledger && ledgerAll(world);
    const auto born = world.pl.born;
    if (!world.pl.alive || world.day != born + 1)
      throw std::runtime_error("START did not create a living pilot at station zero");
    // START installs the stock ship in a medium hull.
    const auto stockSpace = shipSpace(world, world.pl.ship);
    if (stockSpace > world.CP.mediumSpace)
      throw std::invalid_argument("stock ship needs " + std::to_string(stockSpace) +
                                  " space, exceeds mediumSpace=" + std::to_string(world.CP.mediumSpace));
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"seed\":" << seed << ",\"majors\":" << majors
        << ",\"startDay\":" << start << ",\"days\":" << days
        << ",\"sampleEvery\":" << interval << ",\"style\":" << style
        << ",\"resolvedParams\":{";
    bool first = true;
#define RULE(n, d, l, m)
#define PARAM(n, d, u, m, f) out << (first ? "" : ",") << "\"" #n "\":\"" << world.CP.n << "\""; first = false;
#include "core/params.def"
#undef PARAM
#undef RULE
    out << "},\"rules\":{";
    first = true;
#define RULE(n, d, l, m) out << (first ? "" : ",") << "\"" #n "\":" << (world.rules.n ? "true" : "false"); first = false;
#define PARAM(n, d, u, m, f)
#include "core/params.def"
#undef PARAM
#undef RULE
    out << "},\"curve\":[";
    sample(out, world, 0); // Baseline is immediately after START's tick.
    i64 elapsed = 0;
    i64 firstJob = -1;
    while (elapsed < days && world.pl.alive) {
      if (ready(world)) for (const auto& action : decideBot(world, static_cast<int>(style))) submit(world, action);
      tick(world);
      ++elapsed;
      ledger = ledger && ledgerAll(world);
      if (world.pl.n.done > 0 && firstJob < 0) firstJob = elapsed;
      if (elapsed % interval == 0 || elapsed == days || !world.pl.alive) {
        out << ','; sample(out, world, elapsed);
      }
    }
    out << "],\"elapsedDays\":" << elapsed << ",\"alive\":" << (world.pl.alive ? "true" : "false")
        << ",\"endState\":" << world.pl.end << ",\"outcomeDay\":";
    // Outcome durations use elapsed measured days, matching curve coordinates.
    if (world.pl.end) out << elapsed; else out << "null";
    out << ",\"firstJobDay\":";
    if (firstJob >= 0) out << firstJob; else out << "null";
    out << ",\"ledger\":" << (ledger ? "true" : "false") << "}\n";
    std::cout << out.str();
    return std::cout ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "balance_seed: " << error.what() << '\n';
    return 1;
  }
}
