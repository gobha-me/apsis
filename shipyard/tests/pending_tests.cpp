#include "test_support.hpp"

#include <apsis/shipyard/validator.hpp>

#include <set>
#include <vector>

namespace {
using namespace test;

const Json &find_issue(const Json &report, std::string_view code) {
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code)
      return item;
  throw std::runtime_error("Missing issue: " + std::string(code) + " in " +
                           report.dump());
}

bool has_issue(const Json &report, std::string_view code) {
  return std::any_of(report.at("issues").begin(), report.at("issues").end(),
                     [&](const Json &item) { return item.at("code") == code; });
}

Json imported(std::string_view name, const RulesSnapshot &active,
              const SchemaSet &schema) {
  const auto result = import_definition(
      read("fixtures/v3/apsis-" + std::string(name) + "-definition-v3.json"),
      active, schema);
  require(result.ok(), "Canonical input must import before validation");
  return result.value->definition;
}

void pending_holds(const Json &report, std::string_view operation,
                   const std::string &path) {
  require(
      report.at("operation") == operation && report.at("status") == "FAIL" &&
          report.at("pass") == false && report.at("data").is_null(),
      "Holds must block with null data, without guessed totals or traversal");
  require(report.at("issues").size() == 7,
          "Only the seven pending hold findings may be assigned");
  for (const auto *finding :
       {"F11", "F12", "F13", "F14", "F15", "F16", "F17"}) {
    const auto &item = find_issue(report, "pending_" + std::string(finding));
    require(item.at("severity") == "error" && item.at("path") == path &&
                item.at("details").at("finding") == finding &&
                item.at("details").at("issue") == 60 &&
                !item.at("message").get<std::string>().empty(),
            "Hold findings need structured pending identity and source path");
  }
  require(!has_issue(report, "missing_floor"),
          "A requested hold opening must not become an ordinary floor fault");
}

void no_excluded_values(const Json &derived) {
  static const std::set<std::string> excluded{"acceleration",
                                              "range",
                                              "burnSeconds",
                                              "fullBurnSeconds",
                                              "currentBurnSeconds",
                                              "exhaustVelocity",
                                              "deltaV",
                                              "fuelToDryMassRatio"};
  if (derived.is_object()) {
    for (auto it = derived.begin(); it != derived.end(); ++it) {
      require(!excluded.contains(it.key()),
              "F20 must omit excluded derived values, including stand-ins: " +
                  it.key());
      no_excluded_values(it.value());
    }
  } else if (derived.is_array()) {
    for (const auto &value : derived)
      no_excluded_values(value);
  }
}

void pending_f20(const Json &report, bool zero_mass) {
  const auto &item = find_issue(report, "pending_F20");
  require(item.at("severity") == "warning" &&
              item.at("details").at("finding") == "F20" &&
              item.at("details").at("issue") == 51,
          "F20 must remain a nonblocking structured pending report");
  const auto &evaluations = item.at("details").at("evaluations");
  std::set<std::string> actual;
  for (const auto &evaluation : evaluations) {
    const auto key = evaluation.at("formula").get<std::string>() + "/" +
                     evaluation.at("operand").get<std::string>() + "/" +
                     evaluation.value("scenario", std::string());
    require(actual.insert(key).second,
            "F20 evaluation identities must be unique");
  }
  std::set<std::string> expected{"burnSeconds/burnRate/full",
                                 "burnSeconds/burnRate/current",
                                 "exhaustVelocity/burnRate/"};
  if (zero_mass) {
    for (const auto *key :
         {"acceleration/mass/", "range/mass/", "deltaV/endMass/",
          "deltaV/ln(0)/", "fuelToDryMassRatio/dryMass/"})
      expected.insert(key);
  }
  require(actual == expected, "F20 must name exactly the excluded evaluations");
  for (const auto &issue : report.at("issues"))
    require(issue.at("code") != "formula_error" &&
                issue.at("code") != "nonfinite_total",
            "Pending F20 must not become a formula rejection");
}

void port_pending(const Json &report) {
  require(report.at("status") == "FAIL" && report.at("pass") == false &&
              report.at("data").at("pending") == true,
          "A compatible port remains uncertified until F21 is answered");
  const auto &checks = report.at("data").at("checks");
  for (const auto *check : {"a", "b", "d"})
    require(checks.at(check).at("enabled") == true &&
                checks.at(check).at("status") == "PASS",
            "Settled structural, reachability and approach checks must pass: " +
                std::string(check));
  require(checks.at("c").at("enabled") == true &&
              checks.at("c").at("status") == "PENDING",
          "A compatible candidate does not certify an F21 alignment");
  const auto &issue = find_issue(report, "pending_F21");
  require(issue.at("severity") == "warning" &&
              issue.at("details").at("finding") == "F21" &&
              issue.at("details").at("check") == "c",
          "Alignment needs a structured pending F21 report");
  for (const auto &item : report.at("issues"))
    require(item.at("severity") != "error",
            "Port candidate has no settled fault");
  require(!report.at("data").contains("hangar") &&
              report.at("data").at("target").size() == 4,
          "Port data must not acquire hangar size limits");
}

