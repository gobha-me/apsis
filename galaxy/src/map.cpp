#include "apsis/galaxy/map.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace apsis::galaxy {
namespace {

constexpr std::uint32_t galaxy_domain = 0x47414c58U; // GALX
constexpr std::uint32_t jitter_x = 1;
constexpr std::uint32_t jitter_y = 2;
constexpr std::uint32_t calibration_x = 3;
constexpr std::uint32_t calibration_y = 4;

// V1 freezes these unsigned 32-bit operations, constants and argument order.
std::uint32_t mix32(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x85ebca6bU;
  value ^= value >> 13;
  value *= 0xc2b2ae35U;
  value ^= value >> 16;
  return value;
}

std::uint32_t addressed_draw(std::uint32_t seed_lo, std::uint32_t seed_hi,
                             std::uint32_t domain, std::uint32_t version,
                             std::uint32_t cx_bits, std::uint32_t cy_bits,
                             std::uint32_t slot, std::uint32_t purpose,
                             std::uint32_t counter) {
  const std::array<std::uint32_t, 9> words{seed_lo, seed_hi, domain,
                                           version, cx_bits, cy_bits,
                                           slot,    purpose, counter};
  std::uint32_t hash = 0x6d2b79f5U;
  for (const auto word : words) {
    hash = mix32(hash ^ mix32(word + 0x9e3779b9U));
  }
  return hash;
}

// Restoring square root: all intermediates fit uint64_t, including UINT64_MAX.
std::uint64_t nearest_sqrt(std::uint64_t value) {
  const auto original = value;
  std::uint64_t root = 0;
  std::uint64_t bit = std::uint64_t{1} << 62;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  // sqrt(n) >= root + 1/2 iff n-root^2 > root for integral n.
  return root + (original - root * root > root ? 1 : 0);
}

std::uint64_t squared_distance(Point a, Point b) {
  const auto dx = a.x >= b.x ? static_cast<std::uint64_t>(a.x - b.x)
                             : static_cast<std::uint64_t>(b.x - a.x);
  const auto dy = a.y >= b.y ? static_cast<std::uint64_t>(a.y - b.y)
                             : static_cast<std::uint64_t>(b.y - a.y);
  // Radius checks use axis-bounded candidates and the origin (<= 2*Rout^2).
  // Other calls use in-disc endpoints (chord squared <= 4*Rout^2).
  // Rout <= INT32_MAX keeps both in uint64, with differences fitting int64.
  return dx * dx + dy * dy;
}

std::optional<std::int64_t> to_milli_j(std::uint64_t distance,
                                       std::int64_t raw_units_per_j) {
  if (raw_units_per_j <= 0 ||
      distance > std::numeric_limits<std::uint64_t>::max() / 1000) {
    return std::nullopt;
  }
  const auto numerator = distance * 1000;
  const auto denominator = static_cast<std::uint64_t>(raw_units_per_j);
  const auto quotient = numerator / denominator;
  const auto remainder = numerator % denominator;
  const auto rounded =
      quotient + (remainder >= denominator / 2 + denominator % 2 ? 1 : 0);
  if (rounded >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(rounded);
}

Status validate(const Config &config, std::uint32_t version) {
  if (version != current_format_version) {
    return Status::unsupported_format_version;
  }
  constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
  if (config.cell_side_raw < 4 || config.cell_side_raw > maximum ||
      config.cell_side_raw % 2 != 0 || config.inner_radius_raw < 0 ||
      config.inner_radius_raw >= config.outer_radius_raw ||
      config.outer_radius_raw > maximum) {
    return Status::invalid_config;
  }
  return Status::ok;
}

// No mutable state or host APIs: this generator can be replaced independently
// of the facade's reveal bookkeeping and can run on native or WASM workers.
class RegionGenerator {
public:
  RegionGenerator(std::uint64_t seed, Config config, std::uint32_t version)
      : seed_(seed), config_(config), version_(version) {}

  std::uint32_t draw(RegionId region, std::uint32_t slot, std::uint32_t purpose,
                     std::uint32_t counter) const {
    return addressed_draw(
        static_cast<std::uint32_t>(seed_),
        static_cast<std::uint32_t>(seed_ >> 32), galaxy_domain, version_,
        static_cast<std::uint32_t>(region.cx),
        static_cast<std::uint32_t>(region.cy), slot, purpose, counter);
  }

  std::vector<System> generate(RegionId region) const {
    std::vector<System> result;
    const auto half = config_.cell_side_raw / 2;
    // Promoting before multiplication preserves the floor-based half-open
    // cells for negative coordinates and avoids int32 overflow at extrema.
    const Point base{
        static_cast<std::int64_t>(region.cx) * config_.cell_side_raw,
        static_cast<std::int64_t>(region.cy) * config_.cell_side_raw};
    const auto rin = static_cast<std::uint64_t>(config_.inner_radius_raw);
    const auto rout = static_cast<std::uint64_t>(config_.outer_radius_raw);
    for (std::uint32_t slot = 0; slot < 4; ++slot) {
      const auto qx = static_cast<std::int64_t>(slot % 2);
      const auto qy = static_cast<std::int64_t>(slot / 2);
      const Point point{base.x + qx * half + 1 +
                            draw(region, slot, jitter_x, 0) % (half - 1),
                        base.y + qy * half + 1 +
                            draw(region, slot, jitter_y, 0) % (half - 1)};
      // Very distant requested cells can have huge coordinates; reject them
      // before squaring. Accepted coordinates fit the supported disc bounds.
      if (point.x <= -config_.outer_radius_raw ||
          point.x >= config_.outer_radius_raw ||
          point.y <= -config_.outer_radius_raw ||
          point.y >= config_.outer_radius_raw) {
        continue;
      }
      const auto radius_squared = squared_distance(point, {});
      if (radius_squared < rin * rin || radius_squared >= rout * rout) {
        continue;
      }
      result.push_back(
          {{region, slot},
           point,
           {static_cast<std::int64_t>(nearest_sqrt(radius_squared)), point}});
    }
    return result;
  }

private:
  const std::uint64_t seed_;
  const Config config_;
  const std::uint32_t version_;
};

template <typename Function> void for_halo(RegionId region, Function function) {
  for (std::int64_t dy = -1; dy <= 1; ++dy) {
    for (std::int64_t dx = -1; dx <= 1; ++dx) {
      const auto cx = static_cast<std::int64_t>(region.cx) + dx;
      const auto cy = static_cast<std::int64_t>(region.cy) + dy;
      if (cx < std::numeric_limits<std::int32_t>::min() ||
          cx > std::numeric_limits<std::int32_t>::max() ||
          cy < std::numeric_limits<std::int32_t>::min() ||
          cy > std::numeric_limits<std::int32_t>::max()) {
        continue;
      }
      function(RegionId{static_cast<std::int32_t>(cx),
                        static_cast<std::int32_t>(cy)});
    }
  }
}

using LaneId = std::pair<SystemId, SystemId>;

} // namespace

struct Map::Impl {
  Impl(std::uint64_t seed, Config config, std::uint32_t version)
      : seed(seed), config(config), version(version),
        configuration_status(validate(config, version)),
        generator(seed, config, version) {}

