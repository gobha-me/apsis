#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {
using namespace test;

std::string fixture(std::string_view name) {
  return read("fixtures/v3/apsis-" + std::string(name) + "-definition-v3.json");
}

void near(const Json &actual, double expected, const RulesSnapshot &active,
          std::string_view label) {
  require(actual.is_number() && std::isfinite(actual.get<double>()) &&
              std::abs(actual.get<double>() - expected) <=
                  active.data().at("numeric").at("epsilon").get<double>(),
          std::string(label) + ": expected " + std::to_string(expected) +
              ", got " + actual.dump());
}

const Json &finding(const Json &report, std::string_view code) {
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code)
      return item;
  throw std::runtime_error("Missing issue " + std::string(code) + " in " +
                           report.dump());
}

RulesSnapshot replacement(const Json &data, const SchemaSet &schema) {
  auto loaded = load_rules(canonical_json(data), schema);
  require(loaded.ok(), "Replacement rules must load before perf is exercised");
  return std::move(*loaded.value);
}

void rejected_formula(const Json &report) {
  require(report.at("status") == "FAIL" && report.at("pass") == false &&
              report.at("data").is_null(),
          "F10 calculation failures reject with null data");
  const auto &item = finding(report, "formula_error");
  require(item.at("severity") == "error" &&
              item.at("path").get<std::string>().starts_with("/formulas/") &&
              !item.at("message").get<std::string>().empty(),
          "Formula failures identify the active rule and explain the error");
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto wren = fixture("wren");
  Suite suite;

  suite.run("Wren reports four independent load scenarios", [&] {
    const auto report = perf(wren, active, schema);
    require(report.at("pass") == true, "Wren must evaluate successfully");
    const auto &performance = report.at("data").at("performance");
    const auto &scenarios = performance.at("scenarios");
    require(scenarios.size() == 4, "Current, empty, half and full are present");
    // Approved v4 Wren: 61.8 t dry, 20 cargo, two jump charges (0.25 t
    // each), 4 t sublight fuel, 240 kN thrust and a 1 J drive.
    struct Scenario {
      const char *name;
      double mass;
      double cargo;
      double jump;
      double sublight;
      double charges;
    };
    for (const auto &expected :
         std::array{Scenario{"current", 66.3, 0, 0.5, 4, 2},
                    Scenario{"empty", 61.8, 0, 0, 0, 0},
                    Scenario{"half", 74.05, 10, 0.25, 2, 1},
                    Scenario{"full", 86.3, 20, 0.5, 4, 2}}) {
      const auto &value = scenarios.at(expected.name);
      near(value.at("mass"), expected.mass, active, expected.name);
      near(value.at("cargoMass"), expected.cargo, active, "cargo mass");
      near(value.at("jumpFuelMass"), expected.jump, active, "jump fuel mass");
      near(value.at("sublightFuelMass"), expected.sublight, active,
           "sublight mass");
      near(value.at("jumpCharges"), expected.charges, active, "jump charges");
      require(value.at("jumpCount") == expected.charges,
              "Jump counts compare exactly, without epsilon");
      near(value.at("acceleration"), 240 / expected.mass, active,
           "scenario acceleration");
      near(value.at("range"), 100 / expected.mass, active, "scenario range");
      require(value.at("power") == performance.at("power"),
              "Loading does not change the installed power modes");
    }
    require(performance.at("currentMass") == scenarios.at("current").at("mass"),
            "The top-level current mass identifies the current scenario");
  });

  suite.run(
      "optional fractions retain unspecified loads and recompute totals", [&] {
        const auto baseline = perf(wren, active, schema);
        const auto report =
            perf(wren, active, schema, LoadFractions{.cargo = 0.25});
        require(report.at("pass") == true,
                "A valid cargo override is accepted");
        const auto &performance = report.at("data").at("performance");
        require(performance.at("loads") ==
                    Json{{"cargo", 0.25}, {"jumpFuel", 1}, {"sublightFuel", 1}},
                "An omitted override retains that blueprint load");
        near(performance.at("currentMass"), 71.3, active, "overridden mass");
        near(performance.at("cargoMass"), 5, active, "overridden cargo");
        near(report.at("data").at("definition").at("derived").at("currentMass"),
             71.3, active, "recomputed definition mass");
        for (const auto *name : {"empty", "half", "full"})
          require(
              performance.at("scenarios").at(name) == baseline.at("data")
                                                          .at("performance")
                                                          .at("scenarios")
                                                          .at(name),
              "A current-load override leaves the other scenarios independent");

        const auto mixed =
            perf(wren, active, schema,
                 LoadFractions{
                     .cargo = 0.25, .jumpFuel = 0.5, .sublightFuel = 0.25});
        const auto &mixed_perf = mixed.at("data").at("performance");
        near(mixed_perf.at("currentMass"), 68.05, active, "mixed load mass");
        require(mixed_perf.at("jump").at("currentJumps") == 1,
                "The mixed load has exactly one current jump");
        near(mixed_perf.at("sublight").at("currentBurnSeconds"), 125, active,
             "mixed load burn duration");
        near(mixed_perf.at("sublight").at("fullBurnSeconds"), 500, active,
             "full burn duration remains capacity-based");
      });

  suite.run("scenario fractions come from the active rules", [&] {
    auto data = active.data();
    data["performance"]["scenarioFractions"] =
        Json{{"empty", 0.1}, {"half", 0.4}, {"full", 0.9}};
    const auto changed = replacement(data, schema);
    const auto report = perf(wren, changed, schema);
    require(report.at("pass") == true, "Valid scenario policies are accepted");
    const auto &scenarios = report.at("data").at("performance").at("scenarios");
    near(scenarios.at("empty").at("mass"), 64.25, changed, "active empty");
    near(scenarios.at("half").at("mass"), 71.6, changed, "active half");
    near(scenarios.at("full").at("mass"), 83.85, changed, "active full");
    near(scenarios.at("current").at("mass"), 66.3, changed,
         "current retains blueprint loads");
  });

  suite.run(
      "load overrides enforce exact bounds and reject nonfinite values", [&] {
        for (const double value :
             {-0.0000001, 1.0000001, std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::quiet_NaN()}) {
          for (int field = 0; field < 3; ++field) {
            LoadFractions loads;
            if (field == 0)
              loads.cargo = value;
            else if (field == 1)
              loads.jumpFuel = value;
            else
              loads.sublightFuel = value;
            const auto report = perf(wren, active, schema, loads);
            require(report.at("pass") == false && report.at("data").is_null(),
                    "Invalid loads reject before calculation; epsilon is not "
                    "an input tolerance");
            const auto &item = finding(report, "invalid_load");
            const auto *key = field == 0   ? "cargo"
                              : field == 1 ? "jumpFuel"
                                           : "sublightFuel";
            require(item.at("severity") == "error" &&
                        item.at("path") == std::string("/loads/") + key,
                    "Invalid loads identify the offending public option");
          }
        }
        for (const double value : {0.0, 1.0})
          require(perf(wren, active, schema,
                       LoadFractions{.cargo = value,
                                     .jumpFuel = value,
                                     .sublightFuel = value})
                          .at("pass") == true,
                  "Both fraction endpoints are accepted");
      });

  suite.run("readiness and theoretical range remain distinct", [&] {
    const auto fueled = perf(wren, active, schema);
    require(fueled.at("data").at("ready") == true &&
                fueled.at("data").at("jumpReady") == true,
            "A valid powered and fueled Wren is ready to jump");
    const auto unfueled =
        perf(wren, active, schema, LoadFractions{.jumpFuel = 0});
    const auto &data = unfueled.at("data");
    require(unfueled.at("pass") == true && data.at("ready") == true &&
                data.at("jumpReady") == false &&
                data.at("performance").at("range").get<double>() > 0,
            "A warning about no loaded jump fuel preserves readiness and "
            "theoretical range");
    require(finding(unfueled, "no_jump_fuel").at("severity") == "warning",
            "Shipped perf severity is nonblocking warning");
    const auto no_drive = perf(fixture("skiff"), active, schema);
    require(no_drive.at("data").at("ready") == true &&
                no_drive.at("data").at("jumpReady") == false,
            "A ready local craft without a drive cannot jump");
    near(no_drive.at("data").at("performance").at("range"), 0, active,
         "no-drive range is a legitimate zero");
  });

  suite.run("active perf error severity fails an otherwise valid ship", [&] {
    auto data = active.data();
    data["validation"]["checks"]["no_jump_fuel"]["severity"] = "error";
    const auto changed = replacement(data, schema);
    const auto report =
        perf(wren, changed, schema, LoadFractions{.jumpFuel = 0});
    require(report.at("pass") == false && report.at("status") == "FAIL" &&
                report.at("data").at("structuralPass") == true &&
                report.at("data").at("ready") == false &&
                report.at("data").at("jumpReady") == false,
            "Rule-defined perf errors fail readiness without reclassifying "
            "structure");
    const auto &item = finding(report, "no_jump_fuel");
    require(item.at("severity") == "error" &&
                item.at("details").at("rule") ==
                    "/validation/checks/no_jump_fuel",
            "The finding identifies the active severity rule");
  });

  suite.run("non-F20 division and invalid logarithm reject clearly", [&] {
    for (const auto &formula :
         {Json::array({"div", "thrust", 0}), Json::array({"ln", -1})}) {
      auto data = active.data();
      data["formulas"]["acceleration"] = formula;
      const auto changed = replacement(data, schema);
      rejected_formula(perf(wren, changed, schema));
    }
  });

  suite.run("an F20 operand does not hide an independent formula failure", [&] {
    auto data = active.data();
    data["formulas"]["acceleration"] =
        Json::array({"add", Json::array({"div", "thrust", "mass"}),
                     Json::array({"div", 1, 0})});
    const auto changed = replacement(data, schema);
    const auto report =
        perf(canonical_json(blank_blueprint(changed)), changed, schema);
    rejected_formula(report);
    require(finding(report, "formula_error").at("path") ==
                "/formulas/acceleration/2",
            "The independent non-F20 zero divisor remains an error");
  });

  suite.run("division checks a known zero divisor after an F20 operand", [&] {
    auto data = active.data();
    const auto inner = Json::array({"div", "thrust", "mass"});
    data["formulas"]["acceleration"] = Json::array({"div", inner, 0});
    const auto changed = replacement(data, schema);
    const auto report =
        perf(canonical_json(blank_blueprint(changed)), changed, schema);
    rejected_formula(report);
    const auto &item = finding(report, "formula_error");
    require(item.at("path") == "/formulas/acceleration" &&
                item.at("details").at("operator") == "div" &&
                item.at("details").at("denominator") == 0,
            "The parent division identifies its independent zero divisor");

    data["formulas"]["acceleration"] = Json::array({"div", 0, inner});
    const auto pending_rules = replacement(data, schema);
    const auto pending = perf(canonical_json(blank_blueprint(pending_rules)),
                              pending_rules, schema);
    require(pending.at("data").is_object() &&
                !pending.at("data").at("performance").contains("acceleration"),
            "A zero numerator and unavailable divisor leave acceleration "
            "pending");
  });

  suite.run(
      "parent load overrides preserve carried craft loads and diagnostics",
      [&] {
        const auto report =
            perf(fixture("kestrel-tender"), active, schema,
                 LoadFractions{.cargo = 0, .jumpFuel = 0, .sublightFuel = 0});
        require(report.at("pass") == true, "Valid carrier remains inspectable");
        const auto &performance = report.at("data").at("performance");
        const auto &carried = performance.at("carriedCraft");
        require(carried.size() == 1 && carried[0].at("moduleId") == 22 &&
                    carried[0].at("name") == "Skiff",
                "Recursive performance identifies the carried craft");
        near(performance.at("currentMass"), 129.45, active,
             "unloaded parent retains child load mass");
        near(performance.at("carriedLoadMass"), 0.5, active,
             "parent override does not empty child fuel");
        const auto &child = carried[0].at("performance");
        near(child.at("currentMass"), 8.75, active, "child current mass");
        near(child.at("sublight").at("currentBurnSeconds"), 500, active,
             "child fuel is still full");
        const auto &item = finding(report, "no_cargo");
        require(item.at("severity") == "warning" &&
                    item.at("path") ==
                        "/performance/carriedCraft/0/performance" &&
                    item.at("details").at("childBlueprintPath") ==
                        "/blueprint/decks/1/modules/10/craft",
                "Nested diagnostics retain severity and blueprint/output "
                "locations");
      });

  suite.run("a child-specific F10 failure rejects the containing design", [&] {
    auto data = active.data();
    // Kestrel's 0.008 t/s engines have a valid denominator; Skiff's
    // 0.001 t/s engine makes this active formula undefined only in the child.
    data["formulas"]["burnSeconds"] =
        Json::array({"div", "fuel", Json::array({"sub", "burnRate", 0.001})});
    const auto changed = replacement(data, schema);
    const auto report = perf(fixture("kestrel-tender"), changed, schema);
    rejected_formula(report);
    const auto &item = finding(report, "formula_error");
    require(item.at("details").at("outputPath") ==
                    "/performance/carriedCraft/0/performance/sublight/"
                    "fullBurnSeconds" &&
                item.at("details").at("childBlueprintPath") ==
                    "/blueprint/decks/1/modules/10/craft",
            "A child formula error identifies the child rather than silently "
            "succeeding");
  });

  suite.run("a migrated carried craft stays within the source byte limit", [&] {
    const auto deck = Json{
        {"name", "D"}, {"floors", Json::object()}, {"modules", Json::array()}};
    const auto craft = Json{{"schema", "apsis-shipyard-v3"},
                            {"name", "C"},
                            {"budget", 0},
                            {"decks", Json::array({deck})},
                            {"nextId", 1}};
    auto carrier = craft;
    carrier["name"] = "P";
    carrier["nextId"] = 2;
    carrier["decks"][0]["modules"].push_back({{"id", 1},
                                              {"type", "shuttleDock"},
                                              {"x", 0},
                                              {"y", 0},
                                              {"r", 0},
                                              {"craft", craft}});
    const auto source = canonical_json(carrier);
    require(source.size() == 278, "The original v3 carrier fits 278 bytes");
    const InputLimits limits{.maxBytes = source.size()};
    const auto imported = import_definition(source, active, schema, limits);
    require(imported.ok(), "Import accepts the exact caller-supplied byte cap");
    const auto &normalized = imported.value->definition.at("blueprint");
    require(canonical_json(
                normalized.at("decks").at(0).at("modules").at(0).at("craft"))
                    .size() > limits.maxBytes,
            "Normalizing the child expands it beyond the original byte cap");

    const auto ordinary = perf(source, active, schema);
    require(
        ordinary.at("data").is_object() &&
            ordinary.at("data").at("performance").at("carriedCraft").size() ==
                1,
        "The carrier and child are inspectable with ordinary limits");
    require(perf(source, active, schema, {}, limits) == ordinary,
            "The source cap must not be reapplied to normalized craft");
    const auto over_limit = perf(source, active, schema, {},
                                 InputLimits{.maxBytes = source.size() - 1});
    require(over_limit.at("data").is_null() &&
                finding(over_limit, "resource_limit").at("severity") == "error",
            "The source itself remains subject to the byte cap");
  });

  suite.run("a carried direct-total failure identifies the craft", [&] {
    auto data = active.data();
    data["formulas"]["loadedMass"] =
        Json::array({"div", "dryMass", Json::array({"sub", "dryMass", 8.25})});
    const auto changed = replacement(data, schema);
    const auto source = fixture("kestrel-tender");
    const auto imported = import_definition(source, changed, schema);
    require(!imported.ok(),
            "Skiff's 8.25 t dry mass makes loadedMass undefined");
    const auto report = perf(source, changed, schema);
    rejected_formula(report);
    const auto &item = finding(report, "formula_error");
    require(item.at("path") == "/formulas/loadedMass" &&
                item.at("details").at("operator") == "div" &&
                item.at("details").at("denominator") == 0 &&
                item.at("details").value("childBlueprintPath", "") ==
                    "/blueprint/decks/1/modules/10/craft",
            "The direct-total error retains both formula and craft locations");
    require(item == finding(Json{{"issues", imported.issues}}, "formula_error"),
            "Standalone import exposes the same carried formula error");
  });

  return suite.finish();
}
