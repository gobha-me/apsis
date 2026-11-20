// A bounded tooling replay. Outcome observations live here, never in sim/src/.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "pilot/pilot.hpp"

#ifndef APSIS_VIEWER_REVISION
#define APSIS_VIEWER_REVISION "unknown"
#endif

using namespace apsis::world;

namespace {
struct Run {
  uint32_t seed;
  i64 majors, startDay, horizon, station;
  std::vector<apsis::LogEntry> inputs;
};

// JSON int64 values are decimal strings so browser playback preserves every
// bit.
void value(std::ostream &out, i64 v) { out << '"' << v << '"'; }
template <class T, size_t N>
void value(std::ostream &out, const std::array<T, N> &a) {
  out << '[';
  for (size_t i = 0; i < N; ++i) {
    if (i)
      out << ',';
    value(out, a[i]);
  }
  out << ']';
}

bool readRun(Run &run) {
  std::string line;
  bool header = false;
  while (std::getline(std::cin, line)) {
    line.erase(line.find('#') == std::string::npos ? line.size()
                                                   : line.find('#'));
    std::istringstream row(line);
    row >> std::ws;
    if (row.eof())
      continue;
    if (!header) {
      i64 seed;
      if (!(row >> seed >> run.majors >> run.startDay >> run.horizon >>
            run.station))
        return false;
      if (seed < 0 || seed > std::numeric_limits<uint32_t>::max() ||
          run.majors < 3 || run.majors > 5 || run.startDay < 0 ||
          run.startDay > 36000 || run.horizon < 1 || run.horizon > 3600 ||
          run.station < 0)
        return false;
      run.seed = static_cast<uint32_t>(seed);
      // Exactly one lifetime; START is an ordinary recorded input at the run
      // boundary.
      run.inputs.push_back({run.startDay, {A_START, {run.station}}});
      header = true;
    } else {
      apsis::LogEntry entry{};
      i64 kind;
      if (!(row >> entry.day >> kind))
        return false;
      if (entry.day < run.startDay || entry.day >= run.startDay + run.horizon ||
          kind < A_REFIT || kind > A_DROP)
        return false;
      entry.a.kind = static_cast<int32_t>(kind);
      for (auto &field : entry.a.f)
        if (!(row >> field))
          return false;
      if (run.inputs.size() >= 4096)
        return false;
      run.inputs.push_back(entry);
    }
    row >> std::ws;
    if (!row.eof())
      return false;
  }
  std::stable_sort(run.inputs.begin(), run.inputs.end(),
                   [](const auto &a, const auto &b) {
                     return a.day != b.day ? a.day < b.day : a.a < b.a;
                   });
  return header && !std::cin.bad();
}

template <class T> std::string settingValue(const T &field) {
  std::ostringstream out;
  value(out, field);
  return out.str();
}
void settingFields(std::ostream &out,
                   const std::map<std::string, std::string> &fields) {
  out << '{';
  bool comma = false;
  for (const auto &[name, field] : fields) {
    if (comma)
      out << ',';
    comma = true;
    out << '"' << name << "\":" << field;
  }
  out << '}';
}
void settings(std::ostream &out, const World &world) {
  // Sort setting names so the browser can reconstruct the run-record encoding.
  std::map<std::string, std::string> fields;
#define RULE(n, d, l, m) fields.emplace(#n, world.rules.n ? "true" : "false");
#define PARAM(n, d, u, m, f)
#include "core/params.def"
#undef RULE
#undef PARAM
  out << "{\"rules\":";
  settingFields(out, fields);
  fields.clear();
#define RULE(n, d, l, m)
#define PARAM(n, d, u, m, f) fields.emplace(#n, settingValue(world.CP.n));
#include "core/params.def"
#undef RULE
#undef PARAM
  out << ",\"core\":";
  settingFields(out, fields);
  fields.clear();
#define WP(T, n, d, m) fields.emplace(#n, settingValue(world.P.n));
#include "world/wparams.def"
#undef WP
  out << ",\"world\":";
  settingFields(out, fields);
  out << '}';
}

struct War {
  int hostile = 0, pairs = 0;
  bool active = false;
};
War warOf(const World &world) {
  War war;
  war.hostile = generalWarPairs(world, &war.pairs);
  // Existing world gate meaning, including its at-least-two-pairs condition.
  war.active = war.hostile * 2 >= war.pairs && war.pairs >= 2;
  return war;
}
void warFields(std::ostream &out, const War &war) {
  out << "\"active\":" << (war.active ? "true" : "false")
      << ",\"hostilePairs\":" << war.hostile << ",\"pairs\":" << war.pairs;
}
void frame(std::ostream &out, const World &world, const War &war) {
  const Pilot &p = world.pl;
  out << "{\"day\":" << world.day << ",\"war\":{";
  warFields(out, war);
  out << "},\"pilot\":{\"alive\":" << (p.alive ? "true" : "false")
      << ",\"at\":" << p.at << ",\"system\":" << p.sys
      << ",\"busyUntil\":" << p.busy
      << ",\"tripActive\":" << (p.trip.active ? "true" : "false")
      << ",\"tripDestination\":" << p.trip.dest << ",\"end\":" << p.end
      << ",\"endDay\":" << p.endDay << ",\"wallet\":";
  value(out, p.wallet);
  out << ",\"fuel\":";
  value(out, p.fuel);
  out << ",\"strandings\":";
  value(out, p.n.strand);
  out << ",\"debt\":";
  value(out, debtOf(p));
  out << "}}";
}
} // namespace