  const std::vector<System> &region_systems(RegionId region) {
    const auto found = region_cache.find(region);
    if (found != region_cache.end()) {
      return found->second;
    }
    return region_cache.emplace(region, generator.generate(region))
        .first->second;
  }

  std::optional<Lane> lane_between(const System &a, const System &b) const {
    if (a.id == b.id) {
      return std::nullopt;
    }
    const auto squared = squared_distance(a.position, b.position);
    const auto side = static_cast<std::uint64_t>(config.cell_side_raw);
    if (squared > side * side) {
      return std::nullopt;
    }
    return Lane{std::min(a.id, b.id), std::max(a.id, b.id),
                static_cast<std::int64_t>(nearest_sqrt(squared))};
  }

  std::optional<std::int64_t> calibrate() {
    const auto extent = (config.outer_radius_raw + config.cell_side_raw - 1) /
                        config.cell_side_raw;
    const auto width = static_cast<std::uint32_t>(2 * extent);
    std::set<RegionId> sampled_cells;
    for (std::uint32_t counter = 0; counter < 64; ++counter) {
      const auto cx =
          static_cast<std::int64_t>(
              generator.draw({}, 0, calibration_x, counter) % width) -
          extent;
      const auto cy =
          static_cast<std::int64_t>(
              generator.draw({}, 0, calibration_y, counter) % width) -
          extent;
      sampled_cells.insert(
          {static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy)});
    }
    std::map<LaneId, std::int64_t> sampled_lanes;
    for (const auto cell : sampled_cells) {
      for (const auto &owner : region_systems(cell)) {
        for_halo(cell, [&](RegionId neighbor) {
          for (const auto &other : region_systems(neighbor)) {
            // The smaller endpoint owns the lane, including cross-cell lanes.
            if (owner.id < other.id) {
              if (const auto lane = lane_between(owner, other)) {
                sampled_lanes.emplace(LaneId{lane->a, lane->b},
                                      lane->raw_length);
              }
            }
          }
        });
      }
    }
    if (sampled_lanes.empty()) {
      return std::nullopt;
    }
    std::vector<std::int64_t> lengths;
    lengths.reserve(sampled_lanes.size());
    for (const auto &[id, length] : sampled_lanes) {
      lengths.push_back(length);
    }
    std::sort(lengths.begin(), lengths.end());
    return lengths[lengths.size() / 2];
  }

