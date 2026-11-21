#include "dock_test_support.hpp"

#include <array>
#include <string>

namespace {
using namespace dock_test;

const Json &finding(const Json &report, std::string_view code,
                    std::string_view path) {
  const auto &issues = report.at("issues");
  const auto found = std::find_if(issues.begin(), issues.end(),
                                  [&](const Json &item) {
    return item.at("code") == code && item.at("path") == path;
  });
  require(found != issues.end(), "Missing " + std::string(code) + " at " +
                                     std::string(path) + ": " + issues.dump());
  require(found->at("severity") == "error" &&
              !found->at("message").get<std::string>().empty(),
          "Structural fault must be a readable error");
  return *found;
}

Json canonical_blueprint(std::string_view stem, const RulesSnapshot &active,
                         const SchemaSet &schema_set) {
  const auto result = import_definition(
      read("fixtures/v3/apsis-" + std::string(stem) + "-definition-v3.json"),
      active, schema_set);
  require(result.ok(), "Canonical blueprint must import");
  return result.value->definition.at("blueprint");
}

Json &berth(Json &blueprint) {
  for (auto &deck : blueprint["decks"])
    for (auto &module : deck["modules"])
      if (module.at("type") == "shuttleDock")
        return module;
  throw std::runtime_error("Kestrel must have its canonical shuttle berth");
}
} // namespace

