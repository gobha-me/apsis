#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <cmath>
#include <functional>

namespace {
using namespace test;

Json fixture_blueprint(std::string_view name, const RulesSnapshot &active,
                       const SchemaSet &schema) {
  const auto imported = import_definition(
      read("fixtures/v3/apsis-" + std::string(name) + "-definition-v3.json"),
      active, schema);
  require(imported.ok(),
          "The scenario fixture must import through the public API");
  return imported.value->definition.at("blueprint");
}

Json &part(Json &data, std::string_view id) {
  for (auto &candidate : data.at("parts")) {
    if (candidate.at("id") == id)
      return candidate;
  }
  throw std::runtime_error("Missing scenario part " + std::string(id));
}

Json findings(const Json &report, std::string_view code) {
  Json found = Json::array();
  for (const auto &item : report.at("issues")) {
    if (item.at("code") == code)
      found.push_back(item);
  }
  return found;
}

const Json &performance(const Json &report) {
  return report.at("data").at("performance");
}

Json scenarios(const SchemaSet &schema, const RulesSnapshot &active) {
  const auto wren = fixture_blueprint("wren", active, schema);
  const auto skiff = fixture_blueprint("skiff", active, schema);
  const auto station = fixture_blueprint("wayfarer-anchorage", active, schema);
  Json reports = Json::object();
  const auto record = [&](const char *name, const Json &blueprint,
                          LoadFractions loads = {}) {
    reports[name] = perf(canonical_json(blueprint), active, schema, loads);
  };
  const auto changed_rules = [&](const char *name, const Json &blueprint,
                                 const std::function<void(Json &)> &change) {
    auto data = active.data();
    change(data);
    const auto loaded = load_rules(canonical_json(data), schema);
    require(loaded.ok(),
            std::string(name) + ": public load_rules must accept the snapshot");
    reports[name] = perf(canonical_json(blueprint), *loaded.value, schema);
  };

  record("wren", wren);
  auto no_drive = wren;
  auto &drive_modules = no_drive["decks"][0]["modules"];
  drive_modules.erase(std::remove_if(drive_modules.begin(), drive_modules.end(),
                                     [](const Json &module) {
                                       return module.at("type") == "drive10";
                                     }),
                      drive_modules.end());
  record("wren_without_drive", no_drive);
  record("wren_one_jump", wren, LoadFractions{.jumpFuel = 0.5});
  record("wren_no_sublight_fuel", wren, LoadFractions{.sublightFuel = 0});
  record("blank_ship", blank_blueprint(active));
  record("skiff", skiff);
  record("station", station);

  auto no_life_support = skiff;
  auto &life_modules = no_life_support["decks"][0]["modules"];
  life_modules.erase(std::remove_if(life_modules.begin(), life_modules.end(),
                                    [](const Json &module) {
                                      return module.at("type") ==
                                             "microLifeSupport";
                                    }),
                     life_modules.end());
  record("skiff_without_life_support", no_life_support);

  auto two_drives = wren;
  auto drive = two_drives.at("decks")[0].at("modules")[2];
  drive["id"] = two_drives.at("nextId");
  two_drives["nextId"] = two_drives.at("nextId").get<int>() + 1;
  two_drives["decks"][0]["modules"].push_back(drive);
  record("wren_two_drives", two_drives);

  changed_rules("epsilon_power", wren, [](Json &data) {
    part(data, "reactor")["generation"] = 10.0999995;
  });
  changed_rules("zero_engine_burn", wren,
                [](Json &data) { part(data, "engine")["burnRate"] = 0; });
  changed_rules("tied_acceleration_bands", wren, [](Json &data) {
    data["performance"]["accelerationBands"] = Json::array(
        {{{"min", 0}, {"label", "a"}}, {{"min", 0}, {"label", "b"}}});
  });
  changed_rules("idle_power_shortfall", wren, [](Json &data) {
    part(data, "bridge")["powerModes"]["idle"] = 40;
  });
  changed_rules("zero_shield_threshold", wren, [](Json &data) {
    data["performance"]["advisoryShieldCount"] = 0;
  });
  changed_rules("zero_weapon_threshold", wren, [](Json &data) {
    data["performance"]["advisoryWeaponCount"] = 0;
  });
  for (const auto minimum : {1, 2, 3}) {
    const auto name = "engine_threshold_" + std::to_string(minimum);
    changed_rules(name.c_str(), wren, [minimum](Json &data) {
      data["advisories"]["many_engines"]["minEngineCount"] = minimum;
    });
  }
  // Wren has 4 t sublight capacity, two 0.25 t jump charges and 61.8 t dry
  // mass.
  for (const auto offset : {0, 1, 2}) {
    const auto name = "fuel_threshold_" + std::to_string(offset);
    changed_rules(name.c_str(), wren, [offset](Json &data) {
      const auto epsilon = data.at("numeric").at("epsilon").get<double>();
      const double gap = offset == 1 ? epsilon / 2 : offset * epsilon;
      data["advisories"]["large_fuel_capacity"]["minFuelToDryMassRatio"] =
          4.5 / 61.8 + gap;
    });
  }
  for (const auto minimum : {5, 6, 7}) {
    const auto name = "module_threshold_" + std::to_string(minimum);
    changed_rules(name.c_str(), skiff, [minimum](Json &data) {
      data["advisories"]["no_cargo"]["minModuleCount"] = minimum;
    });
  }
  auto two_deck_data = active.data();
  two_deck_data["limits"]["minDecks"] = 2;
  const auto two_deck_rules = load_rules(canonical_json(two_deck_data), schema);
  require(two_deck_rules.ok(), "Public rules must accept two minimum decks");
  for (const auto type : {BlankType::ship, BlankType::station}) {
    const auto blueprint = blank_blueprint(*two_deck_rules.value, type);
    const auto *name = type == BlankType::ship ? "two_deck_blank_ship"
                                               : "two_deck_blank_station";
    reports[name] =
        perf(canonical_json(blueprint), *two_deck_rules.value, schema);
  }
  auto mixed_cargo = wren;
  mixed_cargo["decks"][0]["modules"][4]["type"] = "hidden";
  mixed_cargo["loads"]["cargo"] = 1;
  for (const auto capacity : {10, 20}) {
    const auto name = "mixed_cargo_threshold_" + std::to_string(capacity);
    changed_rules(name.c_str(), mixed_cargo, [capacity](Json &data) {
      data["advisories"]["no_cargo"]["maxCargoCapacity"] = capacity;
    });
  }
  return reports;
}

void advisory(const Json &report, std::string_view code, std::string_view path,
              const Json &details) {
  const auto found = findings(report, code);
  require(found.size() == 1,
          "Exactly one " + std::string(code) + " is required");
  require(
      found[0].at("severity") == "warning" && found[0].at("path") == path &&
          found[0].at("details") == details &&
          !found[0].at("message").get<std::string>().empty(),
      "The public advisory must retain its severity, path and exact details");
}
} // namespace