int main() {
  Run run{};
  if (!readRun(run)) {
    std::cerr << "Invalid run. Header: seed majors startDay horizon station; "
                 "input rows: day kind f0 f1 f2 f3 f4 f5.\n"
                 "Bounds: uint32 seed, 3..5 majors, startDay 0..36000, horizon "
                 "1..3600, at most 4095 timed inputs; kinds 1..10.\n";
    return 2;
  }
  World world = create(run.seed, run.majors, apsis::Rules{});
  if (static_cast<uint64_t>(run.station) >= world.S.size()) {
    std::cerr << "Start station is absent from this generated world.\n";
    return 2;
  }
  while (world.day < run.startDay)
    step(world);

  std::ostringstream out;
  out << "{\"schema\":\"apsis-viewer-v1\",\"metadata\":{\"adapterRevision\":"
         "\"" APSIS_VIEWER_REVISION
         "\",\"historyGap\":\"Daily tooling observations only. No war "
         "declarations, stranding event kinds, or rescue trajectory exist. "
         "World.log is inputs only.\"},\"run\":";
  std::ostringstream record;
  record << "{\"seed\":" << run.seed << ",\"majors\":" << run.majors
         << ",\"startDay\":" << run.startDay << ",\"horizon\":" << run.horizon
         << ",\"station\":" << run.station << ",\"settings\":";
  settings(record, world);
  record << ",\"inputs\":[";
  for (size_t i = 0; i < run.inputs.size(); ++i) {
    const auto &entry = run.inputs[i];
    if (i)
      record << ',';
    record << "{\"day\":" << entry.day << ",\"kind\":" << entry.a.kind
           << ",\"fields\":";
    value(record, entry.a.f);
    record << '}';
  }
  record << "]}";
  out << record.str();
  std::ostringstream frames;
  frames << '[';
  War previous = warOf(world);
  frame(frames, world,
        previous); // baseline before START, not a declaration/transition
  std::ostringstream timeline;
  timeline << '[';
  bool comma = false;
  auto nextObservation = [&]() {
    if (comma)
      timeline << ',';
    comma = true;
  };
  size_t input = 0, event = 0;
  i64 strandings = world.pl.n.strand;
  for (i64 d = 0; d < run.horizon; ++d) {
    while (input < run.inputs.size() && run.inputs[input].day == world.day)
      submit(world, run.inputs[input++].a);
    tick(world);
    const War war = warOf(world);
    if (war.active != previous.active) {
      nextObservation();
      timeline << "{\"day\":" << world.day
               << ",\"type\":\"general-war\",\"source\":\"derived\",";
      warFields(timeline, war);
      timeline << '}';
    }
    if (world.pl.n.strand != strandings) {
      nextObservation();
      timeline << "{\"day\":" << world.day
               << ",\"type\":\"stranding\",\"source\":\"derived\",\"delta\":";
      value(timeline, world.pl.n.strand - strandings);
      timeline << ",\"total\":";
      value(timeline, world.pl.n.strand);
      timeline << '}';
    }
    while (event < world.pl.ev.size()) {
      const auto &e = world.pl.ev[event++];
      nextObservation();
      timeline << "{\"day\":" << e[0]
               << ",\"type\":\"pilot-event\",\"source\":\"Pilot.ev\",\"kind\":"
               << e[1] << ",\"arg\":";
      value(timeline, e[2]); // EV_RESCUED's actual argument is its price.
      timeline << '}';
    }
    previous = war;
    strandings = world.pl.n.strand;
    frames << ',';
    frame(frames, world, war);
  }
  frames << ']';
  timeline << ']';
  const auto finalCheckpoint = checkpoint(world);
  const auto finalStateHash = apsis::hashBytes(finalCheckpoint);
  // Bind every rendered frame field to the completed checkpoint hash. This is
  // a reproducible consistency check, not an authenticated or signed record.
  const std::string frameEvidence =
      "apsis-viewer-frames-v1\n" + finalStateHash + "\n" + frames.str();
  const auto framesHash = apsis::hashBytes(
      std::vector<uint8_t>(frameEvidence.begin(), frameEvidence.end()));
  const std::string timelineEvidence = "apsis-viewer-timeline-v1\n" +
                                       finalStateHash + "\n" + record.str() +
                                       "\n" + timeline.str();
  const auto timelineHash = apsis::hashBytes(
      std::vector<uint8_t>(timelineEvidence.begin(), timelineEvidence.end()));
  out << ",\"frames\":" << frames.str() << ",\"timeline\":" << timeline.str()
      << ",\"finalCheckpoint\":\"";
  static constexpr char hex[] = "0123456789abcdef";
  for (uint8_t byte : finalCheckpoint)
    out << hex[byte >> 4] << hex[byte & 15];
  out << "\",\"finalStateHash\":\"" << finalStateHash << "\",\"framesHash\":\""
      << framesHash << "\",\"timelineHash\":\"" << timelineHash << "\"}\n";
  std::cout << out.str();
  return std::cout ? 0 : 1;
}
