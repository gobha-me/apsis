#include "dock_test_support.hpp"

#include <string>

namespace {
using namespace dock_test;

std::size_t count(const Json &report, std::string_view code) {
  return static_cast<std::size_t>(std::count_if(
      report.at("issues").begin(), report.at("issues").end(),
      [&](const Json &item) { return item.at("code") == code; }));
}

void terminal_marker(const Json &report, std::string_view operation) {
  require(report.at("schema") == "apsis-shipyard-result-v1" &&
              report.at("operation") == operation &&
              report.at("status") == "FAIL" && report.at("pass") == false &&
              report.at("data").is_null(),
          "resource truncation must fail with the documented null data");
  require(report.at("issues").size() <= 128 &&
              count(report, "issues_truncated") == 1,
          "the public cap must retain exactly one marker within 128 slots");
  for (const auto &item : report.at("issues")) {
    if (item.at("code") != "issues_truncated")
      continue;
    require(item.at("severity") == "error" && item.at("path") == "" &&
                !item.at("message").get<std::string>().empty(),
            "the terminal marker must remain a readable root error");
    const auto &details = item.at("details");
    require(details.at("limit") == 128 &&
                details.at("suppressedAtLeast").is_number_integer() &&
                details.at("suppressedAtLeast").get<std::size_t>() >= 1,
            "the marker must expose its limit and omitted-issue lower bound");
  }
}

Json twelve_reactors(Json blueprint) {
  // Both public fixtures already have one reactor at (5,3). Twelve total
  // reactors produce exactly 66 pairwise overlaps, below the per-design cap.
  for (int i = 0; i < 11; ++i)
    add_module(blueprint, "microReactor", 5, 3);
  return blueprint;
}
} // namespace

int main() {
  using namespace dock_test;
  Suite suite;
  const auto schema = schemas();
  const auto active = rules(schema);

  suite.run("N10: carried craft truncation cannot become a ready warning", [&] {
    const auto imported = import_definition(
        read("fixtures/v3/apsis-kestrel-tender-definition-v3.json"), active,
        schema);
    require(imported.ok(), "canonical Kestrel Tender must import");
    auto source = imported.value->definition.at("blueprint");
    require(validate(source.dump(), active, schema).at("pass") == true,
            "canonical carrier must provide a passing control");
    bool changed = false;
    for (auto &deck : source["decks"]) {
      for (auto &module : deck["modules"]) {
        if (module.at("type") != "shuttleDock")
          continue;
        auto &craft = module["craft"];
        const auto first = craft.at("decks").at(0).at("modules").at(0);
        for (int i = 0; i < 200; ++i)
          add_module(craft, "microReactor", first.at("x").get<int>(), first.at("y").get<int>());
        changed = true;
      }
    }
    require(changed, "Kestrel must contain its canonical carried craft");
    const auto report = validate(source.dump(), active, schema);
    terminal_marker(report, "validate");
    require(report.at("issues").size() == 128 &&
                count(report, "module_overlap") > 0,
            "carried truncation must preserve its bounded diagnostic prefix");
    require(!has_issue(report, "carried_not_ready"),
            "truncation must stay terminal rather than becoming a carrier warning");
  });

  const auto host = twelve_reactors(station());
  const auto ship = twelve_reactors(visitor());

  suite.run("N16: combining two uncapped designs retains the shared prefix", [&] {
    const auto host_report = validate(host.dump(), active, schema);
    const auto ship_report = validate(ship.dump(), active, schema);
    require(host_report.at("issues").size() == 67 &&
                ship_report.at("issues").size() == 67 &&
                count(host_report, "module_overlap") == 66 &&
                count(ship_report, "module_overlap") == 66 &&
                !has_issue(host_report, "issues_truncated") &&
                !has_issue(ship_report, "issues_truncated"),
            "each twelve-reactor design must remain independently below the cap");
    const auto report = dock_at_port(host, ship, active, schema);
    terminal_marker(report, "dock");
    require(report.at("issues").size() == 128,
            "combined structural truncation must preserve 127 issues and marker");
    // The structural temporary contains 127 overlaps plus its marker. The
    // public shared cap also includes two earlier import warnings, leaving
    // 125 overlap slots; these warnings must not be silently discarded.
    require(count(report, "module_overlap") == 125 &&
                count(report, "pending_F20") == 0 &&
                count(report, "pending_performance") == 2,
            "combined diagnostics must retain the overlap prefix and import warnings");
  });

  suite.run("N15: disabling structural checks cannot hide resource truncation", [&] {
    DockOptions options;
    options.structural = false;
    const auto report = dock_at_port(host, ship, active, schema, options);
    terminal_marker(report, "dock");
    require(report.at("issues").size() == 3 &&
                count(report, "module_overlap") == 0 &&
                count(report, "pending_F20") == 0 &&
                count(report, "pending_performance") == 2,
            "disabled structural reporting must still retain the terminal marker");
  });

  return suite.finish();
}
