#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <tuple>

namespace {
using namespace test;

RulesSnapshot warning_rules(const RulesSnapshot &active,
                            const SchemaSet &schema_set,
                            std::size_t acceleration_operands = 0) {
  auto authored = active.data();
  // Isolate warning-only amplification through the real configurable policy.
  // Geometry, carrier sizes, parser bounds and required-system lists stay active.
  for (auto &check : authored["validation"]["checks"])
    check["severity"] = "warning";
  if (acceleration_operands != 0) {
    // Two bounded add nodes keep every arity within the active 64-operand cap.
    Json expression = Json::array({"add"});
    for (const auto count : {acceleration_operands / 2,
                             acceleration_operands - acceleration_operands / 2}) {
      Json group = Json::array({"add"});
      for (std::size_t i = 0; i < count; ++i)
        group.push_back(Json::array({"div", "thrust", "mass"}));
      expression.push_back(std::move(group));
    }
    authored["formulas"]["acceleration"] = std::move(expression);
  }
  const auto loaded = load_rules(canonical_json(authored), schema_set);
  require(loaded.ok() && loaded.issues.empty(),
          "Authored warning policy and bounded formulas must load normally");
  return *loaded.value;
}

Json carrier(std::size_t children, const RulesSnapshot &active) {
  require(children <= 15, "The authored carrier uses at most 15 spaced berths");
  auto source = blank_blueprint(active);
  source["name"] = "Diagnostic carrier";
  // A 5-by-3 arrangement of 4-by-6 berths fits the unchanged 26-by-24 grid.
  // One-cell aisles separate their footprints; every footprint has real floors.
  auto &deck = source["decks"][0];
  for (int x = 0; x < 25; ++x)
    for (int y = 0; y < 21; ++y)
      deck["floors"][std::to_string(x) + "," + std::to_string(y)] = true;
  for (std::size_t i = 0; i < children; ++i) {
    auto craft = blank_blueprint(active);
    craft["name"] = "Pending craft " + std::to_string(i);
    deck["modules"].push_back(
        {{"id", i + 1}, {"type", "shuttleDock"},
         {"x", (i % 5) * 5}, {"y", (i / 5) * 7}, {"r", 0},
         {"craft", std::move(craft)}});
  }
  source["nextId"] = children + 1;
  return source;
}

std::string importable(const Json &source, const RulesSnapshot &active,
                       const SchemaSet &schema_set) {
  const auto bytes = canonical_json(source);
  require(bytes.size() < InputLimits{}.maxBytes,
          "Authored cases must fit the default public byte limit");
  const auto imported = import_definition(bytes, active, schema_set);
  require(imported.ok() && imported.issues.size() < 128,
          "Import must succeed below the cap before performance runs");
  for (const auto &item : imported.issues)
    require(item.severity == "warning" && item.code != "issues_truncated",
            "The importer must contribute only ordinary warnings");
  return bytes;
}

const Json &finding(const Json &report, std::string_view code,
                    std::string_view path) {
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code && item.at("path") == path)
      return item;
  throw std::runtime_error("Missing " + std::string(code) + " at " +
                           std::string(path) + " among " +
                           std::to_string(report.at("issues").size()) +
                           " diagnostics");
}

void warnings_only(const Json &report) {
  for (const auto &item : report.at("issues")) {
    if (item.at("code") == "issues_truncated")
      continue;
    require(item.at("severity") == "warning" &&
                !item.at("message").get<std::string>().empty() &&
                item.at("code") != "formula_error",
            "Ordinary warnings and pending F20 must not turn into errors");
  }
}

void saturated(const Json &report) {
  require(report.at("schema") == "apsis-shipyard-result-v1" &&
              report.at("operation") == "perf" &&
              report.at("status") == "FAIL" && report.at("pass") == false &&
              report.at("data").is_null(),
          "Saturation must fail the public perf envelope with null data");
  const auto &issues = report.at("issues");
  require(!issues.empty() && issues.size() <= 128,
          "The marker must fit inside the shared 128-issue allowance");
  std::size_t markers = 0;
  for (const auto &item : issues) {
    if (item.at("code") != "issues_truncated")
      continue;
    ++markers;
    require(item.at("severity") == "error" && item.at("path") == "" &&
                !item.at("message").get<std::string>().empty(),
            "Exactly one readable truncation error must remain at the root");
    const auto &details = item.at("details");
    require(details.is_object() && details.at("limit") == 128 &&
                details.at("suppressedAtLeast").is_number_integer() &&
                details.at("suppressedAtLeast").get<std::size_t>() >= 1 &&
                details.size() == 2,
            "Bound details must survive aggregation without child remapping");
  }
  require(markers == 1, "Child and parent collectors must share one root marker");
  // The public contract sorts diagnostics even when collection stops early.
  require(std::is_sorted(issues.begin(), issues.end(),
                         [](const Json &left, const Json &right) {
    return std::tuple(left.at("path"), left.at("code"), left.at("severity")) <
           std::tuple(right.at("path"), right.at("code"), right.at("severity"));
  }), "Truncated diagnostics must retain the documented public ordering");
}