int main() {
  using namespace dock_test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  suite.run("S07: enclosing the command station removes crew access", [&] {
    auto source = station();
    require(validate(source.dump(), active, schema_set).at("pass") == true,
            "Unenclosed command station is a valid control");
    for (const auto &[x, y] :
         std::array{std::pair{3, 2}, std::pair{2, 3}, std::pair{4, 3},
                    std::pair{3, 4}})
      add_module(source, "microReactor", x, y);
    const auto report = validate(source.dump(), active, schema_set);
    require(report.at("pass") == false,
            "Enclosed bridge must reject structural certification");
    require(finding(report, "command_access", "/blueprint/decks/0/modules/0")
                    .at("details").at("moduleId") == 1,
            "command_access identifies the enclosed command module");
  });

  suite.run("S10: a clear exterior airlock still requires a crew route", [&] {
    auto source = station();
    require(!has_issue(validate(source.dump(), active, schema_set),
                       "missing_airlock"),
            "Connected exterior airlock is a usable control");
    // The airlock's left face and entire outward approach remain empty.
    // Nonwalkable neighbors isolate its walkable cell from the command station.
    for (const auto &[x, y] :
         std::array{std::pair{2, 4}, std::pair{3, 5}, std::pair{2, 6}})
      add_module(source, "microReactor", x, y);
    const auto report = validate(source.dump(), active, schema_set);
    finding(report, "unreachable_component", "/blueprint/decks/0/modules/3");
    finding(report, "missing_airlock", "/blueprint/decks");
    require(!has_issue(report, "blocked_approach",
                       "/blueprint/decks/0/modules/3"),
            "Reachability failure must not depend on airlock clearance");
  });

  suite.run("S12: a reachable airlock needs a clear full approach", [&] {
    auto source = station();
    require(!has_issue(validate(source.dump(), active, schema_set),
                       "missing_airlock"),
            "Reachable clear airlock is a usable control");
    // The immediate face at 1,5 stays exterior; a farther floor blocks approach.
    source["decks"][0]["floors"]["0,5"] = true;
    const auto report = validate(source.dump(), active, schema_set);
    finding(report, "blocked_approach", "/blueprint/decks/0/modules/3");
    finding(report, "missing_airlock", "/blueprint/decks");
    require(!has_issue(report, "unreachable_component",
                       "/blueprint/decks/0/modules/3"),
            "Clearance failure must not depend on airlock crew reachability");
  });

  suite.run("S14: propulsion on a station is an explicit structural fault", [&] {
    auto source = station();
    require(validate(source.dump(), active, schema_set).at("pass") == true,
            "Station without propulsion is a valid control");
    add_module(source, "microEngine", 9, 10);
    const auto report = validate(source.dump(), active, schema_set);
    const auto &issues = report.at("issues");
    const auto propulsion = std::find_if(
        issues.begin(), issues.end(), [](const Json &item) {
          return item.at("code") == "station_propulsion" &&
                 item.at("path") == "/blueprint/decks";
        });
    require(propulsion != issues.end(),
            "A station engine must emit the station_propulsion finding");
    const auto severity = active.data().at("validation").at("checks")
                               .at("station_propulsion").at("severity");
    require(propulsion->at("severity") == severity &&
                propulsion->at("details").at("rule") ==
                    "/validation/checks/station_propulsion" &&
                !propulsion->at("message").get<std::string>().empty(),
            "Station propulsion must retain its active rule severity and identity");
    require(report.at("pass") == (severity == "warning"),
            "Only the active station propulsion severity controls certification");
  });

  for (const bool width : {true, false}) {
    suite.run(width ? "S17: carried width is independently bounded"
                    : "S17: carried length is independently bounded", [&] {
      auto source = canonical_blueprint("kestrel-tender", active, schema_set);
      require(validate(source.dump(), active, schema_set).at("pass") == true,
              "Canonical Skiff fits the carried-craft envelope");
      auto &craft = berth(source)["craft"];
      craft["decks"][0]["floors"][width ? "15,10" : "13,13"] = true;
      const auto report = validate(source.dump(), active, schema_set);
      const auto &entry = finding(report, "carried_fit",
                                 "/blueprint/decks/1/modules/10/craft");
      require(entry.at("details").at("widthM") == (width ? 8 : 6) &&
                  entry.at("details").at("lengthM") == (width ? 10 : 12),
              "Only the selected carried dimension exceeds its limit");
      require(!has_issue(report, "carried_not_ready"),
              "A fit failure must not rely on invalid carried structure");
    });
  }

  suite.run("S18: carried deck cap applies after width and length fit", [&] {
    auto rules_json = active.data();
    for (auto &part : rules_json["parts"])
      if (part.at("id") == "shuttleDock")
        part["carrier"]["maxDecks"] = 1;
    const auto loaded = load_rules(rules_json.dump(), schema_set);
    require(loaded.ok(), "Public loader accepts a one-deck carrier envelope");
    const auto &one_deck_rules = *loaded.value;
    auto source = canonical_blueprint("kestrel-tender", one_deck_rules, schema_set);
    require(validate(source.dump(), one_deck_rules, schema_set).at("pass") == true,
            "One-deck canonical Skiff is an inclusive carrier control");
    auto &craft = berth(source)["craft"];
    add_module(craft, "ladder", 13, 10);
    const auto upper_id = craft.at("nextId").get<std::uint64_t>();
    craft["decks"].push_back(
        {{"name", "Connected upper deck"},
         {"floors", {{"13,10", true}}},
         {"modules", Json::array({{{"id", upper_id}, {"type", "ladder"},
                                    {"x", 13}, {"y", 10}, {"r", 0}}})}});
    craft["nextId"] = upper_id + 1;
    const auto report = validate(source.dump(), one_deck_rules, schema_set);
    const auto &entry = finding(report, "carried_fit",
                               "/blueprint/decks/1/modules/10/craft");
    require(entry.at("details").at("widthM") == 6 &&
                entry.at("details").at("lengthM") == 10 &&
                entry.at("details").at("deckCount") == 2 &&
                entry.at("details").at("limits").at("maxDecks") == 1,
            "Deck overflow is independent of the exact-fitting dimensions");
    require(!has_issue(report, "carried_not_ready"),
            "Paired ladders keep the two-deck carried craft structurally valid");
  });

  suite.run("S21: component footprints obstruct approach without hull floors", [&] {
    auto source = station();
    add_module(source, "microReactor", 0, 6);
    const auto control = validate(source.dump(), active, schema_set);
    require(!has_issue(control, "blocked_approach",
                       "/blueprint/decks/0/modules/3"),
            "Unsupported component outside the airlock runway is a control");
    // An invalid draft is still inspectable. Leaving the blocker unsupported
    // isolates footprint obstruction from the independent floor obstruction.
    source["decks"][0]["modules"].back()["y"] = 5;
    require(!source.at("decks")[0].at("floors").contains("0,5"),
            "No floor may mask the component-footprint check");
    const auto report = validate(source.dump(), active, schema_set);
    finding(report, "missing_floor", "/blueprint/decks/0/modules/5");
    require(finding(report, "blocked_approach", "/blueprint/decks/0/modules/3")
                    .at("details").at("moduleId") == 4,
            "The airlock approach must detect the other component footprint");
  });

  suite.run("S23: floor immediately in front prevents an exterior airlock", [&] {
    auto source = station();
    require(!has_issue(validate(source.dump(), active, schema_set),
                       "missing_airlock"),
            "Outward-facing airlock has an exterior control");
    source["decks"][0]["floors"]["1,5"] = true;
    const auto report = validate(source.dump(), active, schema_set);
    finding(report, "missing_airlock", "/blueprint/decks");
    finding(report, "blocked_approach", "/blueprint/decks/0/modules/3");
  });

  suite.run("S27: rotation two checks the negative-y approach", [&] {
    auto source = visitor();
    auto &engine = source["decks"][0]["modules"].back();
    engine["y"] = 2;
    engine["r"] = 2;
    const auto control = validate(source.dump(), active, schema_set);
    require(control.at("pass") == true &&
                !has_issue(control, "blocked_approach",
                           "/blueprint/decks/0/modules/4"),
            "Upward engine with an empty top runway must be clear");
    source["decks"][0]["floors"]["9,0"] = true;
    const auto report = validate(source.dump(), active, schema_set);
    require(finding(report, "blocked_approach", "/blueprint/decks/0/modules/4")
                    .at("details").at("moduleId") == 5,
            "A floor above the r=2 engine blocks its negative-y approach");
  });

  suite.run("M2: unlinked second-deck floors disconnect from the command", [&] {
    auto source = canonical_blueprint("wren", active, schema_set);
    require(validate(source.dump(), active, schema_set).at("pass") == true,
            "Canonical connected Wren is a valid control");
    source["decks"].push_back(
        {{"name", "Unlinked upper deck"},
         {"floors", {{"0,0", true}, {"0,1", true}}},
         {"modules", Json::array()}});
    const auto report = validate(source.dump(), active, schema_set);
    const auto &entry = finding(report, "disconnected_hull",
                               "/blueprint/decks/1/floors");
    require(report.at("pass") == false &&
                entry.at("details").at("isolatedCells") == 2,
            "A locally connected floor deck still needs a command-linked ladder");
  });

  suite.run("M2: a command module cannot shortcut separate crew routes", [&] {
    auto source = blank("station");
    fill_rectangle(source, 1, 5, 7, 1);
    add_module(source, "cockpit", 4, 5);
    add_module(source, "microReactor", 1, 5);
    add_module(source, "microLifeSupport", 7, 5);
    add_module(source, "airlock", 2, 5, 2);
    auto connected = source;
    fill_rectangle(connected, 3, 6, 3, 1);
    require(validate(connected.dump(), active, schema_set).at("pass") == true,
            "A real walkable bypass connects both sides of the command module");
    const auto report = validate(source.dump(), active, schema_set);
    require(finding(report, "unreachable_component", "/blueprint/decks/0/modules/2")
                    .at("details").at("moduleId") == 3,
            "The right-side life support cannot use the command as a shortcut");
    require(!has_issue(report, "command_access") &&
                !has_issue(report, "unreachable_component",
                           "/blueprint/decks/0/modules/1"),
            "The first left service cell reaches the reactor, not both regions");
  });
  return suite.finish();
}
