#include "dock_test_support.hpp"

#include <array>
#include <string_view>

namespace {
using namespace dock_test;

struct Fault {
  char check;
  std::string_view code;
  std::string_view path;
  Json host;
  Json ship;
};

Fault fault(char check) {
  Fault result{check, "", "", station(), visitor()};
  switch (check) {
  case 'a':
    // The microreactor remains present but its one-cell footprint lacks floor.
    result.ship["decks"][0]["floors"].erase("5,3");
    result.code = "missing_floor";
    result.path = "/visitor/blueprint/decks/0/modules/1";
    break;
  case 'b':
    // Ring the nonwalkable port with nonwalkable seats. Every seat has crew
    // access from outside the ring; the port alone has none.
    for (const auto &[x, y] : std::array<std::pair<int, int>, 6>{
             {{5, 10}, {5, 11}, {8, 10}, {8, 11}, {6, 9}, {7, 9}}})
      add_module(result.host, "shuttleSeats", x, y);
    result.code = "dock_port_unreachable";
    result.path = "/host/blueprint/decks/0/modules/4";
    break;
  case 'c':
    // Both labels are valid rule interfaces; the mismatch is docking legality,
    // not malformed input or an unusable visitor airlock.
    result.ship["decks"][0]["modules"][3]["interface"] = "cargo";
    result.code = "dock_no_matching_airlock";
    result.path = "/visitor/blueprint/decks";
    break;
  case 'd':
    // A connected U-shaped hull extension blocks the approach at y=14 while
    // leaving the port's immediately outward face (y=12) on the exterior.
    fill_rectangle(result.host, 10, 12, 1, 3);
    fill_rectangle(result.host, 6, 14, 4, 1);
    result.code = "dock_blocked_approach";
    result.path = "/host/blueprint/decks/0/modules/4";
    break;
  default:
    throw std::runtime_error("Unknown F18 check");
  }
  return result;
}

void disable(DockOptions &options, char check) {
  switch (check) {
  case 'a':
    options.structural = false;
    break;
  case 'b':
    options.portReachability = false;
    break;
  case 'c':
    options.matchingAirlock = false;
    break;
  case 'd':
    options.approachClearance = false;
    break;
  default:
    throw std::runtime_error("Unknown disabled F18 check");
  }
}

void caught(const Json &report, const Fault &input) {
  require(has_issue(report, input.code, input.path),
          std::string(input.code) + " must catch the enabled F18 fault");
  const auto key = std::string(1, input.check);
  const auto &check = report.at("data").at("checks").at(key);
  require(check.at("enabled") == true && check.at("status") == "FAIL",
          "The responsible F18 check must be enabled and FAIL");
  require(report.at("status") == "FAIL" && report.at("pass") == false,
          "Caught docking faults must fail the result envelope");
}

void paired_mutation(char check, const RulesSnapshot &active,
                     const SchemaSet &schema) {
  const auto input = fault(check);
  const auto enabled = dock_at_port(input.host, input.ship, active, schema);
  caught(enabled, input);

  DockOptions options;
  disable(options, check);
  const auto disabled =
      dock_at_port(input.host, input.ship, active, schema, options);
  require(!has_issue(disabled, input.code),
          "Disabling only the responsible option must miss this fault");
  const auto &checks = disabled.at("data").at("checks");
  for (const char letter : {'a', 'b', 'c', 'd'}) {
    const auto &entry = checks.at(std::string(1, letter));
    require(entry.at("enabled") == (letter != check),
            "Exactly one DockOptions check must be disabled");
    if (letter == check)
      require(entry.at("status") == "DISABLED",
              "The disabled check must report DISABLED");
    else
      require(entry.at("status") == (letter == 'c' ? "PENDING" : "PASS"),
              "Other checks must have no fault in the mutation fixture");
  }
  if (check == 'b' || check == 'd') {
    require(
        !has_issue(disabled,
                   check == 'b' ? "unreachable_component" : "blocked_approach",
                   input.path),
        "Selected-target duplicate findings under (a) must be suppressed");
    require(
        has_issue(enabled,
                  check == 'b' ? "unreachable_component" : "blocked_approach",
                  input.path),
        "Default (a) must retain the selected target's structural finding");
  }
  if (check == 'c')
    require(disabled.at("pass") == true &&
                disabled.at("data").at("pending") == false,
            "The interface fault is the only reason this noncandidate fails");
  else
    require(disabled.at("pass") == false && has_issue(disabled, "pending_F21"),
            "A missed fault must not guess a certified alignment while F21 is "
            "open");
}

void structural_airlock_fault(bool blocked, const RulesSnapshot &active,
                              const SchemaSet &schema) {
  auto ship = visitor();
  // A second usable airlock isolates the faulty airlock's own finding from the
  // design-level requirement for at least one usable airlock.
  add_module(ship, "airlock", 11, 5, 3);
  if (blocked) {
    fill_rectangle(ship, 0, 8, 2, 1);
    fill_rectangle(ship, 0, 5, 1, 3);
  } else {
    // A left-edge airlock has no exterior-side floor; enclose its three inner
    // neighbors with reachable nonwalkable seats.
    for (const auto &[x, y] :
         std::array<std::pair<int, int>, 3>{{{2, 4}, {2, 6}, {3, 5}}})
      add_module(ship, "shuttleSeats", x, y);
  }
  const std::string_view code =
      blocked ? "blocked_approach" : "unreachable_component";
  const std::string_view path = "/visitor/blueprint/decks/0/modules/3";
  const auto report = dock_at_port(station(), ship, active, schema);
  require(has_issue(report, code, path),
          "Visitor airlock access and clearance must be structural findings");
  require(report.at("data").at("checks").at("a").at("status") == "FAIL" &&
              report.at("data").at("checks").at("c").at("status") == "PENDING",
          "Visitor airlock faults belong to (a), with compatible candidates "
          "pending in (c)");
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code && item.at("path") == path)
      require(
          item.at("severity") == "error" &&
              item.at("details").at("rule") ==
                  "/validation/checks/" + std::string(code),
          "Visitor access/clearance uses the active structural error severity");
  DockOptions options;
  options.structural = false;
  const auto disabled = dock_at_port(station(), ship, active, schema, options);
  require(
      !has_issue(disabled, code) &&
          disabled.at("data").at("checks").at("c").at("status") == "PENDING",
      "(c) must not duplicate visitor access/clearance when (a) is disabled");
  options = {};
  if (blocked)
    options.approachClearance = false;
  else
    options.portReachability = false;
  const auto target_disabled =
      dock_at_port(station(), ship, active, schema, options);
  require(has_issue(target_disabled, code, path) &&
              target_disabled.at("data").at("checks").at("a").at("status") ==
                  "FAIL",
          "Disabling target access/clearance must retain visitor structural "
          "findings");

