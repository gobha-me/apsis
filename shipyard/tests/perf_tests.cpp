#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <array>
#include <cmath>
#include <set>

namespace {
using namespace test;

void near(const Json &actual, double expected, const RulesSnapshot &active,
          const std::string &path) {
  require(actual.is_number(), path + " must be a number");
  const auto value = actual.get<double>();
  const auto epsilon = active.data().at("numeric").at("epsilon").get<double>();
  require(std::isfinite(value) && std::isfinite(expected) &&
              std::abs(value - expected) <= epsilon,
          path + ": expected " + Json(expected).dump() + ", got " +
              actual.dump());
}

// F24 approves the preserved prototype metrics as the comparison baseline.
// Read expectations from the original bytes, before ordinary import discards
// caches. F20's historical fallback zeros are explicitly outside that baseline.
void compare_baseline(const Json &actual, const Json &expected,
                      const RulesSnapshot &active, const std::string &path,
                      bool engine_less) {
  static const std::set<std::string> excluded{
      "/sublight/fullBurnSeconds", "/sublight/currentBurnSeconds",
      "/sublight/exhaustVelocity", "/sublight/deltaV"};
  if (expected.is_object()) {
    for (const auto &[key, value] : expected.items()) {
      const auto child_path = path + "/" + key;
      if (engine_less && excluded.contains(child_path)) {
        require(!actual.contains(key), child_path + " must be omitted for F20");
        continue;
      }
      require(actual.contains(key), "Missing baseline metric " + child_path);
      if (key == "maxRangeJumps" || key == "currentJumps")
        require(actual.at(key) == value,
                child_path + " must be an exact count");
      else
        compare_baseline(actual.at(key), value, active, child_path,
                         engine_less);
    }
  } else if (expected.is_number()) {
    near(actual, expected.get<double>(), active, path);
  } else {
    require(actual == expected, "Changed baseline value " + path);
  }
}

std::string severity(const RulesSnapshot &active, std::string_view code) {
  const auto &checks = active.data().at("validation").at("checks");
  return checks.contains(code)
             ? checks.at(code).at("severity").get<std::string>()
             : "warning";
}

std::size_t findings(const Json &report, std::string_view code,
                     std::string_view expected_severity) {
  std::size_t count = 0;
  for (const auto &item : report.at("issues")) {
    if (item.at("code") != code)
      continue;
    ++count;
    require(item.at("severity") == expected_severity &&
                !item.at("message").get<std::string>().empty() &&
                !item.at("path").get<std::string>().empty(),
            "Finding must retain its active severity and structured location");
  }
  return count;
}

struct Canonical {
  const char *name;
  double mass;
  double acceleration;
  double range;
  double cost;
  bool ready;
  bool jump_ready;
};

constexpr std::array canonical{
    Canonical{"wren", 66.3, 3.619909502, 1.508295626, 29080, true, true},
    Canonical{"skiff", 8.75, 2.057142857, 0, 5470, true, false},
    Canonical{"kestrel-tender", 134.45, 1.785050205, 1.115656378, 64330, true,
              true},
    Canonical{"wayfarer-anchorage", 152.45, 0, 0, 72310, true, false},
    Canonical{"rustbucket", 62.6, 0.287539936, 4.792332268, 13530, false,
              false}};

Json check_canonical(const Canonical &example, const RulesSnapshot &active,
                     const SchemaSet &schema) {
  const auto bytes = read("fixtures/v3/apsis-" + std::string(example.name) +
                          "-definition-v3.json");
  const auto historical = Json::parse(bytes);
  const auto imported = import_definition(bytes, active, schema);
  require(imported.ok(), "Canonical design must import normally");
  const auto report =
      perf(canonical_json(imported.value->definition), active, schema);
  require(report.at("schema") == "apsis-shipyard-result-v1" &&
              report.at("operation") == "perf" &&
              report.at("rulesVersion") == active.version() &&
              report.at("rulesSha256") == active.sha256(),
          "Perf envelope must identify the operation and active rules");
  require(report.at("pass") == example.ready &&
              report.at("status") == (example.ready ? "PASS" : "FAIL"),
          "Canonical status must follow freshly evaluated findings");
  const auto &data = report.at("data");
  require(data.is_object(), "Structural faults must retain calculated data");
  const auto &performance = data.at("performance");
  const auto &definition = data.at("definition");
  compare_baseline(performance, historical.at("derived").at("performance"),
                   active, "",
                   std::string_view(example.name) == "wayfarer-anchorage");
  near(performance.at("currentMass"), example.mass, active, "F24 mass");
  near(performance.at("acceleration"), example.acceleration, active,
       "F24 acceleration");
  near(performance.at("range"), example.range, active, "F24 range");
  near(definition.at("derived").at("cost"), example.cost, active,
       "F24 v4 cost");
  near(historical.at("derived").at("cost"),
       example.cost +
           (std::string_view(example.name) == "kestrel-tender" ? 1250 : 0),
       active, "F24 historical cost before the v4 price change");
  near(performance.at("endurance"),
       historical.at("derived").at("endurance").get<double>(), active,
       "endurance");
  require(data.at("ready") == example.ready &&
              data.at("jumpReady") == example.jump_ready &&
              definition.at("validation").at("ready") == example.ready &&
              definition.at("validation").at("jumpReady") == example.jump_ready,
          "Complete readiness and jump readiness must be recomputed");
  require(definition.at("derived").at("performance") == performance,
          "The definition must carry the same calculated performance");
  require(definition.at("blueprint") ==
              imported.value->definition.at("blueprint"),
          "Perf must not repair or rearrange the imported blueprint");

  auto poisoned = imported.value->definition;
  poisoned["derived"] = {{"cost", -999},
                         {"dryMass", 1e30},
                         {"performance", {{"range", 999}, {"thrust", 999}}}};
  poisoned["validation"] = {{"ready", !example.ready},
                            {"jumpReady", !example.jump_ready},
                            {"issues", Json::array()}};
  const auto recomputed = perf(canonical_json(poisoned), active, schema);
  require(recomputed == report,
          "Imported derived and validation caches cannot affect perf: " +
              Json::diff(report, recomputed).dump());

  // Keep the embedded snapshot schema-valid so this tests authority, rather
  // than malformed-input rejection. A changed rule identity may add migration
  // details; it must never change calculated results or structural findings.
  poisoned["rules"] = active.data();
  poisoned["rules"]["performance"]["massReference"] = 123456;
  poisoned["rules"]["formulas"]["range"] = 999;
  for (auto &part : poisoned["rules"]["parts"])
    part["cost"] = 1;
  require(schema.validate(poisoned, "apsis-ship-definition-v4").empty(),
          "The misleading embedded rules must be a valid input snapshot");
  const auto active_report = perf(canonical_json(poisoned), active, schema);
  require(
      active_report.at("pass") == report.at("pass") &&
          active_report.at("issues") == report.at("issues") &&
          active_report.at("rulesSha256") == active.sha256() &&
          active_report.at("data").at("performance") == performance &&
          active_report.at("data").at("definition") == definition,
      "Embedded rules cannot override active prices, formulas or validation");
  require(active_report.at("data").at("migration").at("rulesIdentityChanged") ==
              true,
          "A replaced embedded snapshot must be recorded as a rule identity "
          "change");
  return report;
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

  for (const auto &example : canonical) {
    suite.run(std::string("canonical v4 performance: ") + example.name, [&] {
      const auto report = check_canonical(example, active, schema);
      const auto name = std::string_view(example.name);
      if (name == "rustbucket") {
        require(findings(report, "missing_airlock",
                         severity(active, "missing_airlock")) == 1 &&
                    findings(report, "unreachable_component",
                             severity(active, "unreachable_component")) == 3,
                "Rustbucket's structural errors must remain errors, without "
                "repair");
        for (const auto *code : {"power_shortfall", "no_jump_fuel", "no_cargo"})
          require(findings(report, code, severity(active, code)) == 1,
                  "Rustbucket must retain its performance finding: " +
                      std::string(code));
        const auto &power = report.at("data").at("performance").at("power");
        near(power.at("supply"), 12, active, "Rustbucket power supply");
        near(power.at("jump"), 16.2, active, "Rustbucket jump demand");
        near(power.at("marginJump"), -4.2, active, "Rustbucket jump margin");
        const std::set<int> expected_ids{1, 5, 6};
        std::set<int> actual_ids;
        for (const auto &item : report.at("issues")) {
          if (item.at("code") == "unreachable_component")
            actual_ids.insert(item.at("details").at("moduleId").get<int>());
          if (item.at("code") == "power_shortfall")
            require(item.at("details").at("phase") == "jump" &&
                        item.at("path") == "/performance/power/jump",
                    "Rustbucket power shortfall must identify the jump phase");
        }
        require(actual_ids == expected_ids,
                "Rustbucket's three unreachable modules must be preserved");
      } else if (name == "skiff" || name == "kestrel-tender") {
        require(findings(report, "no_cargo", severity(active, "no_cargo")) == 1,
                "Skiff retains its no_cargo advisory, including when carried");
        if (name == "kestrel-tender") {
          const auto &craft =
              report.at("data").at("performance").at("carriedCraft");
          require(craft.size() == 1 && craft[0].at("name") == "Skiff" &&
                      craft[0].at("moduleId") == 22,
                  "Kestrel must report its carried Skiff");
          const auto skiff =
              Json::parse(read("fixtures/v3/apsis-skiff-definition-v3.json"));
          compare_baseline(craft[0].at("performance"),
                           skiff.at("derived").at("performance"), active, "",
                           false);
          for (const auto &item : report.at("issues"))
            if (item.at("code") == "no_cargo")
              require(
                  item.at("path") ==
                          "/performance/carriedCraft/0/performance" &&
                      item.at("details").contains("childBlueprintPath"),
                  "F24's extra nested advisory must retain its child location");
        }
      }
    });
  }

  suite.run("five explicit v4 jump drive prices flow through public perf", [&] {
    struct Price {
      const char *id;
      double rating;
      double cost;
    };
    constexpr std::array prices{
        Price{"drive05", 0.5, 1500}, Price{"drive10", 1.0, 2500},
        Price{"drive15", 1.5, 3750}, Price{"drive20", 2.0, 5000},
        Price{"drive30", 3.0, 7500}};
    for (const auto &price : prices) {
      auto blueprint = blank_blueprint(active);
      blueprint["decks"][0]["modules"].push_back(
          {{"id", 1}, {"type", price.id}, {"x", 0}, {"y", 0}, {"r", 0}});
      blueprint["nextId"] = 2;
      const auto report = perf(canonical_json(blueprint), active, schema);
      require(report.at("data").is_object(),
              "An inspectable drive design must calculate");
      const auto &performance = report.at("data").at("performance");
      const auto &jump = performance.at("jump");
      require(jump.at("driveCosts").size() == 1,
              "Report the actual fitted drive");
      const auto &drive = jump.at("driveCosts")[0];
      require(drive.at("part") == price.id && drive.at("moduleId") == 1 &&
                  drive.at("deck") == 0,
              "Drive cost must identify the fitted part");
      near(drive.at("rating"), price.rating, active, "drive rating");
      near(drive.at("cost"), price.cost, active, "explicit drive price");
      near(jump.at("driveCost"), price.cost, active, "total drive cost");
      near(performance.at("rating"), price.rating, active,
           "performance rating");
      near(report.at("data").at("definition").at("derived").at("cost"),
           price.cost, active, "drive-only design cost");
    }
  });

  suite.run("scenario mass rounding uses active precision before ratios", [&] {
    auto blueprint =
        Json::parse(read("fixtures/v3/apsis-wren-definition-v3.json"))
            .at("blueprint");
    const LoadFractions loads{.cargo = 0.123456789,
                              .jumpFuel = 0.345678901,
                              .sublightFuel = 0.456789123};
    // Direct mass: 61.8 + 20*cargo + 0.5*jumpFuel + 4*sublightFuel.
    const double unrounded = 61.8 + 20 * *loads.cargo + 0.5 * *loads.jumpFuel +
                             4 * *loads.sublightFuel;
    for (const auto precision :
         {active.data().at("numeric").at("precision").get<int>(), 3}) {
      auto rule_data = active.data();
      rule_data["numeric"]["precision"] = precision;
      const auto loaded = load_rules(canonical_json(rule_data), schema);
      require(loaded.ok(), "The alternate numeric precision must load");
      const auto &policy = *loaded.value;
      const auto report =
          perf(canonical_json(blueprint), policy, schema, loads);
      require(report.at("pass") == true, "Rounding example must remain ready");
      const auto &current =
          report.at("data").at("performance").at("scenarios").at("current");
      const auto scale = std::pow(10.0, precision);
      const double expected_mass = std::floor(unrounded * scale + 0.5) / scale;
      require(current.at("mass").get<double>() == expected_mass,
              "Scenario mass must round at the active precision boundary");
      near(current.at("acceleration"), 240 / expected_mass, policy,
           "rounded acceleration");
      near(current.at("range"), 100 / expected_mass, policy, "rounded range");
    }
  });

  suite.run("the byte limit measures the caller's blueprint", [&] {
    const auto imported = import_definition(
        read("fixtures/v3/apsis-wren-definition-v3.json"), active, schema);
    require(imported.ok(), "The Wren fixture must import");
    const auto &blueprint = imported.value->definition.at("blueprint");
    const auto source = canonical_json(blueprint);
    const auto report =
        perf(source, active, schema, LoadFractions{.jumpFuel = 0.375},
             InputLimits{.maxBytes = source.size()});
    require(report.at("pass") == true,
            "Perf must accept input that exactly fits the caller's byte cap");
    const auto &data = report.at("data");
    const auto &definition = data.at("definition");
    auto expected = blueprint;
    expected["loads"]["jumpFuel"] = 0.375;
    require(definition.at("blueprint") == expected &&
                canonical_json(expected).size() > source.size(),
            "The normalized blueprint contains the larger fractional load");
    near(definition.at("derived").at("currentMass"), 65.9875, active,
         "imported current mass");
    near(data.at("performance").at("currentMass"), 65.9875, active,
         "calculated current mass");

    const auto short_cap =
        perf(source, active, schema, LoadFractions{.jumpFuel = 0.375},
             InputLimits{.maxBytes = source.size() - 1});
    require(short_cap.at("data").is_null() &&
                short_cap.at("issues").at(0).at("code") == "resource_limit",
            "A one-byte-short limit rejects the caller's source");
  });

  suite.run("the replacement fuel load drives direct formulas", [&] {
    auto changed = active.data();
    // Wren holds two charges, so this formula rejects its quarter-load source.
    changed["formulas"]["jumpFuelMass"] =
        Json::array({"div", "jumpFuelCharges",
                     Json::array({"sub", "jumpFuelCharges", 0.5})});
    const auto loaded = load_rules(canonical_json(changed), schema);
    require(loaded.ok(), "The rules loader accepts the jump mass expression");
    const auto &fuel_rules = *loaded.value;
    auto historical =
        Json::parse(read("fixtures/v3/apsis-wren-definition-v3.json"));
    historical["blueprint"]["loads"]["jumpFuel"] = 0.25;
    const auto source = canonical_json(historical);
    const auto rejected = import_definition(source, fuel_rules, schema);
    require(!rejected.ok() && issue(rejected.issues, "formula_error",
                                    "/formulas/jumpFuelMass"),
            "Import reports the undefined formula at the source load");
    const auto report =
        perf(source, fuel_rules, schema, LoadFractions{.jumpFuel = 0.75});
    require(report.at("pass") == true,
            "The override reaches the formula before the source load does");
    const auto &data = report.at("data");
    near(data.at("definition").at("derived").at("currentMass"), 67.3,
         fuel_rules, "direct current mass with the replacement fuel");
    near(data.at("performance").at("currentMass"), 67.3, fuel_rules,
         "performance current mass with the replacement fuel");
    historical["blueprint"]["loads"]["jumpFuel"] = 0.75;
    const auto adjusted = perf(canonical_json(historical), fuel_rules, schema);
    const auto &migration = data.at("migration");
    require(migration.at("sourceVersion") == 3 &&
                migration.at("rulesIdentityChanged") == true &&
                migration == adjusted.at("data").at("migration"),
            "A public override retains the v3 migration diagnostics");
  });

  return suite.finish();
}
