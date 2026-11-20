#include "test_support.hpp"
#include "v1_oracle.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace apsis::galaxy;
using galaxy_test::require;
using galaxy_test::snapshot;
using galaxy_test::v1::rounded_root;
using LaneId = std::pair<SystemId, SystemId>;

constexpr auto min_cell = std::numeric_limits<std::int32_t>::min();
constexpr auto max_cell = std::numeric_limits<std::int32_t>::max();
constexpr std::array<std::uint64_t, 6> seeds{
    0,
    1,
    42,
    0x100000000ULL,
    0x123456789abcdef0ULL,
    std::numeric_limits<std::uint64_t>::max()};

const std::vector<RegionId> regions{{7, 0},
                                    {4, 0},
                                    {5, 0},
                                    {4, 1},
                                    {5, 1},
                                    {-5, -1},
                                    {-4, -1},
                                    {-5, 0},
                                    {-4, 0},
                                    {1, 0},
                                    {2, 0},
                                    {31, 0},
                                    {31, 1},
                                    {0, 31},
                                    {0, -32},
                                    {-32, 0},
                                    {-23, 22},
                                    {0, 0},
                                    {50, 50},
                                    {min_cell, min_cell},
                                    {max_cell, max_cell}};

std::uint64_t squared(Point a, Point b) {
  const auto dx =
      static_cast<std::uint64_t>(a.x >= b.x ? a.x - b.x : b.x - a.x);
  const auto dy =
      static_cast<std::uint64_t>(a.y >= b.y ? a.y - b.y : b.y - a.y);
  return dx * dx + dy * dy;
}

void geometry(const Map &map, const std::set<RegionId> &viewed) {
  const auto &config = map.config();
  const auto systems = map.revealed_systems();
  galaxy_test::v1::Positions expected_systems;
  for (const auto cell : viewed) {
    expected_systems.merge(
        galaxy_test::v1::positions(map.universe_seed(), config, cell));
  }
  galaxy_test::v1::Positions actual_systems;
  for (const auto &system : systems) {
    actual_systems.emplace(system.id, system.position);
  }
  require(actual_systems == expected_systems,
          "viewed cells differ from exact format-v1 in-annulus candidates");
  std::map<LaneId, std::int64_t> expected;
  for (std::size_t i = 0; i < systems.size(); ++i) {
    const auto &a = systems[i];
    require(viewed.contains(a.id.region), "private cell leaked into systems");
    require(a.id.slot < 4, "invalid slot");
    require(i == 0 || systems[i - 1].id < a.id,
            "systems not sorted and unique");
    const auto base_x = std::int64_t{a.id.region.cx} * config.cell_side_raw;
    const auto base_y = std::int64_t{a.id.region.cy} * config.cell_side_raw;
    const auto half = config.cell_side_raw / 2;
    const auto quadrant_x = base_x + (a.id.slot % 2) * half;
    const auto quadrant_y = base_y + (a.id.slot / 2) * half;
    require(a.position.x > quadrant_x && a.position.x < quadrant_x + half &&
                a.position.y > quadrant_y && a.position.y < quadrant_y + half,
            "position outside its fixed slot quadrant");
    const auto radius2 = squared(a.position, {});
    const auto inner = static_cast<std::uint64_t>(config.inner_radius_raw);
    const auto outer = static_cast<std::uint64_t>(config.outer_radius_raw);
    require(radius2 >= inner * inner && radius2 < outer * outer,
            "system outside annulus");
    require(a.orbit.epoch_vector == a.position &&
                a.orbit.radius_raw == rounded_root(radius2),
            "incorrect orbit geometry");
    require(map.distance_milli_j(a.id, a.id) == 0, "self distance is not zero");
    require(!map.distance_milli_j(a.id, {a.id.region, 4}),
            "nonexistent system has distance");
    for (std::size_t j = i + 1; j < systems.size(); ++j) {
      const auto &b = systems[j];
      const auto distance2 = squared(a.position, b.position);
      const auto raw = rounded_root(distance2);
      require(map.raw_units_per_j().has_value(), "visible map has no J");
      const auto scale = *map.raw_units_per_j();
      require(scale > 0, "nonpositive J");
      const auto numerator = raw * 1000;
      const auto milli =
          numerator / scale + (2 * (numerator % scale) >= scale ? 1 : 0);
      require(map.distance_milli_j(a.id, b.id) == milli &&
                  map.distance_milli_j(b.id, a.id) == milli,
              "incorrect or asymmetric chord distance");
      const auto side = static_cast<std::uint64_t>(config.cell_side_raw);
      if (distance2 <= side * side) {
        expected.emplace(LaneId{a.id, b.id}, raw);
      }
    }
  }
  std::map<LaneId, std::int64_t> actual;
  std::optional<LaneId> previous;
  for (const auto &lane : map.revealed_lanes()) {
    const LaneId id{lane.a, lane.b};
    require(lane.a < lane.b && (!previous || *previous < id),
            "lanes not canonical, sorted and unique");
    require(viewed.contains(lane.a.region) && viewed.contains(lane.b.region),
            "private endpoint leaked into lanes");
    require(map.distance_milli_j(lane.a, lane.b).has_value(),
            "lane endpoint is hidden");
    actual.emplace(id, lane.raw_length);
    previous = id;
  }
  require(actual == expected, "visible lanes differ from all eligible pairs");
}