  auto changed_rules = active.data();
  changed_rules["validation"]["checks"][code]["severity"] = "warning";
  const auto warning_rules = load_rules(changed_rules.dump(), schema);
  require(warning_rules.ok(), "An active structural warning rule must load");
  const auto warning =
      dock_at_port(station(), ship, *warning_rules.value, schema);
  require(warning.at("data").at("checks").at("a").at("status") == "PASS" &&
              warning.at("data").at("checks").at("c").at("status") == "PENDING",
          "Rule-defined visitor warnings must not become errors or c failures");
  require(has_issue(warning, code, path),
          "Warning must preserve the precise finding");
  for (const auto &item : warning.at("issues"))
    if (item.at("code") == code && item.at("path") == path)
      require(item.at("severity") == "warning",
              "Active severity must be honored");
}
} // namespace

int main(int argc, char **argv) {
  using namespace dock_test;
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

  if (argc > 1) {
    if ((argc != 3 && argc != 5) ||
        std::string_view(argv[1]) != "--enabled-only" ||
        std::string_view(argv[2]).size() != 1 ||
        (argc == 5 && (std::string_view(argv[3]) != "--disable" ||
                       std::string_view(argv[4]).size() != 1))) {
      std::cerr << "Usage: shipyard-dock-tests --enabled-only a|b|c|d "
                   "[--disable a|b|c|d]\n";
      return 2;
    }
    suite.run("enabled-only F18 assertion", [&] {
      const auto input = fault(argv[2][0]);
      DockOptions options;
      if (argc == 5)
        disable(options, argv[4][0]);
      caught(dock_at_port(input.host, input.ship, active, schema, options),
             input);
    });
    return suite.finish();
  }

  suite.run("baseline station and visitor are structurally valid", [&] {
    require(validate(station().dump(), active, schema).at("pass") == true,
            "Independent host fixture must pass structural validation");
    require(validate(visitor().dump(), active, schema).at("pass") == true,
            "Independent visitor fixture must pass structural validation");
    const auto report = dock_at_port(station(), visitor(), active, schema);
    require(
        report.at("data").at("checks").at("a").at("status") == "PASS" &&
            report.at("data").at("checks").at("b").at("status") == "PASS" &&
            report.at("data").at("checks").at("d").at("status") == "PASS" &&
            report.at("data").at("checks").at("c").at("status") == "PENDING" &&
            report.at("pass") == false && has_issue(report, "pending_F21"),
        "Only unresolved F21 prevents certification of the baseline port dock");
  });
  for (const char check : {'a', 'b', 'c', 'd'})
    suite.run("F18(" + std::string(1, check) + ") independent disable mutation",
              [&] { paired_mutation(check, active, schema); });

  suite.run("structural check covers host faults too", [&] {
    auto host = station();
    host["decks"][0]["floors"].erase("5,3");
    const auto report = dock_at_port(host, visitor(), active, schema);
    require(has_issue(report, "missing_floor",
                      "/host/blueprint/decks/0/modules/1") &&
                report.at("data").at("checks").at("a").at("status") == "FAIL",
            "Host structural faults must fail (a)");
    DockOptions options;
    options.structural = false;
    const auto disabled =
        dock_at_port(host, visitor(), active, schema, options);
    require(!has_issue(disabled, "missing_floor"),
            "Disabled (a) must miss host faults");
  });
  suite.run(
      "disabling structural leaves target access and approach active", [&] {
        for (const char check : {'b', 'd'}) {
          const auto input = fault(check);
          DockOptions options;
          options.structural = false;
          const auto report =
              dock_at_port(input.host, input.ship, active, schema, options);
          require(has_issue(report, input.code, input.path) &&
                      report.at("data")
                              .at("checks")
                              .at(std::string(1, check))
                              .at("status") == "FAIL",
                  "Target access/approach remains active without structural "
                  "checking");
        }
      });
  suite.run("target switches retain other modules' structural findings", [&] {
    for (const char check : {'b', 'd'}) {
      auto input = fault(check);
      const auto other_index = input.host.at("decks")[0].at("modules").size();
      add_module(input.host, "dockingPort", 10, 6, 3);
      if (check == 'b') {
        for (const auto &[x, y] : std::array<std::pair<int, int>, 6>{
                 {{9, 6}, {9, 7}, {10, 5}, {11, 5}, {10, 8}, {11, 8}}})
          add_module(input.host, "shuttleSeats", x, y);
      } else {
        fill_rectangle(input.host, 12, 9, 3, 1);
        fill_rectangle(input.host, 14, 6, 1, 3);
      }
      DockOptions options;
      disable(options, check);
      const auto report =
          dock_at_port(input.host, input.ship, active, schema, options);
      const std::string_view code =
          check == 'b' ? "unreachable_component" : "blocked_approach";
      require(!has_issue(report, code, input.path) &&
                  !has_issue(report, input.code),
              "Only the selected target's disabled finding must be suppressed");
      require(has_issue(report, code,
                        "/host/blueprint/decks/0/modules/" +
                            std::to_string(other_index)) &&
                  report.at("data").at("checks").at("a").at("status") == "FAIL",
              "An unselected target's structural fault must remain under (a)");
    }
  });
  suite.run("visitor airlock access belongs to structural rule severity",
            [&] { structural_airlock_fault(false, active, schema); });
  suite.run("visitor airlock clearance belongs to structural rule severity",
            [&] { structural_airlock_fault(true, active, schema); });
  return suite.finish();
}
