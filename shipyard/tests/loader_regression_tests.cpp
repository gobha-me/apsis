#include "test_support.hpp"

#include <array>
#include <cstdint>
#include <string>

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  for (const auto *stem : {"wren", "skiff", "kestrel-tender"}) {
    suite.run(std::string(stem) + " has an engine and no pending_F20", [&] {
      const auto source = Json::parse(read(std::string("fixtures/v3/apsis-") +
                                          stem + "-definition-v3.json"));
      bool has_engine = false;
      for (const auto &deck : source.at("blueprint").at("decks")) {
        for (const auto &module : deck.at("modules")) {
          for (const auto &part : active.data().at("parts")) {
            if (part.at("id") == module.at("type") &&
                part.value("system", std::string()) == "engine")
              has_engine = true;
          }
        }
      }
      require(has_engine, "canonical regression fixture must have an engine");
      const auto imported =
          import_definition(source.dump(), active, schema_set);
      require(imported.ok(), "engine-equipped canonical ship must import");
      require(std::none_of(imported.issues.begin(), imported.issues.end(),
                           [](const Issue &item) {
                             return item.code == "pending_F20";
                           }),
              "engine-equipped canonical ship must not exclude F20 evaluations");
      for (const auto &item :
           imported.value->definition.at("validation").at("issues")) {
        require(item.at("code") != "pending_F20",
                "exported issue list must also omit pending_F20");
      }
    });
  }

  suite.run("direct import reports F21 as the only pending docking finding", [&] {
    const auto imported = import_definition(blank().dump(), active, schema_set);
    require(imported.ok(), "direct import must return a definition");
    const auto expected_findings = Json::array({"F21"});
    const auto advisory = std::find_if(
        imported.issues.begin(), imported.issues.end(),
        [](const Issue &item) { return item.code == "pending_docking"; });
    require(advisory != imported.issues.end() && advisory->severity == "warning" &&
                advisory->details.at("findings") == expected_findings,
            "direct import must return a warning for F21 alone");

    const auto &issues =
        imported.value->definition.at("validation").at("issues");
    const auto serialized = std::find_if(
        issues.begin(), issues.end(),
        [](const Json &item) { return item.at("code") == "pending_docking"; });
    require(serialized != issues.end() &&
                serialized->at("severity") == "warning" &&
                serialized->at("details").at("findings") == expected_findings,
            "exported validation must contain a warning for F21 alone");
  });

  suite.run("module grid capacity is exact even when footprints overlap", [&] {
    auto source = blank();
    const auto capacity = active.data().at("grid").at("w").get<std::uint64_t>() *
                          active.data().at("grid").at("h").get<std::uint64_t>();
    auto &modules = source["decks"][0]["modules"];
    // Unique IDs and valid one-cell catalogue parts isolate the count guard.
    // Structural overlap legality belongs to the validator, not this importer.
    for (std::uint64_t id = 1; id <= capacity; ++id)
      modules.push_back({{"id", id},
                         {"type", "microReactor"},
                         {"x", 0},
                         {"y", 0},
                         {"r", 0}});
    source["nextId"] = capacity + 1;
    require(import_definition(source.dump(), active, schema_set).ok(),
            "exactly grid capacity modules must pass importer count checks");

    modules.push_back({{"id", capacity + 1},
                       {"type", "microReactor"},
                       {"x", 0},
                       {"y", 0},
                       {"r", 0}});
    source["nextId"] = capacity + 2;
    const auto over = import_definition(source.dump(), active, schema_set);
    require(!over.ok(), "one module beyond grid capacity must fail import");
    require(issue(over.issues, "grid_capacity", "/decks/0"),
            "module count overflow must identify the deck with grid_capacity");
  });

  suite.run("matching rules hash cannot hide changed embedded rules", [&] {
    const auto initial = import_definition(blank().dump(), active, schema_set);
    require(initial.ok(), "reference v4 definition must import");
    const auto unchanged = import_definition(initial.value->definition.dump(),
                                             active, schema_set);
    require(unchanged.ok() && !unchanged.value->migration.rulesIdentityChanged,
            "unchanged v4 rules identity must remain unchanged");

    auto altered = initial.value->definition;
    altered["rules"]["floorCost"] =
        active.data().at("floorCost").get<double>() + 1;
    require(altered.at("rulesSha256") == active.sha256() &&
                altered.at("rulesVersion") == active.version(),
            "hash and version must match while embedded rules differ");
    const auto imported =
        import_definition(altered.dump(), active, schema_set);
    require(imported.ok(), "embedded rules are informational and replaceable");
    require(imported.value->migration.rulesIdentityChanged,
            "changed embedded rules must report identity change despite matching hash");
    require(imported.value->definition.at("rules") == active.data(),
            "active rules must replace the changed embedded snapshot");
    require(imported.value->definition.at("rulesSha256") == active.sha256(),
            "export must retain the active exact-byte rules hash");
    require(imported.value->definition.at("derived") ==
                initial.value->definition.at("derived"),
            "changed embedded rules must not affect recomputed direct totals");
  });
  return suite.finish();
}
