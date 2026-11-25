#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>
#include <apsis/shipyard/validator.hpp>

#include <set>

namespace {
using namespace test;

bool has_issue(const Json &report, std::string_view code) {
  return std::any_of(report.at("issues").begin(), report.at("issues").end(),
                     [&](const Json &item) { return item.at("code") == code; });
}

const Json &pending_at(const Json &report, std::string_view path) {
  for (const auto &item : report.at("issues")) {
    if (item.at("code") == "pending_F20" && item.at("path") == path) {
      require(
          item.at("severity") == "warning" &&
              !item.at("message").get<std::string>().empty() &&
              item.at("details").at("finding") == "F20" &&
              !item.at("details").at("evaluations").empty(),
          "An F20 omission needs a structured, nonblocking pending issue: " +
              std::string(path));
      return item;
    }
  }
  throw std::runtime_error("Missing pending_F20 at " + std::string(path) +
                           ": " + report.at("issues").dump());
}

void import_identities(const Json &report, bool zero_mass) {
  // DESIGN names both the undefined logarithm and its zero endMass operand.
  // These are pending cases, not permission to choose their eventual values.
  const auto &pending = pending_at(report, "/blueprint");
  require(pending.at("details").at("issue") == 51,
          "The importer pending report must identify issue #51");
  std::set<std::string> actual;
  for (const auto &evaluation : pending.at("details").at("evaluations")) {
    const auto identity = evaluation.at("formula").get<std::string>() + "/" +
                          evaluation.at("operand").get<std::string>() + "/" +
                          evaluation.value("scenario", std::string());
    require(actual.insert(identity).second,
            "The excluded evaluation must not be duplicated: " + identity);
  }
  std::set<std::string> expected{"burnSeconds/burnRate/full",
                                 "burnSeconds/burnRate/current",
                                 "exhaustVelocity/burnRate/"};
  if (zero_mass) {
    for (const auto *identity :
         {"acceleration/mass/", "range/mass/", "deltaV/endMass/",
          "deltaV/ln(0)/", "fuelToDryMassRatio/dryMass/"})
      expected.insert(identity);
  }
  require(actual == expected,
          "Pending identities must match every applicable DESIGN F20 case");
}

void no_excluded_keys(const Json &value, bool zero_mass) {
  std::set<std::string> excluded{"burnSeconds", "fullBurnSeconds",
                                 "currentBurnSeconds", "exhaustVelocity",
                                 "deltaV"};
  if (zero_mass) {
    excluded.insert("acceleration");
    excluded.insert("range");
    excluded.insert("fuelToDryMassRatio");
  }
  if (value.is_object()) {
    for (const auto &[key, child] : value.items()) {
      require(!excluded.contains(key),
              "Excluded metrics must be absent, including null, numeric or "
              "string stand-ins: " +
                  key);
      no_excluded_keys(child, zero_mass);
    }
  } else if (value.is_array()) {
    for (const auto &child : value)
      no_excluded_keys(child, zero_mass);
  }
}

void output_pending(const Json &report, const std::string &path,
                    std::string_view formula) {
  const auto &pending = pending_at(report, path);
  require(!report.at("data")
               .at("performance")
               .contains(Json::json_pointer(
                   path.substr(std::string("/performance").size()))),
          "The pending output path must name an omitted metric: " + path);
  for (const auto &evaluation : pending.at("details").at("evaluations")) {
    require(evaluation.at("formula") == formula &&
                evaluation.at("formulaPath")
                    .get<std::string>()
                    .starts_with("/formulas/" + std::string(formula)),
            "A pending formula must identify its active rules path: " + path);
  }
}

void check_f20(const Json &report, bool zero_mass) {
  require(!report.at("data").is_null(),
          "F20 exclusions must retain calculated data, not reject the design");
  require(!has_issue(report, "formula_error") &&
              !has_issue(report, "nonfinite_total") &&
              !has_issue(report, "nonfinite_performance"),
          "Pending F20 must not become a formula/nonfinite rejection");
  require(std::none_of(report.at("issues").begin(), report.at("issues").end(),
                       [](const Json &item) {
                         return item.at("code") == "pending_F20" &&
                                item.at("path") == "/blueprint";
                       }),
          "Perf replaces provisional F20 with output-specific diagnostics");
  const auto &data = report.at("data");
  const auto &performance = data.at("performance");
  require(performance == data.at("definition").at("derived").at("performance"),
          "The definition and top-level performance must share omissions");
  no_excluded_keys(performance, zero_mass);
  no_excluded_keys(data.at("definition").at("derived"), zero_mass);
  output_pending(report, "/performance/sublight/fullBurnSeconds",
                 "burnSeconds");
  output_pending(report, "/performance/sublight/currentBurnSeconds",
                 "burnSeconds");
  output_pending(report, "/performance/sublight/exhaustVelocity",
                 "exhaustVelocity");
  output_pending(report, "/performance/sublight/deltaV", "deltaV");
  const auto &delta = pending_at(report, "/performance/sublight/deltaV");
  require(
      std::any_of(delta.at("details").at("evaluations").begin(),
                  delta.at("details").at("evaluations").end(),
                  [](const Json &evaluation) {
                    return evaluation.value("dependency", std::string()) ==
                           "exhaustVelocity";
                  }),
      "Omitted deltaV must identify its excluded exhaustVelocity dependency");
  if (zero_mass) {
    for (const auto *scenario : {"current", "empty", "half", "full"}) {
      const auto path = "/performance/scenarios/" + std::string(scenario);
      require(performance.at("scenarios").at(scenario).at("mass") == 0,
              "A public blank design must actually have zero scenario mass");
      output_pending(report, path + "/acceleration", "acceleration");
      output_pending(report, path + "/range", "range");
    }
    output_pending(report, "/performance/fuelToDryMassRatio",
                   "fuelToDryMassRatio");
  }
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

  suite.run("holds stay pending at the exact source byte cap", [&] {
    // Parsing expands both exponent spellings when the tree is serialized.
    constexpr std::string_view source =
        R"({"schema":"apsis-shipyard-v3","name":"Hold","budget":1e8,)"
        R"("decks":[{"name":"D","floors":{},"modules":[]}],)"
        R"("nextId":1e9,"holds":[]})";
    const auto unrestricted = perf(source, active, schema);
    const auto &issues = unrestricted.at("issues");
    require(unrestricted.at("pass") == false &&
                unrestricted.at("data").is_null() && issues.size() == 7,
            "A valid hold request returns exactly the seven pending findings");
    for (const auto number : {11, 12, 13, 14, 15, 16, 17}) {
      const auto finding = "F" + std::to_string(number);
      require(std::any_of(issues.begin(), issues.end(), [&](const Json &item) {
                return item.at("code") == "pending_" + finding &&
                       item.at("path") == "/holds" &&
                       item.at("severity") == "error";
              }),
              "The hold request must report " + finding);
    }
    require(perf(source, active, schema, {},
                 InputLimits{.maxBytes = source.size()}) == unrestricted,
            "An exact byte cap must apply to the source and retain pending holds");
    require(has_issue(perf(source, active, schema, {},
                           InputLimits{.maxBytes = source.size() - 1}),
                      "resource_limit"),
            "A source one byte above the cap must still be rejected");
  });

  suite.run(
      "public blank sources use active defaults and remain inspectable", [&] {
        for (const auto type : {BlankType::ship, BlankType::station}) {
          const auto blueprint = blank_blueprint(active, type);
          const auto &defaults = active.data().at("defaults");
          const bool station = type == BlankType::station;
          require(blueprint.at("name") ==
                          defaults.at(station ? "stationName" : "shipName") &&
                      blueprint.at("buildType") ==
                          (station ? "station" : "ship") &&
                      blueprint.at("budget") == defaults.at("budget") &&
                      blueprint.at("loads") == defaults.at("loads") &&
                      blueprint.at("decks").size() == active.data()
                                                          .at("limits")
                                                          .at("minDecks")
                                                          .get<std::size_t>(),
                  "Blank authoring defaults must come from active rules");
          const auto source = canonical_json(blueprint);
          const auto imported = import_definition(source, active, schema);
          require(imported.ok(), "A public blank must import normally");
          import_identities(Json{{"issues", imported.issues}}, true);
          const auto report = perf(source, active, schema);
          check_f20(report, true);
          require(report.at("data").at("structuralPass") == false &&
                      has_issue(report, "required_system"),
                  "Blank structure remains inspectable without inventing "
                  "required systems");
        }
        require(blank_blueprint(active) ==
                    blank_blueprint(active, BlankType::ship),
                "The public blank default must be a ship");
      });

  suite.run(
      "Wayfarer engine-less station preserves valid structure and F20", [&] {
        // Ordinary import accepts the historical source; perf must derive
        // its own values from the blueprint and active rules afterward.
        const auto imported = import_definition(
            read("fixtures/v3/apsis-wayfarer-anchorage-definition-v3.json"),
            active, schema);
        require(imported.ok(), "Wayfarer must import normally");
        import_identities(Json{{"issues", imported.issues}}, false);
        const auto report =
            perf(canonical_json(imported.value->definition.at("blueprint")),
                 active, schema);
        check_f20(report, false);
        require(report.at("pass") == true && report.at("status") == "PASS" &&
                    report.at("data").at("structuralPass") == true,
                "F20 must not reject or structurally reclassify Wayfarer");
        const auto &performance = report.at("data").at("performance");
        require(performance.at("currentMass").get<double>() > 0 &&
                    performance.at("sublight").at("burnRate") == 0 &&
                    performance.at("acceleration") == 0 &&
                    performance.at("range") == 0,
                "Positive-mass no-thrust/no-drive results remain valid "
                "computed zeros, distinct from excluded formulas");
      });

  suite.run(
      "engine-less positive-mass ship and station omit only undefined metrics",
      [&] {
        for (const auto type : {BlankType::ship, BlankType::station}) {
          auto blueprint = blank_blueprint(active, type);
          blueprint["decks"][0]["floors"]["0,0"] = true;
          const auto source = canonical_json(blueprint);
          const auto imported = import_definition(source, active, schema);
          require(imported.ok(), "The engine-less design must import normally");
          import_identities(Json{{"issues", imported.issues}}, false);
          const auto report = perf(source, active, schema);
          check_f20(report, false);
          const auto &performance = report.at("data").at("performance");
          require(performance.at("dryMass").get<double>() > 0 &&
                      performance.at("acceleration") == 0 &&
                      performance.at("range") == 0 &&
                      performance.at("fuelToDryMassRatio") == 0 &&
                      has_issue(report, "required_system"),
                  "Engine-less nonblank designs retain legitimate zeros "
                  "and ordinary structural faults");
        }
      });

  suite.run("computed station metrics do not carry provisional F20", [&] {
    const auto source =
        read("fixtures/v3/apsis-wayfarer-anchorage-definition-v3.json");
    auto changed = active.data();
    changed["formulas"]["burnSeconds"] = 1;
    changed["formulas"]["exhaustVelocity"] = 2;
    changed["formulas"]["deltaV"] = 3;
    const auto loaded = load_rules(canonical_json(changed), schema);
    require(loaded.ok(), "Constant sublight formulas must load as rules");

    const auto imported = import_definition(source, *loaded.value, schema);
    require(imported.ok(), "Wayfarer must import under the custom rules");
    import_identities(Json{{"issues", imported.issues}}, false);
    import_identities(validate(source, *loaded.value, schema), false);

    const auto report = perf(source, *loaded.value, schema);
    require(report.at("pass") == true, "Wayfarer must remain accepted");
    const auto &sublight = report.at("data").at("performance").at("sublight");
    require(
        sublight.at("burnRate") == 0 && sublight.at("fullBurnSeconds") == 1 &&
            sublight.at("currentBurnSeconds") == 1 &&
            sublight.at("exhaustVelocity") == 2 && sublight.at("deltaV") == 3,
        "Accepted constants produce all four engine-less outputs");
    require(!has_issue(report, "pending_F20"),
            "Perf has no F20 issue when all the outputs were evaluated");
  });

  for (const auto *operation : {"add", "mul"}) {
    suite.run(
        "F20 does not hide a nonfinite " + std::string(operation) + " prefix",
        [&] {
          auto changed = active.data();
          changed["formulas"]["acceleration"] =
              Json::array({operation, 1e308, 1e308,
                           Json::array({"div", "thrust", "mass"})});
          const auto loaded = load_rules(canonical_json(changed), schema);
          require(loaded.ok(),
                  "Finite operands must load as an active formula");
          const auto source = canonical_json(blank_blueprint(*loaded.value));
          require(import_definition(source, *loaded.value, schema).ok(),
                  "The blank must import before the acceleration calculation");

          const auto report = perf(source, *loaded.value, schema);
          require(report.at("status") == "FAIL" && report.at("data").is_null(),
                  "A known intermediate overflow must reject even beside F20");
          const auto &issues = report.at("issues");
          require(
              std::any_of(
                  issues.begin(), issues.end(),
                  [](const Json &item) {
                    return item.at("code") == "formula_error" &&
                           item.at("severity") == "error" &&
                           item.at("path") == "/formulas/acceleration" &&
                           item.at("details").at("outputPath") ==
                               "/performance/scenarios/current/acceleration";
                  }),
              "The failed intermediate must identify its formula and output");
        });
  }

  suite.run("blank deltaV logarithm at zero stays explicitly pending F20", [&] {
    auto changed = active.data();
    // Exercise the logarithm independently of the usual endMass division,
    // whose failure prevents the enclosing logarithm from being evaluated.
    changed["formulas"]["deltaV"] = Json::array({"ln", 0});
    const auto loaded = load_rules(canonical_json(changed), schema);
    require(loaded.ok(), "A well-formed ln expression must load as rules");
    for (const auto type : {BlankType::ship, BlankType::station}) {
      const auto report =
          perf(canonical_json(blank_blueprint(*loaded.value, type)),
               *loaded.value, schema);
      require(!report.at("data").is_null() &&
                  !has_issue(report, "formula_error"),
              "The exact blank ln(0) exclusion must not reject calculation");
      output_pending(report, "/performance/sublight/deltaV", "deltaV");
      const auto &evaluations =
          pending_at(report, "/performance/sublight/deltaV")
              .at("details")
              .at("evaluations");
      require(evaluations.size() == 1 &&
                  evaluations[0].at("operator") == "ln" &&
                  evaluations[0].at("operand") == 0,
              "The pending report must identify the excluded zero logarithm");
      no_excluded_keys(report.at("data").at("performance"), true);
      no_excluded_keys(report.at("data").at("definition").at("derived"), true);
    }
  });

  suite.run("a blank with positive current mass rejects undefined deltaV", [&] {
    constexpr double current_mass = 3.25;
    auto changed = active.data();
    changed["formulas"]["loadedMass"] = current_mass;
    changed["formulas"]["deltaV"] = Json::array({"ln", 0});
    const auto loaded = load_rules(canonical_json(changed), schema);
    require(loaded.ok(), "The adjusted mass and deltaV formulas must load");
    for (const auto *type : {"ship", "station"}) {
      const auto source = canonical_json(blank(type));
      const auto imported = import_definition(source, *loaded.value, schema);
      require(imported.ok(), "Direct totals succeed for the empty blueprint");
      const auto &totals = imported.value->definition.at("derived");
      require(totals.at("dryMass") == 0 &&
                  totals.at("currentMass") == current_mass,
              "The case must have positive current mass and zero dry mass");

      const auto report = perf(source, *loaded.value, schema);
      require(report.at("status") == "FAIL" && report.at("pass") == false &&
                  report.at("data").is_null(),
              "F10 rejects ln(0) at positive current mass");
      const auto &issues = report.at("issues");
      const auto failure =
          std::find_if(issues.begin(), issues.end(), [](const Json &item) {
            return item.at("code") == "formula_error" &&
                   item.at("path") == "/formulas/deltaV";
          });
      require(failure != issues.end(), "The error identifies the deltaV rule");
      const auto &details = failure->at("details");
      require(failure->at("severity") == "error" &&
                  details.at("operator") == "ln" &&
                  details.at("operand") == 0 &&
                  details.at("outputPath") == "/performance/sublight/deltaV",
              "The error identifies the failed deltaV calculation");
    }
  });

  return suite.finish();
}
