// Test access to the real adapter's internal helpers; no exporter replacements.
#define main apsis_viewer_export_main
#include "../tools/viewer/export.cpp"
#undef main

namespace {
void rawFrame(const World &world) {
  int pairs = 0;
  const int hostile = generalWarPairs(world, &pairs);
  std::cout << "{\"day\":" << world.day << ",\"pairs\":" << pairs
            << ",\"hostilePairs\":" << hostile << ",\"strandings\":\""
            << world.pl.n.strand << "\",\"events\":[";
  for (size_t i = 0; i < world.pl.ev.size(); ++i) {
    if (i)
      std::cout << ',';
    const auto &event = world.pl.ev[i];
    std::cout << '[' << event[0] << ',' << event[1] << ",\"" << event[2]
              << "\"]";
  }
  std::cout << "]}";
}

int oracle() {
  Run run{};
  if (!readRun(run))
    return 2;
  // Replay the same bounded recipe through the real sim; timeline expectations
  // are calculated separately in Python from these raw daily snapshots.
  World world = create(run.seed, run.majors, apsis::Rules{});
  while (world.day < run.startDay)
    step(world);
  std::cout << "{\"frames\":[";
  rawFrame(world);
  size_t input = 0;
  for (i64 day = 0; day < run.horizon; ++day) {
    while (input < run.inputs.size() && run.inputs[input].day == world.day)
      submit(world, run.inputs[input++].a);
    tick(world);
    std::cout << ',';
    rawFrame(world);
  }
  std::cout << "],\"finalStateHash\":\"" << apsis::hashBytes(checkpoint(world))
            << "\",\"worldOnlyHash\":\"" << hashState(world) << "\"}\n";
  return 0;
}

int fixtures() {
  // Real generalWarPairs/hostile calls over controlled agent relationships.
  // In particular, all-hostile one-pair worlds do not satisfy pairs >= 2.
  const int ids[] = {0, 1, 4, 5};
  std::cout << "{\"warCases\":[";
  bool comma = false;
  for (int count = 0; count <= 4; ++count) {
    const int pairCount = count * (count - 1) / 2;
    for (int hostileCount = 0; hostileCount <= pairCount; ++hostileCount) {
      World world{};
      for (int i = 0; i < count; ++i) {
        Agent agent{};
        agent.id = ids[i];
        agent.kind = K_BASE;
        world.pw.ag.emplace(agent.id, agent);
      }
      int pair = 0;
      for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
          world.pw.rel[pk(ids[i], ids[j])] =
              pair++ < hostileCount ? world.P.contestRelT : 10000;
      const War observed = warOf(world);
      if (comma)
        std::cout << ',';
      comma = true;
      std::cout << "{\"expectedPairs\":" << pairCount
                << ",\"expectedHostile\":" << hostileCount << ',';
      warFields(std::cout, observed);
      std::cout << '}';
    }
  }
  std::cout << "],\"rescues\":[";
  for (int failed = 0; failed <= 1; ++failed) {
    World world = create(333860, 4, apsis::Rules{});
    submit(world, {A_START, {0}});
    tick(world);
    // Existing pilot-gate fixture: five suit days against ten response days.
    world.CP.lsSuitDays = 5;
    world.pl.at = -1;
    world.pl.fuel = 0;
    world.pl.lsh = failed ? 0 : 1000;
    const i64 before = world.pl.n.strand;
    tick(world);
    if (failed)
      std::cout << ',';
    std::cout << "{\"before\":\"" << before << "\",\"raw\":";
    rawFrame(world);
    std::cout << ",\"exported\":";
    frame(std::cout, world, warOf(world));
    std::cout << '}';
  }
  std::cout << "]}\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "oracle")
    return oracle();
  if (argc == 1)
    return fixtures();
  return 2;
}