std::string reveal(std::uint64_t seed, Config config,
                   const std::vector<RegionId> &order,
                   bool check_each = false) {
  Map map(seed, config);
  require(map.revealed_systems().empty() && map.revealed_lanes().empty() &&
              !map.raw_units_per_j(),
          "construction revealed or calibrated the map");
  require(!map.distance_milli_j({{4, 0}, 0}, {{4, 0}, 1}),
          "construction exposes distances");
  std::optional<std::int64_t> frozen;
  std::set<RegionId> viewed;
  for (const auto cell : order) {
    require(map.view_region(cell) == Status::ok, "valid view failed");
    if (!frozen) {
      frozen = map.raw_units_per_j();
      require(frozen && *frozen > 0, "first successful view did not freeze J");
    }
    require(map.raw_units_per_j() == frozen, "J changed after a view");
    viewed.insert(cell);
    if (check_each) {
      geometry(map, viewed);
      const auto before = snapshot(map);
      require(map.view_region(cell) == Status::ok, "repeat view failed");
      require(snapshot(map) == before, "repeat view changed map bytes");
    }
  }
  geometry(map, viewed);
  return snapshot(map);
}

void format_v1_known_answers() {
  struct Fixture {
    std::uint64_t seed;
    Config config;
    std::int64_t raw_j;
  };
  // Computed from the README pseudocode with an independent Python integer
  // translation, before cross-checking against Map. The high-word seed has an
  // even lane sample whose two central lengths differ (46644, 46645); seed 0
  // changes J if counter 63 is omitted (47124 instead of 47079).
  const std::array<Fixture, 3> fixtures{{
      {0, {}, 47079},
      {0x100000000ULL, {}, 46645},
      {42, {128, 256, 4096}, 93},
  }};
  for (const auto &fixture : fixtures) {
    const auto expected_j =
        galaxy_test::v1::calibration(fixture.seed, fixture.config);
    require(expected_j == fixture.raw_j,
            "test oracle differs from independently derived v1 J answer");
    Map map(fixture.seed, fixture.config, 1);
    std::set<RegionId> viewed;
    for (const auto cell :
         std::array<RegionId, 4>{RegionId{4, 0}, {-5, -1}, {7, 0}, {31, 1}}) {
      require(map.view_region(cell) == Status::ok, "v1 fixture view failed");
      viewed.insert(cell);
      geometry(map, viewed); // Exact IDs and x/y positions, including clipping.
      require(map.raw_units_per_j() == expected_j,
              "raw_units_per_j differs from independent format-v1 oracle");
    }
  }
}

