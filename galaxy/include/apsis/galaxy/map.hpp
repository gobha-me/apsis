#pragma once

#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace apsis::galaxy {

inline constexpr std::uint32_t current_format_version = 1;
inline constexpr std::int64_t raw_ticks_per_map_unit = 1000;

struct RegionId {
  std::int32_t cx{};
  std::int32_t cy{};
  auto operator<=>(const RegionId &) const = default;
};

struct SystemId {
  RegionId region;
  std::uint32_t slot{};
  auto operator<=>(const SystemId &) const = default;
};

struct Point {
  std::int64_t x{};
  std::int64_t y{};
  auto operator<=>(const Point &) const = default;
};

struct Orbit {
  std::int64_t radius_raw{};
  Point epoch_vector;
};

struct System {
  SystemId id;
  Point position;
  Orbit orbit;
};

// The ordered pair (a,b), where a < b, is the lane ID and a owns it.
struct Lane {
  SystemId a;
  SystemId b;
  std::int64_t raw_length{};
};

struct Config {
  // Supported: even 4 <= S <= INT32_MAX, 0 <= Rin < Rout <= INT32_MAX.
  std::int64_t cell_side_raw = 64000;
  std::int64_t inner_radius_raw = 128000;
  std::int64_t outer_radius_raw = 2048000;
};

enum class Status {
  ok,
  invalid_config,
  unsupported_format_version,
  calibration_failed,
};

std::string_view status_name(Status status);

// Serialize mutations of a Map. Independent Maps can compute on workers;
// generation is a pure function of seed, immutable config, version and cell ID.
// Observers and view_region throw std::logic_error on a moved-from Map;
// move assignment from a live Map restores normal use.
class Map {
public:
  explicit Map(std::uint64_t universe_seed, Config config = {},
               std::uint32_t format_version = current_format_version);
  ~Map();
  Map(Map &&) noexcept;
  Map &operator=(Map &&) noexcept;
  Map(const Map &) = delete;
  Map &operator=(const Map &) = delete;

  // Explicitly views the entire cell; private halos/calibration reveal nothing.
  // Invalid config/version or failed calibration adds no revelations.
  Status view_region(RegionId region);

  // ID-sorted snapshots owned by this Map, valid until its next mutation.
  std::span<const System> revealed_systems() const;
  std::span<const Lane> revealed_lanes() const;

  // Direct chord distance, not route distance; both IDs must be revealed.
  std::optional<std::int64_t> distance_milli_j(SystemId a, SystemId b) const;
  // Absent at construction; frozen by the first successful view/calibration.
  std::optional<std::int64_t> raw_units_per_j() const;

  const Config &config() const;
  std::uint64_t universe_seed() const;
  std::uint32_t format_version() const;

private:
  void require_impl() const;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace apsis::galaxy
