// Layer-4a tooling adapter: reuse the exact imported career runner, never its
// threaded main. There is currently no public career helper or seed-offset CLI.
// Renaming main removes C++'s implicit return-0 special case; suppress that
// warning only within the imported file. The renamed function is never called.
#define main apsis_imported_pilot_exp_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "../sim/tools/pilot_exp.cpp"
#pragma GCC diagnostic pop
#undef main

#include <charconv>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

static uint64_t number(std::string_view text, uint64_t maximum) {
  uint64_t value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      value > maximum)
    throw std::invalid_argument("invalid or out-of-range integer");
  return value;
}

static void days(std::ostream &out, const std::vector<i64> &values) {
  out << '[';
  for (size_t i = 0; i < values.size(); ++i)
    out << (i ? "," : "") << values[i];
  out << ']';
}

int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::invalid_argument("usage: rescue_debt_seed YEARS SEED_INDEX");
    const auto years = number(argv[1], 1000000);
    const auto index = number(argv[2], std::numeric_limits<uint32_t>::max());
    if (years == 0)
      throw std::invalid_argument("YEARS must be positive");
    for (const char *name : {"APSIS_SET", "APSIS_OFF", "APSIS_DRIVE",
                             "APSIS_MSPACE", "APSIS_MMASS"})
      if (std::getenv(name))
        throw std::invalid_argument(std::string("unset override ") + name);

    // Match pilot_exp wide mode's seed, major count and rotating start exactly.
    const uint32_t seed = apsis::hashN(20261120, index);
    const int majors = index % 5 == 4 ? 5 : 4;
    const int start = 20 + 60 * (index % 3);
    World world = create(seed, majors, apsis::Rules{});
    while (world.day < static_cast<i64>(start) * 360)
      step(world);

    // Buffer the complete seed so an interrupted helper cannot emit a partial
    // seed that the controller might count. No worker threads are launched.
    std::ostringstream out;
    out << "{\"schemaVersion\":1,\"seedIndex\":" << index
        << ",\"seed\":" << seed << ",\"majors\":" << majors
        << ",\"start\":" << start << ",\"horizonYears\":" << years
        << ",\"tanks\":[3,4,6],\"careers\":[";
    bool first = true;
    for (const int style : {S_HAULER, S_CRISIS, S_PATHFINDER}) {
      for (const int tank : TANKS) {
        const Career c = career(world, style, tank, static_cast<i64>(years));
        out << (first ? "" : ",") << "{\"style\":\"" << kStyleName[style]
            << "\",\"tank\":" << c.tank << ",\"seed\":" << c.seed
            << ",\"start\":" << c.start << ",\"debtClear\":";
        days(out, c.debtClear);
        out << ",\"debtOpen\":" << c.debtOpenDays << ",\"rescue\":";
        days(out, c.rescueCost);
        out << ",\"stuck\":" << (c.stuck ? "true" : "false")
            << ",\"ledger\":" << (c.ledger ? "true" : "false")
            << ",\"endState\":" << c.endState << ",\"endDay\":" << c.endDay
            << '}';
        first = false;
      }
    }
    out << "]}\n";
    std::cout << out.str();
    return std::cout ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "rescue_debt_seed: " << error.what() << '\n';
    return 1;
  }
}
