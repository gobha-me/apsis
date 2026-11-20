#include <apsis/galaxy/map.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using apsis::galaxy::Map;
using apsis::galaxy::Point;
using apsis::galaxy::RegionId;
using apsis::galaxy::Status;
using apsis::galaxy::SystemId;

template <typename Integer>
bool parse_integer(std::string_view text, Integer &value) {
  if (text.empty()) {
    return false;
  }
  const auto digits = text.front() == '-' ? text.substr(1) : text;
  if (digits.empty() ||
      (digits.front() == '0' && (digits.size() != 1 || text.front() == '-'))) {
    return false;
  }
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_region(std::string_view text, RegionId &region) {
  const auto comma = text.find(',');
  return comma != std::string_view::npos &&
         parse_integer(text.substr(0, comma), region.cx) &&
         parse_integer(text.substr(comma + 1), region.cy);
}

int argument_error(std::string_view message) {
  // Messages are fixed literals, without JSON string metacharacters.
  std::cerr << "{\"status\":\"invalid_arguments\",\"error\":\"" << message
            << "\"}\n";
  return 2;
}

void write_region(RegionId region) {
  std::cout << "{\"cx\":" << region.cx << ",\"cy\":" << region.cy << '}';
}

void write_id(SystemId id) {
  std::cout << "{\"region\":";
  write_region(id.region);
  std::cout << ",\"slot\":" << id.slot << '}';
}

void write_point(Point point) {
  std::cout << "{\"x\":" << point.x << ",\"y\":" << point.y << '}';
}

void write_optional(std::optional<std::int64_t> value) {
  if (value) {
    std::cout << *value;
  } else {
    std::cout << "null";
  }
}

void write_map(const Map &map, const std::vector<RegionId> &viewed,
               Status status) {
  const auto &config = map.config();
  std::cout << "{\"format_version\":" << map.format_version() << ",\"seed\":\""
            << map.universe_seed() << "\",\"config\":{"
            << "\"raw_ticks_per_map_unit\":"
            << apsis::galaxy::raw_ticks_per_map_unit
            << ",\"cell_side_raw\":" << config.cell_side_raw
            << ",\"inner_radius_raw\":" << config.inner_radius_raw
            << ",\"outer_radius_raw\":" << config.outer_radius_raw
            << "},\"status\":\"" << apsis::galaxy::status_name(status)
            << "\",\"raw_units_per_j\":";
  write_optional(map.raw_units_per_j());
  std::cout << ",\"viewed_regions\":[";
  bool first = true;
  for (const auto region : viewed) {
    if (!first) {
      std::cout << ',';
    }
    first = false;
    write_region(region);
  }
  std::cout << "],\"systems\":[";
  first = true;
  for (const auto &system : map.revealed_systems()) {
    if (!first) {
      std::cout << ',';
    }
    first = false;
    std::cout << "{\"id\":";
    write_id(system.id);
    std::cout << ",\"position_raw\":";
    write_point(system.position);
    std::cout << ",\"orbit\":{\"radius_raw\":" << system.orbit.radius_raw
              << ",\"epoch_vector_raw\":";
    write_point(system.orbit.epoch_vector);
    std::cout << "}}";
  }
  std::cout << "],\"lanes\":[";
  first = true;
  for (const auto &lane : map.revealed_lanes()) {
    if (!first) {
      std::cout << ',';
    }
    first = false;
    std::cout << "{\"a\":";
    write_id(lane.a);
    std::cout << ",\"b\":";
    write_id(lane.b);
    std::cout << ",\"raw_length\":" << lane.raw_length << ",\"milli_j\":";
    write_optional(map.distance_milli_j(lane.a, lane.b));
    std::cout << '}';
  }
  std::cout << "]}\n";
}

} // namespace

int main(int argc, char **argv) {
  std::optional<std::uint64_t> seed;
  std::vector<RegionId> regions;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      std::cout << "Usage: galaxy-cli --seed N [--region cx,cy ...]\n"
                   "N is an unsigned 64-bit decimal integer. Each region views "
                   "a whole cell.\n"
                   "Without regions, the map is unrevealed and J is null.\n";
      return 0;
    }
    if (argument != "--seed" && argument != "--region") {
      return argument_error("unknown option; use --help for usage");
    }
    if (index + 1 == argc) {
      return argument_error("option requires a value");
    }
    const std::string_view value(argv[++index]);
    if (argument == "--seed") {
      if (seed) {
        return argument_error("--seed must appear exactly once");
      }
      std::uint64_t parsed_seed{};
      if (!parse_integer(value, parsed_seed)) {
        return argument_error(
            "seed must be an unsigned 64-bit decimal integer");
      }
      seed = parsed_seed;
    } else {
      RegionId region{};
      if (!parse_region(value, region)) {
        return argument_error("region must be two signed 32-bit decimal "
                              "integers separated by a comma");
      }
      regions.push_back(region);
    }
  }
  if (!seed) {
    return argument_error("--seed is required");
  }

  Map map(*seed);
  std::vector<RegionId> viewed;
  for (const auto region : regions) {
    const auto status = map.view_region(region);
    if (status != Status::ok) {
      std::sort(viewed.begin(), viewed.end());
      viewed.erase(std::unique(viewed.begin(), viewed.end()), viewed.end());
      write_map(map, viewed, status);
      return 1;
    }
    viewed.push_back(region);
  }
  std::sort(viewed.begin(), viewed.end());
  viewed.erase(std::unique(viewed.begin(), viewed.end()), viewed.end());
  write_map(map, viewed, Status::ok);
  return std::cout ? 0 : 1;
}
