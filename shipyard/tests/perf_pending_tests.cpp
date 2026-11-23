#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <set>

namespace {
using namespace test;

bool has_issue(const Json &report, std::string_view code) {
  return std::any_of(report.at("issues").begin(), report.at("issues").end(),
                     [&](const Json &item) { return item.at("code") == code; });
}

const Json &unavailable_at(const Json &report, std::string_view path) {
  for (const auto &item : report.at("issues")) {
    if (item.at("code") == "stat_unavailable" && item.at("path") == path) {
      require(
          item.at("severity") == "warning" &&
              !item.at("message").get<std::string>().empty() &&
              item.at("details").at("finding") == "F20" &&
              !item.at("details").at("evaluations").empty(),
          "An unavailable stat needs a structured, nonblocking resolved issue: " +
              std::string(path));
      require(std::count_if(report.at("issues").begin(), report.at("issues").end(),
                            [&](const Json &entry) {
        return entry.at("code") == "stat_unavailable" && entry.at("path") == path;
      }) == 1, "Each unavailable output has exactly one diagnostic");
      std::set<std::string> evaluations;
      for (const auto &entry : item.at("details").at("evaluations"))
        require(evaluations.insert(entry.dump()).second,
                "Ordinary unavailable output evaluation identities are unique");
      return item;
    }
  }
  throw std::runtime_error("Missing stat_unavailable at " + std::string(path) +
                           ": " + report.at("issues").dump());
}

void unavailable_values(const Json &value, bool zero_mass) {
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
      if (excluded.contains(key))
        require(child == "n/a", "Undefined metrics use exact n/a: " + key);
      unavailable_values(child, zero_mass);
    }
  } else if (value.is_array()) {
    for (const auto &child : value)
      unavailable_values(child, zero_mass);
  }
}

void output_unavailable(const Json &report, const std::string &path,
                    std::string_view formula) {
  const auto &pending = unavailable_at(report, path);
  const Json::json_pointer metric_path(
      path.substr(std::string("/performance").size()));
  require(report.at("data").at("performance").at(metric_path) == "n/a",
          "The unavailable output path must contain exact n/a: " + path);
  for (const auto &evaluation : pending.at("details").at("evaluations")) {
    require(evaluation.at("formula") == formula &&
                evaluation.at("formulaPath")
                    .get<std::string>()
                    .starts_with("/formulas/" + std::string(formula)),
            "An unavailable formula must identify its active rules path: " + path);
  }
}