void boundary_and_ties() {
  // With S=10 and seed 1, slot 0 is exactly (4,3): r² == Rin² == 25.
  const Config annulus{10, 5, 40};
  const SystemId on_inner{{0, 0}, 0};
  const auto annulus_positions = galaxy_test::v1::positions(1, annulus, {0, 0});
  require(annulus_positions.at(on_inner) == Point{4, 3} &&
              squared(annulus_positions.at(on_inner), {}) == 25,
          "inner-boundary fixture no longer lies exactly on Rin");
  Map boundary(1, annulus);
  require(boundary.view_region({0, 0}) == Status::ok,
          "inner-boundary view failed");
  geometry(boundary, {{0, 0}});
  require(boundary.distance_milli_j(on_inner, on_inner) == 0,
          "inclusive inner-boundary system was omitted");

  // These two same-cell positions differ by 15 ticks. Calibration gives J=16,
  // so 1000*15/16 = 937.5 exactly; the required ties-up answer is 938.
  const Config tie_config{22, 0, 88};
  const SystemId a{{-2, -2}, 0};
  const SystemId b{{-2, -2}, 1};
  const auto tie_positions =
      galaxy_test::v1::positions(0, tie_config, {-2, -2});
  require(tie_positions.at(a) == Point{-43, -37} &&
              tie_positions.at(b) == Point{-28, -37} &&
              galaxy_test::v1::calibration(0, tie_config) == 16,
          "milliJ fixture is not an exact half tie");
  const auto raw =
      rounded_root(squared(tie_positions.at(a), tie_positions.at(b)));
  require(raw == 15 && 2 * ((1000 * raw) % 16) == 16,
          "milliJ fixture does not exercise tie rounding");
  Map tie(0, tie_config);
  require(tie.view_region({-2, -2}) == Status::ok, "tie fixture view failed");
  require(tie.raw_units_per_j() == 16 && tie.distance_milli_j(a, b) == 938 &&
              tie.distance_milli_j(b, a) == 938,
          "exact milliJ tie did not round upward");
  geometry(tie, {{-2, -2}});
}

void rounded_outer_boundary() {
  const Config config{128, 256, 4096};
  const SystemId boundary_id{{-32, -1}, 0};
  Map map(0, config);
  require(map.view_region(boundary_id.region) == Status::ok,
          "outer-boundary fixture view failed");
  const auto systems = map.revealed_systems();
  const auto fixture = std::find_if(
      systems.begin(), systems.end(),
      [boundary_id](const System &system) { return system.id == boundary_id; });
  require(fixture != systems.end(), "outer-boundary fixture was omitted");
  require(fixture->position == Point{-4095, -68},
          "outer-boundary fixture position changed");
  const auto radius2 = squared(fixture->position, {});
  const auto outer = static_cast<std::uint64_t>(config.outer_radius_raw);
  require(radius2 < outer * outer && rounded_root(radius2) == 4096,
          "outer-boundary fixture does not round upward to Rout");
  require(fixture->orbit.radius_raw == 4096 &&
              fixture->orbit.radius_raw == config.outer_radius_raw,
          "outer-boundary orbital radius must round to Rout without a clamp");
  geometry(map, {boundary_id.region});
}

template <typename Operation> void moved_from_throws(Operation operation) {
  auto check = [&operation](Map &source) {
    bool threw = false;
    try {
      operation(source);
    } catch (const std::logic_error &) {
      threw = true;
    }
    require(threw, "moved-from operation did not throw std::logic_error");
  };

  Map source(42, Config{128, 256, 4096});
  Map destination(std::move(source));
  check(source);
  source = std::move(destination);
  check(destination);
}

void moves_preserve_and_restore_state() {
  const Config config{128, 256, 4096};
  constexpr std::uint64_t seed = 42;
  Map source(seed, config);
  require(source.view_region({4, 0}) == Status::ok, "move fixture view failed");
  require(!source.revealed_systems().empty(),
          "move fixture has no visible systems");
  const auto before = snapshot(source);

  Map destination(std::move(source));
  require(snapshot(destination) == before,
          "move construction changed the public map state");
  require(destination.view_region({5, 0}) == Status::ok &&
              snapshot(destination) == reveal(seed, config, {{4, 0}, {5, 0}}),
          "move-constructed Map cannot reveal another cell");
  const auto extended = snapshot(destination);

  source = std::move(destination);
  require(snapshot(source) == extended,
          "move assignment did not restore the moved-from Map state");
  require(source.view_region({4, 1}) == Status::ok &&
              snapshot(source) ==
                  reveal(seed, config, {{4, 0}, {5, 0}, {4, 1}}),
          "restored Map cannot reveal another cell");

  Map live_target(7, Config{4, 0, 4});
  require(live_target.view_region({0, 0}) == Status::ok,
          "live move-assignment target view failed");
  const auto restored = snapshot(source);
  live_target = std::move(source);
  require(snapshot(live_target) == restored,
          "move assignment into a live Map changed the transferred state");
}

