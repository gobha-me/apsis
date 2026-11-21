#include "dock_test_support.hpp"

#include <chrono>
#include <string>

namespace {
using namespace dock_test;

void capped(const Json &report, std::string_view operation,
            std::string_view ordinary_code) {
  require(report.at("schema") == "apsis-shipyard-result-v1" &&
              report.at("operation") == operation &&
              report.at("status") == "FAIL" && report.at("pass") == false &&
              report.at("data").is_null(),
          "truncated operations must fail with null data");
  const auto &issues = report.at("issues");
  require(issues.size() == 128,
          "amplifying input must saturate the 128-slot diagnostic allowance");
  std::size_t markers = 0;
  std::size_t ordinary = 0;
  for (const auto &item : issues) {
    require(!item.at("message").get<std::string>().empty(),
            "retained diagnostics must be readable");
    if (item.at("code") != "issues_truncated") {
      if (item.at("code") == ordinary_code)
        ++ordinary;
      continue;
    }
    ++markers;
    require(item.at("severity") == "error" && item.at("path") == "",
            "the sole terminal marker must remain a root error in both APIs");
    const auto &details = item.at("details");
    require(details.at("limit") == 128 &&
                details.at("suppressedAtLeast").is_number_integer() &&
                details.at("suppressedAtLeast").get<std::size_t>() >= 1,
            "the marker must identify the cap and at least one omitted issue");
  }
  require(markers == 1, "exactly one marker belongs inside the 128-slot cap");
  require(ordinary > 0, "the intended amplification path must be exercised");
  require(!has_issue(report, "grid_capacity"),
          "the overlap fixture must reach structure, not fail module-count bounds");
}

template <class Function>
void bounded_call(Function &&function) {
  const auto start = std::chrono::steady_clock::now();
  std::forward<Function>(function)();
  // A generous regression watchdog, in addition to the CTest process timeout;
  // this is not a latency requirement imposed on library callers.
  require(std::chrono::steady_clock::now() - start < std::chrono::seconds(30),
          "diagnostic amplification must complete within 30 seconds");
}

std::string overlapping_modules(std::string_view build_type) {
  auto blueprint = blank(build_type);
  blueprint["decks"] = Json::array();
  std::uint64_t id = 1;
  for (std::size_t d = 0; d < 5; ++d) {
    Json modules = Json::array();
    for (std::size_t m = 0; m < 624; ++m)
      modules.push_back({{"id", id++},
                         {"type", "microReactor"},
                         {"x", 0},
                         {"y", 0},
                         {"r", 0}});
    blueprint["decks"].push_back({{"name", "Overlapping deck"},
                                    {"floors", {{"0,0", true}}},
                                    {"modules", std::move(modules)}});
  }
  blueprint["nextId"] = id;
  return blueprint.dump();
}

std::string malformed_holds(std::size_t count) {
  auto bytes = blank().dump();
  const auto empty_modules = bytes.find("\"modules\":[]");
  require(empty_modules != std::string::npos,
          "the blank blueprint must contain a module-list placeholder");
  std::string modules = "[";
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0)
      modules += ',';
    modules += "{\"craft\":{\"holds\":0}}";
  }
  modules += ']';
  bytes.replace(empty_modules + std::string("\"modules\":").size(), 2,
                modules);
  require(bytes.size() < InputLimits{}.maxBytes,
          "the review's 300,000-hold body must fit the default 8 MiB cap");
  return bytes;
}

void schema_precedes_holds(const Json &report, std::string_view operation) {
  capped(report, operation, "schema_error");
  for (const auto &item : report.at("issues")) {
    const auto &code = item.at("code").get_ref<const std::string &>();
    const bool pending_hold = code.size() == 11 &&
                              code.starts_with("pending_F1") &&
                              code.back() >= '1' && code.back() <= '7';
    require(!pending_hold,
            "invalid schema must not be replaced by per-hold pending findings");
  }
}
} // namespace

int main() {
  using namespace dock_test;
  Suite suite;
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto host = station().dump();
  const auto ship = visitor().dump();

  suite.run("validate caps five full decks of overlapping modules", [&] {
    const auto bytes = overlapping_modules("ship");
    bounded_call([&] {
      capped(validate(bytes, active, schema), "validate", "module_overlap");
    });
  });

  suite.run("dock caps overlapping modules from either design", [&] {
    const auto bad_host = overlapping_modules("station");
    const auto bad_visitor = overlapping_modules("ship");
    bounded_call([&] {
      capped(dock(bad_host, 0, 1, ship, active, schema), "dock",
             "module_overlap");
    });
    bounded_call([&] {
      capped(dock(host, 0, 5, bad_visitor, active, schema), "dock",
             "module_overlap");
    });
  });

  suite.run("validate caps large malformed holds before pending traversal", [&] {
    for (const auto count : {1000U, 300000U}) {
      const auto bytes = malformed_holds(count);
      bounded_call([&] {
        schema_precedes_holds(validate(bytes, active, schema), "validate");
      });
    }
  });

  suite.run("dock caps large malformed holds in either design", [&] {
    const auto bytes = malformed_holds(300000);
    bounded_call([&] {
      schema_precedes_holds(dock(bytes, 0, 1, ship, active, schema), "dock");
    });
    bounded_call([&] {
      schema_precedes_holds(dock(host, 0, 5, bytes, active, schema), "dock");
    });
  });

  return suite.finish();
}