void check_f20(const Json &report, bool zero_mass) {
  require(!report.at("data").is_null(),
          "F20 exclusions must retain calculated data, not reject the design");
  require(!has_issue(report, "formula_error") &&
              !has_issue(report, "nonfinite_total") &&
              !has_issue(report, "nonfinite_performance"),
          "Pending F20 must not become a formula/nonfinite rejection");
  require(!has_issue(report, "pending_F20"),
          "Resolved stats must not retain speculative importer pending metadata");
  const auto &data = report.at("data");
  const auto &performance = data.at("performance");
  require(performance == data.at("definition").at("derived").at("performance"),
          "The definition and top-level performance must share unavailable values");
  unavailable_values(performance, zero_mass);
  unavailable_values(data.at("definition").at("derived"), zero_mass);
  output_unavailable(report, "/performance/sublight/fullBurnSeconds",
                 "burnSeconds");
  output_unavailable(report, "/performance/sublight/currentBurnSeconds",
                 "burnSeconds");
  output_unavailable(report, "/performance/sublight/exhaustVelocity",
                 "exhaustVelocity");
  output_unavailable(report, "/performance/sublight/deltaV", "deltaV");
  const auto &delta = unavailable_at(report, "/performance/sublight/deltaV");
  require(
      std::any_of(delta.at("details").at("evaluations").begin(),
                  delta.at("details").at("evaluations").end(),
                  [](const Json &evaluation) {
                    return evaluation.value("dependency", std::string()) ==
                           "exhaustVelocity";
                  }),
      "Omitted deltaV must identify its excluded exhaustVelocity dependency");
  if (zero_mass) {
    for (const auto *key : {"acceleration", "range", "fuelToDryMassRatio",
                            "accelerationRating", "underpowered"})
      require(performance.at(key) == "n/a",
              "Every unavailable summary uses exact n/a: " + std::string(key));
    for (const auto *scenario : {"current", "empty", "half", "full"}) {
      const auto path = "/performance/scenarios/" + std::string(scenario);
      require(performance.at("scenarios").at(scenario).at("mass") == 0,
              "A public blank design must actually have zero scenario mass");
      output_unavailable(report, path + "/acceleration", "acceleration");
      output_unavailable(report, path + "/range", "range");
    }
    output_unavailable(report, "/performance/fuelToDryMassRatio",
                   "fuelToDryMassRatio");
  }
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

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
          const auto report = perf(canonical_json(blueprint), active, schema);
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
      "engine-less positive-mass ship and station mark only undefined metrics n/a",
      [&] {
        for (const auto type : {BlankType::ship, BlankType::station}) {
          auto blueprint = blank_blueprint(active, type);
          blueprint["decks"][0]["floors"]["0,0"] = true;
          const auto report = perf(canonical_json(blueprint), active, schema);
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

  suite.run("data-zero deltaV logarithm uses n/a", [&] {
    auto changed = active.data();
    // Exercise the logarithm independently of the usual endMass division,
    // whose failure prevents the enclosing logarithm from being evaluated.
    changed["formulas"]["deltaV"] = Json::array({"ln", "startMass"});
    const auto loaded = load_rules(canonical_json(changed), schema);
    require(loaded.ok(), "A well-formed ln expression must load as rules");
    for (const auto type : {BlankType::ship, BlankType::station}) {
      const auto report =
          perf(canonical_json(blank_blueprint(*loaded.value, type)),
               *loaded.value, schema);
      require(!report.at("data").is_null() &&
                  !has_issue(report, "formula_error"),
              "The exact blank ln(0) exclusion must not reject calculation");
      output_unavailable(report, "/performance/sublight/deltaV", "deltaV");
      const auto &evaluations =
          unavailable_at(report, "/performance/sublight/deltaV")
              .at("details")
              .at("evaluations");
      require(evaluations.size() == 1 &&
                  evaluations[0].at("operator") == "ln" &&
                  evaluations[0].at("operand") == 0,
              "The pending report must identify the data-zero logarithm");
      unavailable_values(report.at("data").at("performance"), true);
      unavailable_values(report.at("data").at("definition").at("derived"), true);
    }
  });

  // Decisions: Luna, under owner delegation 2026-11-23. Expectations below
  // come from zero material, actual rule operands and the public n/a contract.
  suite.run("N1 literal zero-cell JSON agrees with blank authoring and round trips", [&] {
    auto literal = Json::parse(R"({"schema":"apsis-shipyard-v4","name":"Blank",
      "buildType":"ship","budget":100000,"loads":{"cargo":0,"jumpFuel":1,
      "sublightFuel":1},"decks":[{"name":"Main deck","floors":{},"modules":[]}],
      "nextId":1,"migrationNotes":[]})");
    for (const auto type : {BlankType::ship, BlankType::station}) {
      auto factory = blank_blueprint(active, type);
      literal["name"] = factory.at("name");
      literal["buildType"] = factory.at("buildType");
      require(literal == factory, "Literal zero-cell source must equal the factory source");
      const auto loaded = import_definition(literal.dump(), active, schema);
      require(loaded.ok(), "Explicit zero-cell JSON must import");
      const auto again = import_definition(loaded.value->definition.dump(), active, schema);
      require(again.ok() && again.value->definition == loaded.value->definition,
              "Zero-cell definitions export and reimport without changes");
      const auto report = perf(literal.dump(), active, schema);
      require(report == perf(factory.dump(), active, schema),
              "Literal JSON and blank authoring calculate identically");
      check_f20(report, true);
      const auto &totals = report.at("data").at("definition").at("derived");
      for (const auto *key : {"cost", "floorCount", "structureMass", "componentMass",
                              "dryMass", "currentMass", "cargo", "fuel", "sublightFuel"})
        require(totals.at(key).is_number() && totals.at(key) == 0,
                "Zero material keeps a numeric zero direct total: " + std::string(key));
    }
  });

  suite.run("nonblank real zero-mass geometry uses n/a beyond the blank shape", [&] {
    auto changed = active.data();
    changed["floor"]["mass"] = 0;
    changed["floorMass"] = 0;
    for (auto &part : changed["parts"])
      if (part.at("id") == "cockpit") part["mass"] = 0;
    const auto loaded = load_rules(changed.dump(), schema);
    require(loaded.ok(), "Real replacement rules may assign zero material mass");
    auto source = blank_blueprint(*loaded.value);
    source["decks"][0]["floors"]["3,3"] = true;
    source["decks"][0]["modules"].push_back(
        {{"id",1},{"type","cockpit"},{"x",3},{"y",3},{"r",0}});
    source["nextId"] = 2;
    const auto report = perf(source.dump(), *loaded.value, schema);
    check_f20(report, true);
    require(report.at("data").at("definition").at("blueprint") == source &&
                report.at("data").at("definition").at("derived").at("floorCount") == 1,
            "Actual nonempty floors and modules must survive zero-mass calculation");
  });

  suite.run("literal invalid divisions and logarithms remain F10", [&] {
    struct Invalid { const char *formula; Json expression; const char *output; };
    const std::vector<Invalid> invalid{
      {"acceleration", Json::array({"div", "thrust", 0}), "/performance/scenarios/current/acceleration"},
      {"range", Json::array({"div", "rating", 0}), "/performance/scenarios/current/range"},
      {"burnSeconds", Json::array({"div", "fuel", 0}), "/performance/sublight/fullBurnSeconds"},
      {"exhaustVelocity", Json::array({"div", "thrust", 0}), "/performance/sublight/exhaustVelocity"},
      {"deltaV", Json::array({"ln", 0}), "/performance/sublight/deltaV"},
      {"deltaV", Json::array({"ln", -1}), "/performance/sublight/deltaV"},
      {"fuelToDryMassRatio", Json::array({"div", "fuelMass", 0}), "/performance/fuelToDryMassRatio"}};
    for (const auto &test : invalid) {
      auto changed = active.data();
      changed["formulas"][test.formula] = test.expression;
      const auto loaded = load_rules(changed.dump(), schema);
      require(loaded.ok(), "Well-shaped invalid arithmetic must reach F10 evaluation");
      const auto report = perf(blank_blueprint(*loaded.value).dump(), *loaded.value, schema);
      require(report.at("status") == "FAIL" && report.at("pass") == false &&
                  report.at("data").is_null(), "Invalid literal arithmetic must reject calculation");
      bool found = false;
      for (const auto &entry : report.at("issues")) {
        if (entry.at("code") == "formula_error") {
          found = true;
          require(entry.at("path") == "/formulas/" + std::string(test.formula) &&
                      entry.at("severity") == "error" &&
                      entry.at("details").at("outputPath") == test.output,
                  "F10 identifies the actual failed formula and output");
        }
        require(entry.at("code") != "stat_unavailable" || entry.at("path") != test.output,
                "The invalid expression cannot be reclassified as unavailable");
      }
      require(found, "The rejected literal expression must expose formula_error");
    }
    auto changed = active.data();
    changed["formulas"]["structureMass"] = Json::array({"div", "floorCount", 0});
    const auto loaded = load_rules(changed.dump(), schema);
    require(loaded.ok(), "A well-shaped direct-total formula must load");
    const auto report = perf(blank_blueprint(*loaded.value).dump(), *loaded.value, schema);
    require(report.at("data").is_null() && has_issue(report, "formula_error"),
            "F20 never exempts an invalid direct-total formula");
  });

  suite.run("valid constant zero formulas remain numeric on a zero-cell design", [&] {
    auto changed = active.data();
    for (const auto *formula : {"acceleration", "range", "burnSeconds",
                                "exhaustVelocity", "deltaV", "fuelToDryMassRatio"})
      changed["formulas"][formula] = 0;
    const auto loaded = load_rules(changed.dump(), schema);
    require(loaded.ok(), "Constant zero formulas are valid real rules");
    const auto report = perf(blank_blueprint(*loaded.value).dump(), *loaded.value, schema);
    require(report.at("data").is_object() && !has_issue(report, "stat_unavailable"),
            "Successfully evaluated zero must not be replaced by n/a");
    const auto &performance = report.at("data").at("performance");
    for (const auto *key : {"acceleration", "range", "fuelToDryMassRatio"})
      require(performance.at(key).is_number() && performance.at(key) == 0,
              "Successful summary arithmetic keeps numeric zero");
    for (const auto *key : {"fullBurnSeconds", "currentBurnSeconds", "exhaustVelocity", "deltaV"})
      require(performance.at("sublight").at(key).is_number() &&
                  performance.at("sublight").at(key) == 0,
              "Successful burn arithmetic keeps numeric zero");
  });

  return suite.finish();
}