void blank_omissions(const Json &performance) {
  for (const auto *key : {"acceleration", "range", "fuelToDryMassRatio"})
    require(!performance.contains(key),
            "Excluded blank metrics cannot acquire any stand-in value");
  for (const auto *key : {"fullBurnSeconds", "currentBurnSeconds",
                          "exhaustVelocity", "deltaV"})
    require(!performance.at("sublight").contains(key),
            "Engine-less sublight omissions must remain absent");
  for (const auto *name : {"current", "empty", "half", "full"}) {
    const auto &scenario = performance.at("scenarios").at(name);
    require(scenario.at("mass") == 0 && !scenario.contains("acceleration") &&
                !scenario.contains("range"),
            "Every zero-mass child scenario must omit undefined outputs");
  }
}

void pending(const Json &report, const std::string &path,
             std::size_t evaluations) {
  const auto &item = finding(report, "pending_F20", path);
  require(item.at("severity") == "warning" &&
              item.at("details").at("finding") == "F20" &&
              item.at("details").at("evaluations").size() == evaluations,
          "A below-cap F20 report must retain every excluded evaluation");
}
} // namespace

int main() {
  const auto schema_set = schemas();
  const auto active = rules(schema_set);
  const auto warning_policy = warning_rules(active, schema_set);
  Suite suite;

  suite.run("below-cap carried warnings preserve pending F20 and omissions", [&] {
    const auto bytes = importable(carrier(3, warning_policy), warning_policy,
                                  schema_set);
    const auto report = perf(bytes, warning_policy, schema_set);
    require(report.at("pass") == true && report.at("status") == "PASS" &&
                report.at("issues").size() < 128,
            "Ordinary warning-only performance must succeed below the cap");
    warnings_only(report);
    require(std::none_of(report.at("issues").begin(), report.at("issues").end(),
                         [](const Json &item) {
                           return item.at("code") == "issues_truncated";
                         }), "A below-cap result must have no truncation marker");
    require(finding(report, "no_cargo", "/performance").at("severity") ==
                "warning", "The ordinary no-cargo advisory must survive");
    const auto &data = report.at("data");
    require(data.at("performance") ==
                data.at("definition").at("derived").at("performance"),
            "The recomputed definition must preserve the same performance");
    const auto &children = data.at("performance").at("carriedCraft");
    require(children.size() == 3, "Every below-cap child must be calculated");
    for (std::size_t i = 0; i < children.size(); ++i) {
      blank_omissions(children[i].at("performance"));
      pending(report, "/performance/carriedCraft/" + std::to_string(i) +
                          "/performance/sublight/exhaustVelocity", 1);
    }
  });

  suite.run("provisional F20 does not consume the carried warning budget", [&] {
    constexpr std::size_t children = 6;
    auto source = carrier(children, warning_policy);
    source["buildType"] = "station";
    const auto bytes = importable(source, warning_policy, schema_set);
    const auto report = perf(bytes, warning_policy, schema_set);
    require(report.at("pass") == true && report.at("issues").size() < 128,
            "Actual diagnostics from six carried blanks fit the issue budget");
    require(report.at("data").at("performance").at("carriedCraft").size() ==
                children,
            "Every child completes performance calculation within the budget");
    pending(report,
            "/performance/carriedCraft/5/performance/sublight/exhaustVelocity",
            1);
  });

  suite.run("warning-only carried traversal and public aggregation saturate",
            [&] {
    for (const auto children : {8U, 15U}) {
      const auto bytes = importable(carrier(children, warning_policy),
                                    warning_policy, schema_set);
      const auto report = perf(bytes, warning_policy, schema_set);
      saturated(report);
      warnings_only(report);
      require(report.at("issues").size() == 128,
              "Warning amplification must retain 127 warnings plus its marker");
      // Truncation may drop any particular late metric. Retain proof that
      // performance traversal reached the children, without requiring its tail.
      require(std::any_of(report.at("issues").begin(), report.at("issues").end(),
                           [](const Json &item) {
        return item.at("code") == "pending_F20" &&
               item.at("path").get<std::string>().starts_with(
                   "/performance/carriedCraft/") &&
               item.at("details").at("finding") == "F20" &&
               !item.at("details").at("evaluations").empty();
      }), "Saturation must be reached through real child performance warnings");
      finding(report, "no_cargo", "/performance");
    }
  });

  suite.run("127 independent F20 evaluations remain ordinary pending warnings",
            [&] {
    const auto policy = warning_rules(active, schema_set, 127);
    const auto bytes = importable(blank_blueprint(policy), policy, schema_set);
    const auto report = perf(bytes, policy, schema_set);
    require(report.at("status") == "PASS" && report.at("pass") == true &&
                report.at("issues").size() < 128,
            "A legal 127-operand evaluation must not be truncated");
    warnings_only(report);
    blank_omissions(report.at("data").at("performance"));
    for (const auto *name : {"current", "empty", "half", "full"})
      pending(report, "/performance/scenarios/" + std::string(name) +
                          "/acceleration", 127);
  });

  suite.run("128 independent F20 evaluations retain a root truncation error",
            [&] {
    const auto policy = warning_rules(active, schema_set, 128);
    for (const bool carried : {false, true}) {
      const auto source = carried ? carrier(1, policy) : blank_blueprint(policy);
      const auto bytes = importable(source, policy, schema_set);
      const auto report = perf(bytes, policy, schema_set);
      saturated(report);
      warnings_only(report);
    }
  });

  suite.run("two failed craft retain distinct performance paths", [&] {
    auto authored = warning_policy.data();
    authored["formulas"]["acceleration"] =
        Json::array({"div", 1, Json::array({"sub", "mass", 0})});
    const auto loaded = load_rules(canonical_json(authored), schema_set);
    require(loaded.ok(), "The acceleration expression must load as rules");
    auto source = carrier(3, *loaded.value);
    // An empty berth between the children has no carried performance slot.
    source["decks"][0]["modules"][1]["craft"] = nullptr;
    const auto bytes = importable(source, *loaded.value, schema_set);
    const auto report = perf(bytes, *loaded.value, schema_set);
    require(report.at("pass") == false && report.at("data").is_null(),
            "Both child-only formula failures reject performance");
    const auto &issues = report.at("issues");
    require(std::count_if(issues.begin(), issues.end(),
                          [](const Json &item) {
                            return item.at("code") == "formula_error";
                          }) == 2,
            "Each blank child must report one formula failure");
    for (const auto index : {0, 1}) {
      const auto blueprint_path =
          "/blueprint/decks/0/modules/" + std::to_string(index * 2) + "/craft";
      const auto output_path = "/performance/carriedCraft/" +
                               std::to_string(index) +
                               "/performance/scenarios/current/acceleration";
      require(std::any_of(issues.begin(), issues.end(),
                          [&](const Json &item) {
                            if (item.at("code") != "formula_error")
                              return false;
                            const auto &details = item.at("details");
                            return item.at("severity") == "error" &&
                                   item.at("path") ==
                                       "/formulas/acceleration" &&
                                   details.at("childBlueprintPath") ==
                                       blueprint_path &&
                                   details.at("outputPath") == output_path;
                          }),
              "A child failure must identify " + blueprint_path + " and " +
                  output_path);
    }
  });

  for (const bool carried : {false, true}) {
    suite.run(
        carried ? "carried direct totals keep one root truncation marker"
                : "root direct totals keep one root truncation marker",
        [&] {
          auto authored = active.data();
          Json group = Json::array({"add"});
          for (int i = 0; i < 64; ++i)
            group.push_back(Json::array({"div", 1, 0}));
          authored["formulas"]["dryMass"] = Json::array({"add", group, group});
          const auto loaded = load_rules(canonical_json(authored), schema_set);
          require(loaded.ok(),
                  "The two bounded formula groups must load as rules");
          const auto &policy = *loaded.value;
          const auto bytes = canonical_json(carried ? carrier(1, policy)
                                                    : blank_blueprint(policy));
          const auto imported = import_definition(bytes, policy, schema_set);
          const auto report = perf(bytes, policy, schema_set);
          saturated(report);
          const auto &issues = report.at("issues");
          require(
              !imported.ok() && issues == Json(imported.issues) &&
                  issues.size() == 128,
              "Import and perf must expose 127 ordinary issues and one marker");
          for (const auto &item : issues) {
            if (item.at("code") == "issues_truncated")
              continue;
            const auto &details = item.at("details");
            require(item.at("code") == "formula_error" &&
                        item.at("path").get<std::string>().starts_with(
                            "/formulas/dryMass/"),
                    "The retained ordinary issues must come from dryMass");
            require(carried ? details.at("childBlueprintPath") ==
                                  "/blueprint/decks/0/modules/0/craft"
                            : !details.contains("childBlueprintPath"),
                    "Only carried formula errors identify a child blueprint");
          }
        });
  }

  return suite.finish();
}
