#pragma once

#include <apsis/galaxy/map.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

// Test-side translation of README.md "Format 1 addressed randomness" and
// "Geometry and scale". This code never constructs or queries a Map, nor calls
// production generation, calibration, hashing or rounding helpers. Keep version
// 1 and its constants literal: changing the public format must not silently
// change this specification oracle.
namespace galaxy_test::v1 {
using namespace apsis::galaxy;
using Positions = std::map<SystemId, Point>;

inline std::uint32_t mix(std::uint32_t v) {
  v ^= v >> 16;
  v *= 0x85ebca6bU;
  v ^= v >> 13;
  v *= 0xc2b2ae35U;
  return v ^ (v >> 16);
}

inline std::uint32_t draw(std::uint64_t seed, RegionId cell, std::uint32_t slot,
                          std::uint32_t purpose, std::uint32_t counter = 0) {
  const std::array<std::uint32_t, 9> words{
      static_cast<std::uint32_t>(seed),
      static_cast<std::uint32_t>(seed >> 32),
      0x47414c58U,
      1,
      static_cast<std::uint32_t>(cell.cx),
      static_cast<std::uint32_t>(cell.cy),
      slot,
      purpose,
      counter};
  std::uint32_t h = 0x6d2b79f5U;
  for (const auto w : words) {
    h = mix(h ^ mix(w + 0x9e3779b9U));
  }
  return h;
}

inline std::uint64_t distance_squared(Point a, Point b) {
  const auto dx =
      static_cast<std::uint64_t>(a.x >= b.x ? a.x - b.x : b.x - a.x);
  const auto dy =
      static_cast<std::uint64_t>(a.y >= b.y ? a.y - b.y : b.y - a.y);
  return dx * dx + dy * dy;
}

// Binary search, independent of the production restoring square root.
inline std::int64_t rounded_root(std::uint64_t value) {
  std::uint64_t lo = 0;
  std::uint64_t hi = std::uint64_t{1} << 32;
  while (lo + 1 < hi) {
    const auto mid = lo + (hi - lo) / 2;
    if (mid * mid <= value) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return static_cast<std::int64_t>(lo + (value > lo * lo + lo ? 1 : 0));
}

inline Positions positions(std::uint64_t seed, Config config, RegionId cell) {
  Positions result;
  const auto half = config.cell_side_raw / 2;
  for (std::uint32_t slot = 0; slot != 4; ++slot) {
    const Point p{
        std::int64_t{cell.cx} * config.cell_side_raw + (slot % 2) * half + 1 +
            draw(seed, cell, slot, 1) % (half - 1),
        std::int64_t{cell.cy} * config.cell_side_raw + (slot / 2) * half + 1 +
            draw(seed, cell, slot, 2) % (half - 1)};
    // Bound the axes before squaring even for INT32_MIN/MAX requested cells.
    if (p.x <= -config.outer_radius_raw || p.x >= config.outer_radius_raw ||
        p.y <= -config.outer_radius_raw || p.y >= config.outer_radius_raw) {
      continue;
    }
    const auto radius2 = distance_squared(p, {});
    const auto rin = static_cast<std::uint64_t>(config.inner_radius_raw);
    const auto rout = static_cast<std::uint64_t>(config.outer_radius_raw);
    if (rin * rin <= radius2 && radius2 < rout * rout) {
      result.emplace(SystemId{cell, slot}, p);
    }
  }
  return result;
}

inline std::optional<std::int64_t> calibration(std::uint64_t seed,
                                               Config config) {
  const auto extent = (config.outer_radius_raw + config.cell_side_raw - 1) /
                      config.cell_side_raw;
  const auto width = static_cast<std::uint32_t>(2 * extent);
  std::set<RegionId> sampled;
  for (std::uint32_t counter = 0; counter != 64; ++counter) {
    sampled.insert({static_cast<std::int32_t>(
                        draw(seed, {}, 0, 3, counter) % width - extent),
                    static_cast<std::int32_t>(
                        draw(seed, {}, 0, 4, counter) % width - extent)});
  }

  // Build the union once, then enumerate each unordered pair once. This differs
  // from the production per-owner/per-halo lane-cache traversal. Ownership is
  // decided solely by ordered IDs; empty cells remain in the sampled set.
  std::set<RegionId> halo;
  for (const auto cell : sampled) {
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        halo.insert({cell.cx + dx, cell.cy + dy});
      }
    }
  }
  Positions all;
  for (const auto cell : halo) {
    all.merge(positions(seed, config, cell));
  }
  std::vector<std::int64_t> lengths;
  const auto side = static_cast<std::uint64_t>(config.cell_side_raw);
  for (auto a = all.begin(); a != all.end(); ++a) {
    if (!sampled.contains(a->first.region)) {
      continue;
    }
    auto b = a;
    for (++b; b != all.end(); ++b) {
      const auto distance2 = distance_squared(a->second, b->second);
      if (distance2 <= side * side) {
        lengths.push_back(rounded_root(distance2));
      }
    }
  }
  if (lengths.empty()) {
    return std::nullopt;
  }
  std::sort(lengths.begin(), lengths.end());
  return lengths[lengths.size() / 2]; // The documented upper median.
}
} // namespace galaxy_test::v1