int main(int argc, char **argv) {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto reports = scenarios(schema, active);
  // This mode runs every scenario and writes full public results without
  // executing expectations. Independent exact mutants can compare all output,
  // including scenarios after the first behavioral difference.
  if (argc == 2 && std::string_view(argv[1]) == "--snapshot") {
    std::cout << canonical_json(reports) << '\n';
    return 0;
  }
  Suite suite;

  suite.run("computed performance removes every importer placeholder", [&] {
    const auto &report = reports.at("wren");
    require(report.at("pass") == true, "Wren must remain accepted");
    for (const auto *code :
         {"pending_validation", "pending_performance", "pending_docking"}) {
      require(findings(report, code).empty(),
              "A computed public report must omit " + std::string(code));
    }
    require(
        report.at("issues") ==
            report.at("data").at("definition").at("validation").at("issues"),
        "Definition validation must contain the same filtered findings");
  });

  suite.run("jump readiness requires a drive even with loaded jump fuel", [&] {
    const auto &report = reports.at("wren_without_drive");
    require(report.at("pass") == true &&
                report.at("data").at("ready") == true &&
                performance(report).at("rating") == 0 &&
                performance(report).at("jump").at("currentJumps") == 2 &&
                report.at("data").at("jumpReady") == false,
            "A ready drive-less Wren with two charges cannot jump");
  });

  suite.run("exactly one loaded jump is sufficient", [&] {
    const auto &report = reports.at("wren_one_jump");
    require(
        report.at("pass") == true &&
            performance(report).at("jump").at("currentJumps") == 1 &&
            report.at("data").at("jumpReady") == true &&
            report.at("data")
                    .at("definition")
                    .at("validation")
                    .at("jumpReady") == true,
        "The one-jump equality boundary must be ready in both public copies");
  });

  suite.run("epsilon power margin remains jump ready without shortfall", [&] {
    const auto &report = reports.at("epsilon_power");
    const auto margin =
        performance(report).at("power").at("marginJump").get<double>();
    require(report.at("pass") == true && margin < 0 &&
                std::abs(margin + 0.0000005) < 0.000000000001 &&
                report.at("data").at("jumpReady") == true &&
                findings(report, "power_shortfall").empty(),
            "A half-epsilon power deficit is tolerated for readiness and "
            "advisories");
  });

  suite.run("endurance requires an installed life support system", [&] {
    for (const auto *name : {"blank_ship", "skiff_without_life_support"}) {
      const auto &value = performance(reports.at(name));
      require(value.at("endurance") == 0,
              "Neither suit reserve nor unrelated modules provide endurance");
      for (const auto *scenario : {"current", "empty", "half", "full"})
        require(value.at("scenarios").at(scenario).at("endurance") == 0,
                "Every load scenario retains zero unsupported endurance");
    }
    require(performance(reports.at("skiff")).at("endurance") == 70,
            "An installed compact life support supplies 60 plus 10 days");
  });

  suite.run(
      "station and drive-less craft do not acquire inapplicable fuel warnings",
      [&] {
        const auto &station = reports.at("station");
        require(station.at("pass") == true &&
                    performance(station).at("acceleration") == 0 &&
                    performance(station).at("sublight").at("current") == 0 &&
                    findings(station, "weak_acceleration").empty() &&
                    findings(station, "no_sublight_fuel").empty(),
                "An engine-less station has neither ship acceleration nor "
                "engine fuel advisories");
        require(
            findings(reports.at("skiff"), "no_jump_fuel").empty(),
            "A zero-rating Skiff must not report jump-drive fuel starvation");
      });

  suite.run(
      "zero loaded propellant with installed engines reports its exact warning",
      [&] {
        const auto &report = reports.at("wren_no_sublight_fuel");
        require(report.at("pass") == true &&
                    performance(report).at("sublightFuelMass") == 0,
                "The empty propellant endpoint remains inspectable");
        advisory(report, "no_sublight_fuel", "/performance/sublight/current",
                 {{"rule", "/validation/checks/no_sublight_fuel"}});
      });

  suite.run(
      "structurally rejected duplicate drives still report both costs", [&] {
        const auto &report = reports.at("wren_two_drives");
        const auto &jump = performance(report).at("jump");
        require(report.at("status") == "FAIL" &&
                    report.at("data").at("structuralPass") == false &&
                    !findings(report, "unique_system").empty(),
                "Two drives retain their structural rejection and public "
                "performance");
        require(
            jump.at("driveCost") == 5000 && jump.at("driveCosts").size() == 2 &&
                jump.at("driveCosts")[0].at("cost") == 2500 &&
                jump.at("driveCosts")[1].at("cost") == 2500 &&
                jump.at("driveCosts")[0].at("moduleId") == 3 &&
                jump.at("driveCosts")[1].at("moduleId") == 13,
            "The aggregate cost is the sum of both identified drive entries");
      });

  suite.run("F20 station evaluations expose the exact zero denominator", [&] {
    const auto &report = reports.at("station");
    const auto pending = findings(report, "pending_F20");
    for (const auto *output :
         {"fullBurnSeconds", "currentBurnSeconds", "exhaustVelocity"}) {
      const auto path = "/performance/sublight/" + std::string(output);
      const auto formula = std::string_view(output) == "exhaustVelocity"
                               ? "exhaustVelocity"
                               : "burnSeconds";
      const auto found =
          std::find_if(pending.begin(), pending.end(), [&](const Json &item) {
            return item.at("path") == path;
          });
      require(found != pending.end(),
              "The omitted station metric must have an F20 report");
      require(found->at("details") ==
                  Json{{"finding", "F20"},
                       {"evaluations",
                        Json::array({{{"formula", formula},
                                      {"formulaPath",
                                       "/formulas/" + std::string(formula)},
                                      {"operator", "div"},
                                      {"denominator", 0.0}}})}},
              "The public F20 evaluation retains formula, operator and "
              "denominator zero");
    }
  });

  suite.run("accepted zero-burn engine rules fail F10 instead of claiming "
            "engine-less F20",
            [&] {
              const auto &report = reports.at("zero_engine_burn");
              const auto errors = findings(report, "formula_error");
              require(report.at("status") == "FAIL" &&
                          report.at("data").is_null() && errors.size() == 1 &&
                          findings(report, "pending_F20").empty(),
                      "A fitted zero-burn engine rejects calculation without "
                      "any F20 exemption");
              require(errors[0].at("path") == "/formulas/burnSeconds" &&
                          errors[0].at("severity") == "error" &&
                          errors[0].at("details") ==
                              Json{{"operator", "div"},
                                   {"denominator", 0.0},
                                   {"outputPath",
                                    "/performance/sublight/fullBurnSeconds"}},
                      "F10 identifies the failed burnSeconds division and "
                      "public output path");
            });

  suite.run("the later acceleration band wins an equal minimum", [&] {
    const auto &report = reports.at("tied_acceleration_bands");
    require(report.at("pass") == true &&
                performance(report).at("accelerationRating") == "b",
            "Accepted duplicate-minimum rules choose the later b band");
  });

  suite.run("idle power alone can make a fast ship underpowered", [&] {
    const auto &report = reports.at("idle_power_shortfall");
    const auto &value = performance(report);
    const auto &power = value.at("power");
    require(report.at("pass") == true &&
                value.at("acceleration").get<double>() > 3 &&
                power.at("marginIdle").get<double>() < 0 &&
                power.at("marginCruise").get<double>() > 0 &&
                power.at("marginJump").get<double>() > 0 &&
                value.at("underpowered") == true,
            "The independent idle deficit contributes to underpowered");
    advisory(report, "power_shortfall", "/performance/power/idle",
             {{"phase", "idle"},
              {"supply", power.at("supply")},
              {"demand", power.at("idle")},
              {"rule", "/validation/checks/power_shortfall"}});
  });

  suite.run(
      "zero shields and weapons do not exceed their zero advisory thresholds",
      [&] {
        for (const auto *name :
             {"zero_shield_threshold", "zero_weapon_threshold"}) {
          const auto &report = reports.at(name);
          require(report.at("pass") == true &&
                      findings(report, "excess_defence").empty(),
                  "Each defence threshold uses a strict greater-than boundary");
        }
      });

  suite.run("engine advice includes equality and excludes the next count", [&] {
    for (const auto *name : {"engine_threshold_1", "engine_threshold_2"})
      advisory(reports.at(name), "many_engines", "/performance",
               {{"rule", "/validation/checks/many_engines"}});
    require(findings(reports.at("engine_threshold_3"), "many_engines").empty(),
            "Two engines fall below the next threshold");
  });

  suite.run("fuel ratio advice includes equality and the active epsilon", [&] {
    for (const auto *name : {"fuel_threshold_0", "fuel_threshold_1"}) {
      const auto &report = reports.at(name);
      require(performance(report).at("fuelToDryMassRatio") == 4.5 / 61.8,
              "The policy boundary is the exact demonstrated Wren fuel ratio");
      advisory(report, "large_fuel_capacity", "/performance",
               {{"rule", "/validation/checks/large_fuel_capacity"}});
    }
    require(
        findings(reports.at("fuel_threshold_2"), "large_fuel_capacity").empty(),
        "A gap of two epsilons is below the active advisory threshold");
  });

  suite.run(
      "cargo-less module advice includes exactly six installed modules", [&] {
        for (const auto *name : {"module_threshold_5", "module_threshold_6"})
          advisory(reports.at(name), "no_cargo", "/performance",
                   {{"rule", "/validation/checks/no_cargo"}});
        require(findings(reports.at("module_threshold_7"), "no_cargo").empty(),
                "Six cargo-less modules fall below a seven-module threshold");
      });

  suite.run("blank authoring honors accepted multi-deck minimum rules", [&] {
    for (const auto *name : {"two_deck_blank_ship", "two_deck_blank_station"}) {
      const auto &report = reports.at(name);
      require(!report.at("data").is_null(),
              "A blank source with the active minimum deck count must import");
      const auto &blueprint =
          report.at("data").at("definition").at("blueprint");
      require(blueprint.at("decks").size() == 2,
              "Both public blank types must create exactly two minimum decks");
      for (const auto &deck : blueprint.at("decks")) {
        require(
            deck.at("name") == active.data().at("defaults").at("deckName") &&
                deck.at("floors").empty() && deck.at("modules").empty(),
            "Every minimum deck retains the active empty authoring defaults");
      }
    }
  });

  suite.run("no-cargo advice adds ordinary and concealed capacity", [&] {
    for (const auto *name :
         {"mixed_cargo_threshold_10", "mixed_cargo_threshold_20"}) {
      const auto &report = reports.at(name);
      require(report.at("pass") == true,
              "A Wren with one ordinary and one concealed hold remains valid");
      const auto &totals = report.at("data").at("definition").at("derived");
      require(totals.at("cargo") == 10 && totals.at("hidden") == 10 &&
                  performance(report).at("cargoMass") == 20,
              "The public report must expose both ten-unit capacities and "
              "their loaded sum");
    }
    require(
        findings(reports.at("mixed_cargo_threshold_10"), "no_cargo").empty(),
        "Twenty combined cargo units exceed a ten-unit no-cargo threshold");
    advisory(reports.at("mixed_cargo_threshold_20"), "no_cargo", "/performance",
             {{"rule", "/validation/checks/no_cargo"}});
  });

  return suite.finish();
}