Json module(std::uint64_t id, std::string_view type, int x, int y,
            int rotation) {
  return {{"id", id}, {"type", type}, {"x", x}, {"y", y}, {"r", rotation}};
}

RulesSnapshot rotation_only_hangar(const RulesSnapshot &active,
                                   const SchemaSet &schema) {
  auto changed = active.data();
  // Wren is 20x34 m. A real rules snapshot with a 34x20 m hangar isolates
  // the unresolved rotation case; default 10x18 m definite oversize is covered
  // separately in dock_regression_tests.cpp with a required size error.
  for (auto &part : changed["parts"])
    if (part.at("id") == "stationHangar") {
      part["docking"]["maxWidthM"] = 34;
      part["docking"]["maxLengthM"] = 20;
    }
  auto loaded = load_rules(changed.dump(), schema);
  require(loaded.ok(), "Rotation-only hangar rules must load publicly");
  return std::move(*loaded.value);
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto host = imported("wayfarer-anchorage", active, schema);
  const auto wren = imported("wren", active, schema);
  Suite suite;

  suite.run(
      "requested holds remain pending F11-F17 in direct and wrapped input",
      [&] {
        auto blueprint = wren.at("blueprint");
        // Only request the feature: no unapproved hold record shape is
        // invented.
        blueprint["holds"] = Json::array();
        blueprint["decks"][0]["floors"].erase("12,3");
        pending_holds(validate(blueprint.dump(), active, schema), "validate",
                      "/holds");
        auto wrapped = wren;
        wrapped["blueprint"] = blueprint;
        pending_holds(validate(wrapped.dump(), active, schema), "validate",
                      "/blueprint/holds");
        pending_holds(dock(host.dump(), 1, 14, wrapped.dump(), active, schema),
                      "dock", "/visitor/blueprint/holds");
        auto host_request = host;
        host_request["blueprint"]["holds"] = Json::array();
        pending_holds(
            dock(host_request.dump(), 1, 14, wren.dump(), active, schema),
            "dock", "/host/blueprint/holds");
      });

  suite.run(
      "nested carried hold requests block validate and both dock inputs", [&] {
        auto carrier = blank();
        auto craft = blank();
        craft["holds"] = Json::array();
        auto berth = module(1, "shuttleDock", 0, 0, 0);
        berth["craft"] = craft;
        carrier["decks"][0]["modules"].push_back(berth);
        carrier["nextId"] = 2;
        const std::string path = "/decks/0/modules/0/craft/holds";
        pending_holds(validate(carrier.dump(), active, schema), "validate",
                      path);
        pending_holds(dock(host.dump(), 1, 14, carrier.dump(), active, schema),
                      "dock", "/visitor" + path);
        carrier["buildType"] = "station";
        pending_holds(dock(carrier.dump(), 0, 1, wren.dump(), active, schema),
                      "dock", "/host" + path);
      });

  suite.run(
      "blank ships and stations retain F20 pending without numeric substitutes",
      [&] {
        for (const auto *build_type : {"ship", "station"}) {
          const auto bytes = blank(build_type).dump();
          const auto loaded = import_definition(bytes, active, schema);
          require(loaded.ok(),
                  "A blank design must import without formula rejection");
          const Json import_report = {{"issues", loaded.issues}};
          pending_f20(import_report, true);
          no_excluded_values(loaded.value->definition.at("derived"));
          const auto report = validate(bytes, active, schema);
          pending_f20(report, true);
          require(
              !report.at("data").is_null() &&
                  has_issue(report, "required_system"),
              "Blank validation must retain its inspectable structural report");
          no_excluded_values(report.at("data").at("definition").at("derived"));
        }
      });

  suite.run(
      "Wayfarer passes structure with exactly its three F20 exclusions", [&] {
        const auto report = validate(host.dump(), active, schema);
        require(report.at("pass") == true &&
                    report.at("data").at("structuralPass") == true,
                "Engine-less Wayfarer must not fail for pending burn-rate "
                "formulas");
        pending_f20(report, false);
        no_excluded_values(report.at("data").at("definition").at("derived"));
      });

  suite.run(
      "20 by 34 metre Wren has no port size envelope and awaits F21", [&] {
        require(
            wren.at("derived").at("widthM") == 20 &&
                wren.at("derived").at("lengthM") == 34,
            "The owner-specified visitor must really measure 20 by 34 metres");
        port_pending(dock(host.dump(), 1, 14, wren.dump(), active, schema));
      });

  suite.run(
      "visitor wider than the former port cap still only awaits F21", [&] {
        auto wide = wren.at("blueprint");
        for (int x = 0; x < 26; ++x)
          wide["decks"][0]["floors"][std::to_string(x) + ",10"] = true;
        const auto checked = validate(wide.dump(), active, schema);
        require(checked.at("pass") == true && checked.at("data")
                                                      .at("definition")
                                                      .at("derived")
                                                      .at("widthM") == 52,
                "A connected 52-metre visitor must remain structurally valid");
        const auto old_rules =
            Json::parse(read("fixtures/v3/apsis-rules-v3.json"));
        bool beyond_old_cap = false;
        for (const auto &part : old_rules.at("parts"))
          if (part.at("id") == "dockingPort")
            beyond_old_cap =
                52 > part.at("docking").at("maxWidthM").get<double>();
        require(beyond_old_cap,
                "This visitor must exceed the actual historical port cap");
        port_pending(dock(host.dump(), 1, 14, wide.dump(), active, schema));
      });

  suite.run(
      "multiple compatible exterior airlocks are counted without picking one",
      [&] {
        auto visitor = wren.at("blueprint");
        auto second = module(13, "airlock", 13, 3, 3);
        second["interface"] = "standard";
        visitor["decks"][0]["modules"].push_back(second);
        visitor["nextId"] = 14;
        const auto report =
            dock(host.dump(), 1, 14, visitor.dump(), active, schema);
        port_pending(report);
        const Json counts = {{"airlockCount", 2},
                             {"compatibleInterfaceCount", 2},
                             {"exteriorOutwardCount", 2}};
        require(report.at("data").at("checks").at("c").at("details") == counts,
                "The airlock check reports all candidates, with no chosen ID "
                "or guessed alignment");
        require(report.at("data").size() == 3,
                "Dock data contains target, checks and pending, without an "
                "airlock selection");
        auto &modules = visitor["decks"][0]["modules"];
        std::reverse(modules.begin(), modules.end());
        const auto reversed =
            dock(host.dump(), 1, 14, visitor.dump(), active, schema);
        require(reversed == report, "Reordering compatible candidates must not "
                                    "reveal a first-airlock picker");
      });

  suite.run(
      "rotation-only hangar fit remains uncertified with matching check disabled",
      [&] {
        const auto rotation_rules = rotation_only_hangar(active, schema);
        DockOptions options;
        options.matchingAirlock = false;
        const auto report =
            dock(host.dump(), 1, 15, wren.dump(), rotation_rules, schema, options);
        require(report.at("pass") == false &&
                    report.at("data").at("pending") == true,
                "Disabling F21 must not bypass the unresolved hangar fit");
        const auto &hangar = report.at("data").at("hangar");
        const auto &issue = find_issue(report, "pending_F19");
        require(issue.at("severity") == "warning" &&
                    issue.at("details").at("finding") == "F19" &&
                    hangar.at("widthLengthFit").is_null() &&
                    hangar.at("visitorWidthM") == 20 &&
                    hangar.at("visitorLengthM") == 34 &&
                    hangar.at("deckCountFits") == true,
                "F19 leaves width/length fit null, rather than guessing "
                "allowed rotations");
        for (const auto &part : rotation_rules.data().at("parts"))
          if (part.at("id") == "stationHangar") {
            for (const auto *key : {"maxWidthM", "maxLengthM", "maxDecks"})
              require(hangar.at(key) == part.at("docking").at(key),
                      "Hangar limits must come from active rules");
          }
        for (const auto &item : report.at("issues"))
          require(item.at("severity") != "error",
                  "No guessed hangar dimensional rejection");
      });

  suite.run(
      "hangar enforces the settled deck cap while F19 and F21 stay pending",
      [&] {
        const auto rotation_rules = rotation_only_hangar(active, schema);
        auto visitor = wren.at("blueprint");
        visitor["decks"][0]["modules"].push_back(
            module(13, "ladder", 13, 10, 0));
        for (std::uint64_t id : {14, 15})
          visitor["decks"].push_back(
              {{"name", "Linked deck"},
               {"floors", {{"13,10", true}}},
               {"modules", Json::array({module(id, "ladder", 13, 10, 0)})}});
        visitor["nextId"] = 16;
        require(validate(visitor.dump(), active, schema).at("pass") == true,
                "The over-cap visitor must have valid paired ladders and crew "
                "routes");
        const auto report =
            dock(host.dump(), 1, 15, visitor.dump(), rotation_rules, schema);
        const auto &issue = find_issue(report, "dock_hangar_deck_limit");
        const auto &hangar = report.at("data").at("hangar");
        require(
            report.at("pass") == false && issue.at("severity") == "error" &&
                issue.at("details").at("actual") == 3 &&
                issue.at("details").at("maximum") == hangar.at("maxDecks") &&
                hangar.at("deckCountFits") == false &&
                hangar.at("widthLengthFit").is_null() &&
                has_issue(report, "pending_F19") &&
                has_issue(report, "pending_F21"),
            "The known deck limit must be enforced without settling F19/F21");
      });

  return suite.finish();
}
