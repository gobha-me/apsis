#include "test_support.hpp"

#include <apsis/shipyard/validator.hpp>

#include <array>
#include <set>
#include <tuple>

namespace {
using namespace test;

Json imported(std::string_view stem, const RulesSnapshot &active,
              const SchemaSet &schema_set) {
  const auto result = import_definition(
      read("fixtures/v3/apsis-" + std::string(stem) + "-definition-v3.json"),
      active, schema_set);
  require(result.ok(),
          "Canonical definition must import: " + std::string(stem));
  return result.value->definition;
}

const Json &finding(const Json &report, std::string_view code,
                    std::string_view path = "") {
  const auto &issues = report.at("issues");
  const auto found =
      std::find_if(issues.begin(), issues.end(), [&](const Json &entry) {
        return entry.at("code") == code &&
               (path.empty() || entry.at("path") == path);
      });
  require(found != issues.end(), "Missing finding " + std::string(code) +
                                     " at " + std::string(path) + ": " +
                                     issues.dump());
  return *found;
}

bool has(const Json &report, std::string_view code) {
  const auto &issues = report.at("issues");
  return std::any_of(issues.begin(), issues.end(), [&](const Json &entry) {
    return entry.at("code") == code;
  });
}

void envelope(const Json &report, const RulesSnapshot &active, bool pass) {
  require(report.at("schema") == "apsis-shipyard-result-v1" &&
              report.at("operation") == "validate",
          "Public validation result envelope");
  require(report.at("pass") == pass &&
              report.at("status") == (pass ? "PASS" : "FAIL") &&
              report.at("data").at("structuralPass") == pass,
          "Envelope and structural status must agree: " + report.dump());
  require(report.at("rulesVersion") == active.version() &&
              report.at("rulesSha256") == active.sha256(),
          "Result identifies the active rules snapshot");
  const auto &definition = report.at("data").at("definition");
  require(definition.at("validation").at("ready") ==
                  (pass ? Json(nullptr) : Json(false)) &&
              definition.at("validation").at("jumpReady").is_null(),
          "Structural approval does not guess complete or jump readiness");
  require(definition.at("validation").at("issues") == report.at("issues"),
          "Definition retains the recomputed report");
  require(!has(report, "pending_validation") && !has(report, "pending_docking"),
          "Operations replace loader-only boundary markers");
  const auto key = [](const Json &entry) {
    return std::tuple(entry.at("path").get<std::string>(),
                      entry.at("code").get<std::string>(),
                      entry.at("severity").get<std::string>());
  };
  const auto &issues = report.at("issues");
  require(std::is_sorted(
              issues.begin(), issues.end(),
              [&](const Json &a, const Json &b) { return key(a) < key(b); }),
          "Issues are sorted by path, code, severity");
  for (const auto &entry : issues)
    require(entry.at("message").is_string() &&
                !entry.at("message").get<std::string>().empty(),
            "Every finding has a readable message");
}

Json module(std::uint64_t id, std::string_view type, int x, int y) {
  return {{"id", id}, {"type", type}, {"x", x}, {"y", y}, {"r", 0}};
}

Json two_decks(const Json &skiff) {
  auto blueprint = skiff;
  blueprint["decks"][0]["modules"].push_back(module(7, "ladder", 13, 10));
  blueprint["decks"].push_back(
      {{"name", "Upper deck"},
       {"floors", {{"13,10", true}, {"13,11", true}, {"14,11", true}}},
       {"modules", Json::array({module(8, "ladder", 13, 10),
                                module(9, "shuttleSeats", 14, 11)})}});
  blueprint["nextId"] = 10;
  return blueprint;
}

RulesSnapshot severity_rules(const SchemaSet &schema_set, std::string_view code,
                             std::string_view level) {
  auto source = Json::parse(read("rules/apsis-rules-v4.json"));
  source["validation"]["checks"][code]["severity"] = level;
  auto result = load_rules(source.dump(), schema_set);
  require(result.ok(), "Public rules loader must accept severity change");
  return std::move(*result.value);
}

Json &berth(Json &blueprint) {
  for (auto &deck : blueprint["decks"])
    for (auto &entry : deck["modules"])
      if (entry.at("type") == "shuttleDock")
        return entry;
  throw std::runtime_error("Kestrel must retain its carried-craft berth");
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  // F07/F23 explicitly name the structural outcomes. The historical self-test
  // report is not the oracle, because it never tested Rustbucket.
  struct Canonical {
    const char *stem;
    const char *name;
    bool pass;
  };
  constexpr std::array examples{
      Canonical{"wren", "Wren", true}, Canonical{"skiff", "Skiff", true},
      Canonical{"kestrel-tender", "Kestrel Tender", true},
      Canonical{"wayfarer-anchorage", "Wayfarer Anchorage", true},
      Canonical{"rustbucket", "Rustbucket", false}};
  for (const auto &example : examples) {
    suite.run(
        std::string(example.name) + ": validate after ordinary import", [&] {
          const auto definition = imported(example.stem, active, schema_set);
          require(definition.at("blueprint").at("name") == example.name,
                  "Canonical identity is explicit");
          require(definition.at("validation").at("ready").is_null(),
                  "Import leaves structural readiness undecided");
          const auto report = validate(definition.dump(), active, schema_set);
          envelope(report, active, example.pass);
          require(report.at("data").at("definition").at("blueprint") ==
                      definition.at("blueprint"),
                  "Validation must not repair or rearrange canonical geometry");
          require(report.at("data").at("checks") ==
                      Json({{"crewRoutes", true}, {"approachClearance", true}}),
                  "Default structural checks are enabled");
          require(!has(report, "no_cargo") && !has(report, "power_shortfall") &&
                      !has(report, "no_jump_fuel"),
                  "Performance advisories remain deferred to #51");
          if (!example.pass) {
            const auto &airlock =
                finding(report, "missing_airlock", "/blueprint/decks");
            require(airlock.at("severity") == "error",
                    "Rustbucket missing airlock is an error");
            std::set<std::uint64_t> unreachable;
            std::size_t errors = 0;
            std::size_t unreachable_count = 0;
            for (const auto &entry : report.at("issues")) {
              if (entry.at("severity") == "error")
                ++errors;
              if (entry.at("code") == "unreachable_component") {
                ++unreachable_count;
                require(entry.at("severity") == "error",
                        "Rustbucket crew-access faults remain errors");
                unreachable.insert(
                    entry.at("details").at("moduleId").get<std::uint64_t>());
              }
            }
            require(
                unreachable == std::set<std::uint64_t>{1, 5, 6} &&
                    unreachable_count == 3 && errors == 4,
                "Rustbucket must retain exactly its missing airlock and three "
                "unreachable components (IDs 1, 5, 6)");
          }

          auto forged = definition;
          forged["derived"]["cost"] = -1;
          forged["validation"] = {
              {"ready", !example.pass},
              {"jumpReady", true},
              {"issues", Json::array({{{"code", "forged_cache"},
                                       {"severity", "error"}}})}};
          const auto recomputed = validate(forged.dump(), active, schema_set);
          require(
              recomputed == report,
              "Neither approval nor rejection can come from an imported cache");
        });
  }

  const auto skiff = imported("skiff", active, schema_set).at("blueprint");
  suite.run(
      "missing floor is rejected without synthesizing a replacement", [&] {
        auto source = skiff;
        source["decks"][0]["floors"].erase("12,9");
        const auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        const auto &entry =
            finding(report, "missing_floor", "/blueprint/decks/0/modules/1");
        require(entry.at("details").at("moduleId") == 2 &&
                    entry.at("details").at("rule") ==
                        "/validation/checks/invalid_blueprint",
                "Missing floor retains the module and active severity family");
        require(report.at("data").at("definition").at("blueprint") == source,
                "The invalid draft stays inspectable and unrepaired");
      });
  suite.run(
      "overlap and rotated footprints beyond the grid are structural errors",
      [&] {
        auto source = skiff;
        source["decks"][0]["modules"][1]["x"] = 13;
        source["decks"][0]["modules"][1]["y"] = 8;
        auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        const auto &overlap =
            finding(report, "module_overlap", "/blueprint/decks/0/modules/1");
        require(overlap.at("details").at("moduleId") == 2 &&
                    overlap.at("details").at("otherModuleId") == 1,
                "Overlap identifies both real parts");
        source = skiff;
        source["decks"][0]["modules"][3]["r"] = 1;
        source["decks"][0]["modules"][3]["x"] =
            active.data().at("grid").at("w").get<int>() - 1;
        report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        require(finding(report, "module_out_of_bounds",
                        "/blueprint/decks/0/modules/3")
                        .at("severity") == "error",
                "A legal origin does not admit an out-of-grid part footprint");
      });
  suite.run(
      "required and unique systems and disconnected hull are checked", [&] {
        auto source = skiff;
        source["decks"][0]["modules"].erase(1);
        auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        require(finding(report, "required_system").at("details").at("system") ==
                    "reactor",
                "Missing active required system is identified");
        source = skiff;
        source["decks"][0]["modules"].push_back(module(7, "cockpit", 13, 10));
        source["nextId"] = 8;
        report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        require(finding(report, "unique_system").at("details").at("system") ==
                    "bridge",
                "Duplicate command system is rejected");
        source = skiff;
        source["decks"][0]["floors"]["0,0"] = true;
        report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        require(
            finding(report, "disconnected_hull", "/blueprint/decks/0/floors")
                    .at("details")
                    .at("components") == 2,
            "Four-neighbor hull connectivity excludes an isolated floor");
      });
  suite.run(
      "active severity changes rejection without hiding the finding", [&] {
        auto source = skiff;
        source["decks"][0]["modules"].erase(1);
        const auto warning_rules =
            severity_rules(schema_set, "required_system", "warning");
        const auto report = validate(source.dump(), warning_rules, schema_set);
        envelope(report, warning_rules, true);
        const auto &entry = finding(report, "required_system");
        require(entry.at("severity") == "warning" &&
                    entry.at("details").at("rule") ==
                        "/validation/checks/required_system",
                "Rule-defined warning remains visible and nonblocking");
      });
  suite.run(
      "ordinary paired ladders join adjacent decks and survive deck reorder",
      [&] {
        auto source = two_decks(skiff);
        envelope(validate(source.dump(), active, schema_set), active, true);
        std::swap(source["decks"][0], source["decks"][1]);
        const auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, true);
        require(!has(report, "unpaired_ladder") &&
                    !has(report, "unreachable_component"),
                "Physical ladder pairing follows deck adjacency after reorder");
      });
  suite.run(
      "unpaired ladders cannot create a crew route; crewRoutes is explicit",
      [&] {
        auto source = two_decks(skiff);
        source["decks"][1]["modules"].erase(0);
        const auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        require(
            finding(report, "unpaired_ladder", "/blueprint/decks/0/modules/6")
                    .at("severity") == "warning",
            "Ordinary unpaired ladders use the active warning severity");
        require(finding(report, "unreachable_component",
                        "/blueprint/decks/1/modules/0")
                        .at("details")
                        .at("moduleId") == 9,
                "An upper component requires a real adjacent-deck connection");
        ValidationOptions options;
        options.crewRoutes = false;
        const auto disabled =
            validate(source.dump(), active, schema_set, options);
        envelope(disabled, active, true);
        require(disabled.at("data").at("checks").at("crewRoutes") == false &&
                    !has(disabled, "unreachable_component") &&
                    has(disabled, "unpaired_ladder"),
                "Disabling crew routes is reported and does not disable ladder "
                "geometry");
      });
  suite.run(
      "carried craft retain structural findings at the carrier rule severity",
      [&] {
        auto source =
            imported("kestrel-tender", active, schema_set).at("blueprint");
        auto &craft = berth(source)["craft"];
        craft["decks"][0]["modules"].erase(
            5); // Skiff's airlock; no golden edits.
        auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, true);
        const auto &entry = finding(report, "carried_not_ready");
        require(entry.at("severity") == "warning",
                "The active carried_not_ready default is a warning");
        const auto &nested = entry.at("details").at("issues");
        require(std::any_of(nested.begin(), nested.end(),
                            [](const Json &issue) {
                              return issue.at("code") == "missing_airlock" &&
                                     issue.at("severity") == "error";
                            }),
                "The carried craft's original structural error is retained");
        const auto strict =
            severity_rules(schema_set, "carried_not_ready", "error");
        report = validate(source.dump(), strict, schema_set);
        envelope(report, strict, false);
        require(finding(report, "carried_not_ready").at("severity") == "error",
                "Carrier severity can make the same child fault blocking");
      });
  suite.run(
      "a valid carried craft must fit the carrier's active envelope", [&] {
        auto source =
            imported("kestrel-tender", active, schema_set).at("blueprint");
        berth(source)["craft"] =
            imported("wren", active, schema_set).at("blueprint");
        const auto report = validate(source.dump(), active, schema_set);
        envelope(report, active, false);
        const auto &entry = finding(report, "carried_fit");
        require(
            entry.at("severity") == "error" &&
                entry.at("details").at("widthM") == 20 &&
                entry.at("details").at("lengthM") == 34,
            "A structurally valid Wren exceeds the existing berth envelope");
        require(!has(report, "carried_not_ready"),
                "Carrier fit is distinct from the child's structural approval");
      });
  return suite.finish();
}
