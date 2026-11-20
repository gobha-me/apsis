#include "test_support.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <system_error>

namespace {
template <typename T> T integer(std::string_view text) {
  T result{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result);
  galaxy_test::require(parsed.ec == std::errc{} &&
                           parsed.ptr == text.data() + text.size(),
                       "invalid probe argument");
  return result;
}
} // namespace

int main(int argc, char **argv) {
  try {
    galaxy_test::require(argc >= 2, "usage: galaxy-map-probe SEED [CX,CY ...]");
    apsis::galaxy::Map map(integer<std::uint64_t>(argv[1]));
    for (int i = 2; i < argc; ++i) {
      const std::string_view region(argv[i]);
      const auto comma = region.find(',');
      galaxy_test::require(comma != std::string_view::npos, "missing comma");
      const apsis::galaxy::RegionId id{
          integer<std::int32_t>(region.substr(0, comma)),
          integer<std::int32_t>(region.substr(comma + 1))};
      galaxy_test::require(map.view_region(id) == apsis::galaxy::Status::ok,
                           "probe view failed");
    }
    galaxy_test::dump_map(std::cout, map);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
