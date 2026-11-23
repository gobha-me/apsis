// Fixed-work, seeded properties of the real simulation input channel.
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "pilot/pilot.hpp"

namespace {
using namespace apsis::world;
constexpr uint32_t kDefaultSeed = 0x5eed1234;
constexpr int kInputCases = 128;
constexpr int kActionsPerCase = 64;
constexpr int kStreams = 8;
constexpr int kDays = 30;

bool report(bool ok, const char *property, uint32_t seed, int index) {
  if (!ok)
    std::cerr << "property " << property << " failed: seed=" << seed
              << " case=" << index << '\n';
  return ok;
}

i64 signedBits(std::mt19937 &rng) {
  // Avoid an implementation-defined unsigned-to-signed out-of-range cast.
  const uint64_t magnitude =
      (static_cast<uint64_t>(rng()) << 31) | (rng() >> 1);
  return rng() % 2 ? static_cast<i64>(magnitude)
                   : -static_cast<i64>(magnitude) - 1;
}

bool channelProperties(uint32_t seed, std::mt19937 &rng) {
  const std::array<i64, 7> boundaries{
      std::numeric_limits<i64>::min(), -1, 0, 1, 127, 128,
      std::numeric_limits<i64>::max()};
  const std::array<int32_t, 5> kindBoundaries{
      std::numeric_limits<int32_t>::min(), -1, 0, 1,
      std::numeric_limits<int32_t>::max()};
  for (int index = 0; index < kInputCases; ++index) {
    std::vector<apsis::LogEntry> log;
    apsis::Inbox original, shuffled;
    for (int count = 0; count < kActionsPerCase; ++count) {
      apsis::Action action;
      action.kind = static_cast<int32_t>(rng() % 11);
      // The input codec accepts any representable kind. These boundary values
      // stay in the serializer/inbox test and never reach game dispatch.
      if (count < static_cast<int>(kindBoundaries.size()))
        action.kind = kindBoundaries[static_cast<size_t>(count)];
      for (size_t field = 0; field < action.f.size(); ++field)
        action.f[field] =
            count < static_cast<int>(boundaries.size())
                ? boundaries[(static_cast<size_t>(count) + field) %
                             boundaries.size()]
                : signedBits(rng);
      // Include exact duplicate actions: canonicalization must retain
      // multiplicity.
      if (count % 8 == 7)
        action = log.back().a;
      log.push_back({signedBits(rng), action});
      original.submit(action);
    }
    apsis::Writer writer;
    writer.vec(log);
    apsis::Reader reader{writer.b};
    std::vector<apsis::LogEntry> restored;
    reader.vec(restored);
    bool same = !reader.bad && reader.p == writer.b.size() &&
                restored.size() == log.size();
    for (size_t entry = 0; entry < log.size() && same; ++entry)
      same = restored[entry].day == log[entry].day &&
             restored[entry].a == log[entry].a;
    if (!report(same, "signed-action-log-round-trip", seed, index))
      return false;
    apsis::Writer rewritten;
    rewritten.vec(restored);
    if (!report(rewritten.b == writer.b, "serialized-bytes-round-trip", seed,
                index))
      return false;
    // A proper prefix of a serialized nonempty action log cannot decode
    // successfully.
    auto truncated = writer.b;
    truncated.pop_back();
    apsis::Reader shortReader{truncated};
    std::vector<apsis::LogEntry> partial;
    shortReader.vec(partial);
    if (!report(shortReader.bad, "truncated-log-rejected", seed, index))
      return false;
    std::shuffle(log.begin(), log.end(), rng);
    for (const auto &entry : log)
      shuffled.submit(entry.a);
    const auto first = original.drain(), second = shuffled.drain();
    if (!report(first == second && first.size() == kActionsPerCase,
                "canonical-permutation-and-duplicate-count", seed, index))
      return false;
    if (!report(original.drain().empty() && shuffled.drain().empty(),
                "drain-clears-inbox", seed, index))
      return false;
    original.submit(first.back());
    if (!report(original.drain() == std::vector<apsis::Action>{first.back()},
                "inbox-reuse", seed, index))
      return false;
  }
  return true;
}

apsis::Action randomCommand(std::mt19937 &rng, size_t stations) {
  const int kinds[] = {A_POLICY, A_REPAY, A_REPAIR, A_SURVEY, A_FLY, A_DROP};
  apsis::Action action{kinds[rng() % std::size(kinds)], {}};
  switch (action.kind) {
  case A_POLICY:
    action.f[0] = static_cast<i64>(rng() % 12) - 5;
    action.f[1] = static_cast<i64>(rng() % 3) - 1;
    break;
  case A_REPAY:
    action.f[0] = static_cast<i64>(rng() % 1101) - 100;
    break;
  case A_SURVEY:
    action.f[0] = static_cast<i64>(rng() % 10) - 1;
    break;
  case A_FLY:
    action.f[0] = static_cast<i64>(rng() % (stations + 3)) - 1;
    action.f[1] = static_cast<i64>(rng() % 6) - 1;
    action.f[2] = static_cast<i64>(rng() % 26) - 5;
    action.f[3] = rng() % 2;
    action.f[4] = rng() % 2;
    break;
  default:
    break;
  }
  return action;
}

bool simulationStreams(uint32_t seed, std::mt19937 &rng) {
  for (int index = 0; index < kStreams; ++index) {
    const uint32_t worldSeed = rng();
    World direct = create(worldSeed, 4, apsis::Rules{});
    World permuted = create(worldSeed, 4, apsis::Rules{});
    for (int day = 0; day < kDays; ++day) {
      std::vector<apsis::Action> actions;
      if (day == 0)
        actions.push_back({A_START, {0}});
      for (int count = 0; count < 4; ++count)
        actions.push_back(randomCommand(rng, direct.S.size()));
      for (const auto &action : actions)
        submit(direct, action);
      std::reverse(actions.begin(), actions.end());
      for (const auto &action : actions)
        submit(permuted, action);
      tick(direct);
      tick(permuted);
      const int sample = index * kDays + day;
      if (!report(ledgerAll(direct) && ledgerAll(permuted),
                  "random-command-ledgers", seed, sample))
        return false;
      const auto bytes = checkpoint(direct);
      if (!report(bytes == checkpoint(permuted), "random-command-permutation",
                  seed, sample))
        return false;
      if (day == kDays / 2) {
        World resumed;
        if (!report(restore(bytes, resumed) && checkpoint(resumed) == bytes,
                    "random-stream-save-load", seed, sample))
          return false;
        permuted = std::move(resumed);
      }
    }
  }
  return true;
}
} // namespace

int main(int argc, char **argv) {
  uint32_t seed = kDefaultSeed;
  if (argc == 2) {
    const std::string value = argv[1];
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), seed);
    if (error != std::errc{} || end != value.data() + value.size())
      return 2;
  } else if (argc != 1)
    return 2;
  std::mt19937 rng(seed);
  if (!channelProperties(seed, rng) || !simulationStreams(seed, rng))
    return 1;
  std::cout << "{\"passed\":true,\"seed\":" << seed
            << ",\"inputCases\":" << kInputCases
            << ",\"serializedActions\":" << kInputCases * kActionsPerCase
            << ",\"streams\":" << kStreams << ",\"daysPerStream\":" << kDays
            << "}\n";
  return 0;
}