void generation_order() {
  const std::array<Config, 3> configs{Config{}, Config{128, 256, 4096},
                                      Config{2147483646, 0, 2147483647}};
  for (const auto config : configs) {
    for (const auto seed : seeds) {
      const auto reference = reveal(seed, config, regions, true);
      auto order = regions;
      std::reverse(order.begin(), order.end());
      require(reveal(seed, config, order, true) == reference,
              "reversed generation differs for seed " + std::to_string(seed));
      std::mt19937 random(static_cast<std::uint32_t>(seed));
      std::shuffle(order.begin(), order.end(), random);
      // Repeated requests interleaved with fresh cells exercise cache hits.
      order.insert(order.end(), regions.begin(), regions.end());
      require(reveal(seed, config, order, true) == reference,
              "shuffled generation differs for seed " + std::to_string(seed));
    }
  }
  const auto low_seed = reveal(0, {}, regions);
  const auto high_seed = reveal(0x100000000ULL, {}, regions);
  require(low_seed.substr(low_seed.find('\n')) !=
              high_seed.substr(high_seed.find('\n')),
          "seed high word does not affect public output");
}

void thread_counts() {
  // Every job has its own Map and views the complete set. No shared Map is
  // mutated concurrently, and no boundary lanes are lost to per-cell merging.
  constexpr std::size_t jobs = 24;
  std::vector<std::string> serial(jobs);
  for (std::size_t i = 0; i < jobs; ++i) {
    serial[i] = reveal(seeds[i % seeds.size()], {}, regions);
  }
  for (const auto workers : {1, 2, 8}) {
    std::vector<std::string> results(jobs);
    std::vector<std::exception_ptr> failures(jobs);
    std::atomic<std::size_t> next{0};
    std::barrier start(workers);
    std::vector<std::jthread> threads;
    for (int worker = 0; worker < workers; ++worker) {
      threads.emplace_back([&] {
        start.arrive_and_wait();
        for (;;) {
          const auto index = next.fetch_add(1);
          if (index >= jobs) {
            return;
          }
          try {
            auto order = regions;
            std::mt19937 random(static_cast<std::uint32_t>(index + 100));
            std::shuffle(order.begin(), order.end(), random);
            results[index] = reveal(seeds[index % seeds.size()], {}, order);
          } catch (...) {
            failures[index] = std::current_exception();
          }
        }
      });
    }
    threads.clear(); // Join before inspecting per-job results.
    for (const auto &failure : failures) {
      if (failure) {
        std::rethrow_exception(failure);
      }
    }
    require(results == serial, "thread-count invariance failed at " +
                                   std::to_string(workers) + " workers");
  }
}

void fog_cache_and_j() {
  for (const auto seed : seeds) {
    Map map(seed);
    require(map.view_region({4, 0}) == Status::ok, "first cell failed");
    geometry(map, {{4, 0}});
    const auto frozen = map.raw_units_per_j();
    const auto first = snapshot(map);
    const SystemId hidden{{5, 0}, 0};
    const auto visible = map.revealed_systems().front().id;
    require(!map.distance_milli_j(visible, hidden) &&
                !map.distance_milli_j(hidden, visible),
            "halo system distance leaked");
    require(map.view_region({4, 0}) == Status::ok && snapshot(map) == first,
            "immediate repeat changed map");
    require(map.view_region({5, 0}) == Status::ok, "halo cell view failed");
    geometry(map, {{4, 0}, {5, 0}});
    const auto lanes = map.revealed_lanes();
    require(std::any_of(lanes.begin(), lanes.end(),
                        [](const Lane &lane) {
                          return lane.a.region != lane.b.region;
                        }),
            "test did not exercise visible boundary lanes");
    require(snapshot(map) == reveal(seed, {}, {{5, 0}, {4, 0}}),
            "privately generated halo differs when explicitly viewed");
    const auto before_empty = snapshot(map);
    for (const auto empty : std::array<RegionId, 7>{RegionId{0, 0},
                                                    {-1, 0},
                                                    {50, -50},
                                                    {min_cell, min_cell},
                                                    {min_cell, max_cell},
                                                    {max_cell, min_cell},
                                                    {max_cell, max_cell}}) {
      require(map.view_region(empty) == Status::ok,
              "empty/extreme view failed");
      require(snapshot(map) == before_empty && map.raw_units_per_j() == frozen,
              "empty/extreme view changed revelations or frozen J");
    }

    // S=4 has only the offsets 1 and 3. In the radius-4 disc all four
    // bounding cells contain three systems; any calibration cell's halo
    // generates all four privately. Start by explicitly viewing nothing there.
    const Config small{4, 0, 4};
    Map calibrated(seed, small);
    require(calibrated.view_region({max_cell, min_cell}) == Status::ok,
            "empty first view did not calibrate");
    require(calibrated.revealed_systems().empty() &&
                calibrated.revealed_lanes().empty() &&
                calibrated.raw_units_per_j().has_value(),
            "calibration leaked systems/lanes or did not freeze J");
    const auto small_j = calibrated.raw_units_per_j();
    std::vector<RegionId> disc{{-1, -1}, {-1, 0}, {0, -1}, {0, 0}};
    const auto fresh = reveal(seed, small, disc, true);
    std::reverse(disc.begin(), disc.end());
    std::set<RegionId> viewed;
    for (const auto cell : disc) {
      require(calibrated.view_region(cell) == Status::ok,
              "calibration-cached cell failed");
      viewed.insert(cell);
      geometry(calibrated, viewed);
      require(calibrated.raw_units_per_j() == small_j,
              "viewing calibration cells changed J");
      const auto before = snapshot(calibrated);
      require(calibrated.view_region(cell) == Status::ok &&
                  snapshot(calibrated) == before,
              "calibration-cached repeat changed map");
    }
    require(calibrated.revealed_systems().size() == 12 &&
                snapshot(calibrated) == fresh,
            "private calibration cache changed public map bytes");
  }
}

