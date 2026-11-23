// Test-only bounded scenarios using the imported simulation's public APIs.
#include "pilot/bots.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

using namespace apsis::world;

static uint64_t number(std::string_view text, uint64_t maximum) {
  uint64_t value = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      value > maximum)
    throw std::invalid_argument("invalid or out-of-range integer");
  return value;
}

// Operational limit on these collections, not a new game-rule entity limit.
static size_t entities(const World &w) {
  size_t n = w.S.size() + w.F.size() + w.RT.size() + w.pw.ag.size() +
             w.pw.con.size() + w.queue.size() + w.log.size() +
             w.pl.debt.size() + w.pl.warrants.size() + w.pl.ev.size();
  for (const auto &route : w.RT)
    n += route.groups.size() + route.units.size();
  return n;
}

int main(int argc, char **argv) {
  try {
    if (argc != 5)
      throw std::invalid_argument(
          "usage: sim_invariant_driver SEED SCENARIO DAYS MAX_ENTITIES");
    const auto seed = static_cast<uint32_t>(number(argv[1], UINT32_MAX));
    const std::string_view scenario = argv[2];
    const auto days = number(argv[3], 36000);
    const auto limit = number(argv[4], 10000000);
    if (!days || !limit)
      throw std::invalid_argument("DAYS and MAX_ENTITIES must be positive");
    int style = -1;
    if (scenario == "hauler")
      style = S_HAULER;
    else if (scenario == "crisis")
      style = S_CRISIS;
    else if (scenario == "pathfinder")
      style = S_PATHFINDER;
    else if (scenario != "world" && scenario != "repayment" &&
             scenario != "bankruptcy")
      throw std::invalid_argument("unknown scenario");

    // Persisted numeric state is integral; NaN/Inf are not representable.
    static_assert(std::is_integral_v<decltype(World::day)>);
    static_assert(std::is_integral_v<decltype(Pilot::wallet)>);
    World w = create(seed, 4, apsis::Rules{});
    bool ledger = true, bounded = true, clock = true;
    size_t peak = 0;
    i64 checked = 0, firstFailure = -1;
    const auto check = [&] {
      ++checked;
      ledger &= ledgerAll(w);
      peak = std::max(peak, entities(w));
      bounded &= peak <= limit;
      if ((!ledger || !bounded || !clock) && firstFailure < 0)
        firstFailure = w.day;
      return ledger && bounded && clock;
    };
    check();
    const auto advance = [&](bool pilot) {
      const auto previous = w.day;
      if (pilot)
        tick(w);
      else
        step(w);
      clock &= w.day == previous + 1;
      return check();
    };
    // Mature-world fixture for the bankruptcy boundary. Rules and parameters
    // retain defaults; only fixture state (an old, unpayable debt) is injected.
    if (scenario == "bankruptcy")
      while (w.day < w.CP.bankruptYears * 360 && ledger && bounded && clock)
        advance(false);
    if (scenario != "world" && ledger && bounded) {
      submit(w, apsis::Action{A_START, {0}});
      advance(true);
      if (scenario == "repayment") {
        w.pl.wallet = 2000;
        w.pl.debt.push_back(Debt{1, 500});
        submit(w, apsis::Action{A_REPAY, {500}});
      } else if (scenario == "bankruptcy") {
        w.pl.debt.push_back(Debt{1, 1000000000});
        w.pl.debtFrom = 0;
      }
    }
    uint64_t simulated = 0;
    bool outcome = scenario != "repayment" && scenario != "bankruptcy";
    for (; simulated < days && ledger && bounded && clock; ++simulated) {
      if (style >= 0 && ready(w))
        for (const auto &action : decideBot(w, style))
          submit(w, action);
      const bool advanced = advance(scenario != "world");
      // Reachability is an observed event, not a condition that must persist
      // forever: an idle pilot can incur new upkeep debt after repaying.
      outcome |= scenario == "repayment" && w.pl.debt.empty() && w.pl.alive;
      outcome |=
          scenario == "bankruptcy" && !w.pl.alive && w.pl.end == E_BANKRUPT;
      if (!advanced) {
        ++simulated;
        break;
      }
    }
    const bool passed = ledger && bounded && clock && outcome;
    std::cout << "{\"schema_version\":1,\"seed\":" << seed << ",\"scenario\":\""
              << scenario << "\",\"requested_days\":" << days
              << ",\"simulated_days\":" << simulated
              << ",\"final_day\":" << w.day << ",\"states_checked\":" << checked
              << ",\"peak_entities\":" << peak
              << ",\"first_failure_day\":" << firstFailure
              << ",\"end_state\":" << w.pl.end
              << ",\"deliveries\":" << w.pl.n.done
              << ",\"numeric_domain\":\"integer_checkpoint\",\"state_hash\":\""
              << apsis::hashBytes(checkpoint(w))
              << "\",\"checks\":{\"resource_conservation\":"
              << (ledger ? "true" : "false")
              << ",\"bounded_entities\":" << (bounded ? "true" : "false")
              << ",\"day_progression\":" << (clock ? "true" : "false")
              << ",\"outcome_reachable\":" << (outcome ? "true" : "false")
              << "}}\n";
    return !std::cout ? 2 : passed ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "sim_invariant_driver: " << error.what() << '\n';
    return 2;
  }
}
