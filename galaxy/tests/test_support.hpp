#pragma once

#include <apsis/galaxy/map.hpp>

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace galaxy_test {

inline void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void write_id(std::ostream &out, apsis::galaxy::SystemId id) {
  out << id.region.cx << ' ' << id.region.cy << ' ' << id.slot;
}

// A lossless, deliberately non-JSON public-API export for the CLI comparison.
// No private generator, cache or serialization implementation is included.
inline void dump_map(std::ostream &out, const apsis::galaxy::Map &map) {
  const auto &config = map.config();
  out << "M " << map.universe_seed() << ' ' << map.format_version() << ' '
      << apsis::galaxy::raw_ticks_per_map_unit << ' ' << config.cell_side_raw
      << ' ' << config.inner_radius_raw << ' ' << config.outer_radius_raw
      << ' ';
  if (map.raw_units_per_j()) {
    out << *map.raw_units_per_j();
  } else {
    out << "null";
  }
  out << '\n';
  for (const auto &system : map.revealed_systems()) {
    out << "S ";
    write_id(out, system.id);
    out << ' ' << system.position.x << ' ' << system.position.y << ' '
        << system.orbit.radius_raw << ' ' << system.orbit.epoch_vector.x << ' '
        << system.orbit.epoch_vector.y << '\n';
  }
  for (const auto &lane : map.revealed_lanes()) {
    out << "L ";
    write_id(out, lane.a);
    out << ' ';
    write_id(out, lane.b);
    const auto distance = map.distance_milli_j(lane.a, lane.b);
    require(distance.has_value(), "visible lane has no public distance");
    out << ' ' << lane.raw_length << ' ' << *distance << '\n';
  }
}

inline std::string snapshot(const apsis::galaxy::Map &map) {
  std::ostringstream out;
  dump_map(out, map);
  // Include non-lane chord distances as well as all serialized map fields.
  for (const auto &a : map.revealed_systems()) {
    for (const auto &b : map.revealed_systems()) {
      const auto distance = map.distance_milli_j(a.id, b.id);
      require(distance.has_value(), "visible systems have no public distance");
      out << "D ";
      write_id(out, a.id);
      out << ' ';
      write_id(out, b.id);
      out << ' ' << *distance << '\n';
    }
  }
  return out.str();
}

} // namespace galaxy_test