  Status view_region(RegionId region) {
    if (configuration_status != Status::ok) {
      return configuration_status;
    }
    if (viewed_cells.contains(region)) {
      return Status::ok;
    }
    if (!raw_j) {
      const auto calibration = calibrate();
      if (!calibration || *calibration <= 0) {
        return Status::calibration_failed;
      }
      raw_j = calibration;
    }

    const auto &new_systems = region_systems(region);
    for (const auto &system : new_systems) {
      visible_systems.emplace(system.id, system);
    }
    for (const auto &system : new_systems) {
      for_halo(region, [&](RegionId neighbor) {
        for (const auto &other : region_systems(neighbor)) {
          if (visible_systems.contains(other.id)) {
            if (const auto lane = lane_between(system, other)) {
              // Either endpoint may have been revealed first. Canonical IDs
              // refresh boundary lanes even when their owner was viewed
              // earlier.
              visible_lanes.emplace(LaneId{lane->a, lane->b}, *lane);
            }
          }
        }
      });
    }
    viewed_cells.insert(region);
    systems_snapshot.clear();
    systems_snapshot.reserve(visible_systems.size());
    for (const auto &[id, system] : visible_systems) {
      systems_snapshot.push_back(system);
    }
    lanes_snapshot.clear();
    lanes_snapshot.reserve(visible_lanes.size());
    for (const auto &[id, lane] : visible_lanes) {
      lanes_snapshot.push_back(lane);
    }
    return Status::ok;
  }

  const std::uint64_t seed;
  const Config config;
  const std::uint32_t version;
  const Status configuration_status;
  const RegionGenerator generator;
  std::map<RegionId, std::vector<System>> region_cache;
  std::set<RegionId> viewed_cells;
  std::map<SystemId, System> visible_systems;
  std::map<LaneId, Lane> visible_lanes;
  std::vector<System> systems_snapshot;
  std::vector<Lane> lanes_snapshot;
  std::optional<std::int64_t> raw_j;
};

std::string_view status_name(Status status) {
  switch (status) {
  case Status::ok:
    return "ok";
  case Status::invalid_config:
    return "invalid_config";
  case Status::unsupported_format_version:
    return "unsupported_format_version";
  case Status::calibration_failed:
    return "calibration_failed";
  }
  return "unknown_status";
}

Map::Map(std::uint64_t universe_seed, Config config,
         std::uint32_t format_version)
    : impl_(std::make_unique<Impl>(universe_seed, config, format_version)) {}

Map::~Map() = default;
Map::Map(Map &&) noexcept = default;
Map &Map::operator=(Map &&) noexcept = default;

void Map::require_impl() const {
  if (!impl_) {
    throw std::logic_error("operation on a moved-from Map");
  }
}

Status Map::view_region(RegionId region) {
  require_impl();
  return impl_->view_region(region);
}

std::span<const System> Map::revealed_systems() const {
  require_impl();
  return impl_->systems_snapshot;
}

std::span<const Lane> Map::revealed_lanes() const {
  require_impl();
  return impl_->lanes_snapshot;
}

std::optional<std::int64_t> Map::distance_milli_j(SystemId a,
                                                  SystemId b) const {
  require_impl();
  const auto first = impl_->visible_systems.find(a);
  const auto second = impl_->visible_systems.find(b);
  if (!impl_->raw_j || first == impl_->visible_systems.end() ||
      second == impl_->visible_systems.end()) {
    return std::nullopt;
  }
  return to_milli_j(nearest_sqrt(squared_distance(first->second.position,
                                                  second->second.position)),
                    *impl_->raw_j);
}

std::optional<std::int64_t> Map::raw_units_per_j() const {
  require_impl();
  return impl_->raw_j;
}

const Config &Map::config() const {
  require_impl();
  return impl_->config;
}
std::uint64_t Map::universe_seed() const {
  require_impl();
  return impl_->seed;
}
std::uint32_t Map::format_version() const {
  require_impl();
  return impl_->version;
}

} // namespace apsis::galaxy