void failures_are_atomic() {
  constexpr auto huge = std::numeric_limits<std::int64_t>::max();
  const std::array<Config, 12> invalid{Config{0, 0, 8},
                                       {3, 0, 8},
                                       {5, 0, 8},
                                       {huge, 0, 8},
                                       {2147483648LL, 0, 8},
                                       {4, -1, 8},
                                       {4, 8, 8},
                                       {4, 9, 8},
                                       {4, 0, 0},
                                       {4, 0, -1},
                                       {4, 0, 2147483648LL},
                                       {4, 0, huge}};
  auto check = [](Map &map, Status expected) {
    const auto before = snapshot(map);
    for (const auto cell : std::array<RegionId, 3>{
             RegionId{4, 0}, {0, 0}, {min_cell, max_cell}}) {
      for (int attempt = 0; attempt < 2; ++attempt) {
        require(map.view_region(cell) == expected, "wrong failure status");
        require(snapshot(map) == before && !map.raw_units_per_j() &&
                    map.revealed_systems().empty() &&
                    map.revealed_lanes().empty(),
                "failed view changed map state");
      }
    }
  };
  for (const auto config : invalid) {
    Map map(42, config);
    check(map, Status::invalid_config);
  }
  for (const auto version : {0U, current_format_version + 1,
                             std::numeric_limits<std::uint32_t>::max()}) {
    Map map(42, {}, version);
    check(map, Status::unsupported_format_version);
  }
  // Valid bounds, but no integer candidate fits inside radius 1.
  Map no_calibration(42, Config{4, 0, 1});
  check(no_calibration, Status::calibration_failed);
}
} // namespace

int main() {
  const std::array<std::pair<const char *, void (*)()>, 16> tests{{
      {"independent format-v1 positions and calibration",
       format_v1_known_answers},
      {"inclusive inner boundary and exact milliJ ties", boundary_and_ties},
      {"unclamped rounding at the outer boundary", rounded_outer_boundary},
      {"moved-from view_region throws logic_error",
       [] { moved_from_throws([](Map &map) { map.view_region({4, 0}); }); }},
      {"moved-from revealed_systems throws logic_error",
       [] { moved_from_throws([](Map &map) { map.revealed_systems(); }); }},
      {"moved-from revealed_lanes throws logic_error",
       [] { moved_from_throws([](Map &map) { map.revealed_lanes(); }); }},
      {"moved-from distance_milli_j throws logic_error",
       [] {
         moved_from_throws(
             [](Map &map) { map.distance_milli_j({{4, 0}, 0}, {{4, 0}, 1}); });
       }},
      {"moved-from raw_units_per_j throws logic_error",
       [] { moved_from_throws([](Map &map) { map.raw_units_per_j(); }); }},
      {"moved-from config throws logic_error",
       [] { moved_from_throws([](Map &map) { map.config(); }); }},
      {"moved-from universe_seed throws logic_error",
       [] { moved_from_throws([](Map &map) { map.universe_seed(); }); }},
      {"moved-from format_version throws logic_error",
       [] { moved_from_throws([](Map &map) { map.format_version(); }); }},
      {"moves preserve state and restore normal use",
       moves_preserve_and_restore_state},
      {"generation order, config and repeat invariance", generation_order},
      {"independent Maps at 1/2/8 threads", thread_counts},
      {"fog, boundary refresh, private caches and frozen J", fog_cache_and_j},
      {"invalid config/version and calibration failure", failures_are_atomic},
  }};
  for (const auto &[name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
      return 1;
    }
  }
}
