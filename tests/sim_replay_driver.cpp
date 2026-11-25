// Test-only replay oracle. Compare full checkpoint bytes before publishing
// hashes.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "pilot/bots.hpp"

namespace {
using namespace apsis::world;
struct Recipe {
  uint32_t seed = 0;
  int majors = 4;
  i64 days = 0;
  std::vector<apsis::LogEntry> actions;
};

bool number(const std::string &text, i64 &value) {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

bool supportedMajors(i64 majors) {
  // The generator indexes its six-entry mapSide table by major count.
  return majors >= 4 && majors <= 5;
}

bool readRecipe(Recipe &recipe) {
  std::string line;
  bool header = false;
  while (std::getline(std::cin, line)) {
    if (line.size() > 1024)
      return false;
    std::istringstream row(line.substr(0, line.find('#')));
    std::vector<i64> values;
    std::string field;
    while (row >> field) {
      i64 value = 0;
      if (!number(field, value))
        return false;
      values.push_back(value);
    }
    if (values.empty())
      continue;
    if (!header) {
      if (values.size() != 3 || values[0] < 0 ||
          values[0] > std::numeric_limits<uint32_t>::max() ||
          !supportedMajors(values[1]) || values[2] < 2 || values[2] > 360)
        return false;
      recipe.seed = static_cast<uint32_t>(values[0]);
      recipe.majors = static_cast<int>(values[1]);
      recipe.days = values[2];
      header = true;
    } else {
      if (values.size() != 8 || values[0] < 0 || values[0] >= recipe.days ||
          values[1] < A_START || values[1] > A_DROP ||
          recipe.actions.size() >= 4096)
        return false;
      apsis::LogEntry entry{values[0], {static_cast<int32_t>(values[1]), {}}};
      std::copy(values.begin() + 2, values.end(), entry.a.f.begin());
      recipe.actions.push_back(entry);
    }
  }
  std::stable_sort(recipe.actions.begin(), recipe.actions.end(),
                   [](const auto &a, const auto &b) { return a.day < b.day; });
  return header;
}

void submitDay(World &world, const std::vector<apsis::LogEntry> &log,
               bool reverse) {
  std::vector<apsis::Action> actions;
  for (const auto &entry : log)
    if (entry.day == world.day)
      actions.push_back(entry.a);
  if (reverse)
    std::reverse(actions.begin(), actions.end());
  for (const auto &action : actions)
    submit(world, action);
}

int verify(const Recipe &recipe) {
  auto fresh = [&] {
    return create(recipe.seed, recipe.majors, apsis::Rules{});
  };
  World live = fresh();
  std::vector<std::vector<uint8_t>> states{checkpoint(live)};
  for (i64 day = 0; day < recipe.days; ++day) {
    submitDay(live, recipe.actions, false);
    tick(live);
    if (!ledgerAll(live)) {
      std::cerr << "ledger failure at day " << live.day << '\n';
      return 1;
    }
    states.push_back(checkpoint(live));
  }
  // Replay the actual recorded, canonical live log, not the source recipe.
  // New fresh worlds and a reversed same-day submission check every full state.
  for (int mode = 0; mode != 3; ++mode) {
    World replay = fresh();
    for (i64 day = 0; day <= recipe.days; ++day) {
      if (checkpoint(replay) != states[static_cast<size_t>(day)]) {
        std::cerr << "checkpoint mismatch in mode " << mode << " day " << day
                  << '\n';
        return 1;
      }
      if (day == recipe.days)
        break;
      submitDay(replay, mode == 0 ? live.log : recipe.actions, mode == 2);
      tick(replay);
    }
  }
  // Saves occur between drained ticks; the recorded input stream stays
  // external.
  const i64 midpoint = recipe.days / 2;
  World resumed;
  const auto &saved = states[static_cast<size_t>(midpoint)];
  if (!restore(saved, resumed) || checkpoint(resumed) != saved) {
    std::cerr << "checkpoint restore/round-trip failed\n";
    return 1;
  }
  for (i64 day = midpoint; day < recipe.days; ++day) {
    submitDay(resumed, live.log, false);
    tick(resumed);
    if (checkpoint(resumed) != states[static_cast<size_t>(day + 1)]) {
      std::cerr << "resume mismatch at day " << day + 1 << '\n';
      return 1;
    }
  }
  std::cout << "{\"schemaVersion\":1,\"seed\":" << recipe.seed
            << ",\"majors\":" << recipe.majors << ",\"days\":" << recipe.days
            << ",\"actions\":" << live.log.size()
            << ",\"repeatFrames\":" << states.size()
            << ",\"replayFrames\":" << states.size()
            << ",\"reverseFrames\":" << states.size()
            << ",\"resumeFrames\":" << recipe.days - midpoint + 1
            << ",\"roundTripBytes\":" << saved.size()
            << ",\"pilot\":{\"alive\":" << (live.pl.alive ? "true" : "false")
            << ",\"legs\":" << live.pl.n.legs
            << ",\"surveys\":" << live.pl.n.surveys
            << ",\"policy\":" << live.pl.lawPolicy << "},\"stateHashes\":[";
  for (size_t index = 0; index < states.size(); ++index) {
    if (index)
      std::cout << ',';
    std::cout << '"' << apsis::hashBytes(states[index]) << '"';
  }
  std::cout << "]}\n";
  return 0;
}

int record(int argc, char **argv) {
  if (argc != 6)
    return 2;
  std::vector<i64> values(4);
  for (size_t index = 0; index < values.size(); ++index)
    if (!number(argv[index + 2], values[index]))
      return 2;
  if (values[0] < 0 || values[0] > std::numeric_limits<uint32_t>::max() ||
      !supportedMajors(values[1]) || values[2] < 2 || values[2] > 360 ||
      values[3] < 0 || values[3] >= S_COUNT)
    return 2;
  World world =
      create(static_cast<uint32_t>(values[0]), values[1], apsis::Rules{});
  submit(world, {A_START, {0}});
  tick(world);
  while (world.day < values[2]) {
    if (ready(world))
      for (const auto &action : decideBot(world, static_cast<int>(values[3])))
        submit(world, action);
    tick(world);
  }
  std::cout << values[0] << ' ' << values[1] << ' ' << values[2] << '\n';
  for (const auto &entry : world.log) {
    std::cout << entry.day << ' ' << entry.a.kind;
    for (const auto field : entry.a.f)
      std::cout << ' ' << field;
    std::cout << '\n';
  }
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "record")
    return record(argc, argv);
  Recipe recipe;
  if (argc != 1 || !readRecipe(recipe)) {
    std::cerr << "sim_replay_driver: expected bounded replay recipe\n";
    return 2;
  }
  return verify(recipe);
}
