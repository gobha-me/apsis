#include "test_support.hpp"

#include <chrono>
#include <string>

namespace {
using namespace test;
using Clock = std::chrono::steady_clock;

std::string repeated_array(std::string_view item, std::size_t count) {
  std::string bytes;
  bytes.reserve(2 + count * (item.size() + 1));
  bytes += '[';
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0)
      bytes += ',';
    bytes += item;
  }
  bytes += ']';
  return bytes;
}

double parse_seconds(const std::string &bytes, std::size_t count) {
  const auto start = Clock::now();
  const auto parsed = parse_json(bytes);
  const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
  require(parsed.ok() && parsed.value->is_array() && parsed.value->size() == count,
          "all array entries must parse through the public API");
  return elapsed;
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  suite.run("400k object entries parse linearly like array entries", [] {
    // Build the inputs before timing; measure parsing rather than serialization
    // or DOM destruction. Callback-based DOM parsing rescanned the parent array
    // after every object and took about 39 seconds for this same input.
    constexpr std::size_t count = 400000;
    const auto arrays = repeated_array("[]", count);
    const auto objects = repeated_array("{}", count);
    const double array_seconds = parse_seconds(arrays, count);
    const double object_seconds = parse_seconds(objects, count);
    std::cout << "TIMING parse 400k []=" << array_seconds
              << "s {}=" << object_seconds << "s\n";
    require(object_seconds < 1.0,
            "400k objects must parse in under one second in the Release suite");
    require(object_seconds < 8 * array_seconds + 0.1,
            "object parsing must remain comparable to array parsing");
  });

  suite.run("6.6 MB malformed holds import remains bounded after parsing", [&] {
    auto bytes = blank().dump();
    const auto position = bytes.find("\"modules\":[]");
    require(position != std::string::npos, "blank has a modules placeholder");
    bytes.replace(position + std::string("\"modules\":").size(), 2,
                  repeated_array(R"({"craft":{"holds":0}})", 300000));
    require(bytes.size() > 6600000 && bytes.size() < InputLimits{}.maxBytes,
            "review input must exercise 6.6 MB inside the public byte limit");
    const auto start = Clock::now();
    const auto result = import_definition(bytes, active, schema_set);
    const double elapsed =
        std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << "TIMING import holds bytes=" << bytes.size() << " seconds="
              << elapsed << '\n';
    require(!result.ok() && result.issues.size() == 128 &&
                issue(result.issues, "issues_truncated", ""),
            "malformed holds must reach capped schema rejection");
    require(elapsed < 5.0,
            "linear parser must keep the large import well below 30 seconds");
  });

  suite.run("truncated carried blueprint schema trial is a nonmatch", [&] {
    auto blueprint = blank();
    auto craft = blank();
    for (std::size_t i = 0; i < 200; ++i)
      craft["unknown" + std::to_string(i)] = true;
    blueprint["decks"][0]["modules"].push_back(
        {{"id", 1}, {"type", "shuttleDock"}, {"x", 0}, {"y", 0}, {"r", 0},
         {"craft", std::move(craft)}});
    blueprint["nextId"] = 2;
    const auto result = import_definition(blueprint.dump(), active, schema_set);
    require(!result.ok() && !result.value.has_value(),
            "a truncated schema alternative must never import as pending");
    require(issue(result.issues, "schema_error", "/decks/0/modules/0/craft"),
            "200 unknown craft properties must reject the craft alternative");
  });

  suite.run("floor overflow independently reports grid capacity", [&] {
    auto blueprint = blank();
    for (std::size_t i = 0; i < 3000; ++i)
      blueprint["decks"][0]["floors"][std::to_string(i) + ",0"] = true;
    const auto result = import_definition(blueprint.dump(), active, schema_set);
    require(!result.ok(), "3000 floors must fail import");
    require(issue(result.issues, "grid_capacity", "/decks/0"),
            "floor overflow must report capacity before coordinate truncation");
    require(result.issues.size() == 128 &&
                issue(result.issues, "issues_truncated", ""),
            "invalid floor coordinates must retain the bounded issue marker");
  });

  return suite.finish();
}
