#include "test_support.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <set>
#include <sstream>
#include <string_view>

namespace {
using namespace test;

struct Fixture {
  const char *stem;
  const char *name;
};

constexpr std::array fixtures{
    Fixture{"wren", "Wren"}, Fixture{"skiff", "Skiff"},
    Fixture{"kestrel-tender", "Kestrel Tender"},
    Fixture{"wayfarer-anchorage", "Wayfarer Anchorage"},
    Fixture{"rustbucket", "Rustbucket"}};

Json fixture(const Fixture &entry) {
  return Json::parse(read(std::string("fixtures/v3/apsis-") + entry.stem +
                          "-definition-v3.json"));
}

// OpenSSL hashes the original input independently of loader serialization.
std::string sha256(std::string_view bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int size = 0;
  require(EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size,
                     EVP_sha256(), nullptr) == 1,
          "independent SHA-256 calculation failed");
  require(size == 32, "SHA-256 digest length");
  std::ostringstream encoded;
  encoded << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < size; ++i)
    encoded << std::setw(2) << static_cast<int>(digest[i]);
  return encoded.str();
}

void preserved(const Json &source, const Json &migrated) {
  require(source.at("schema") == "apsis-shipyard-v3", "source must stay v3");
  require(migrated.at("schema") == "apsis-shipyard-v4",
          "target blueprint must be v4");
  const auto &old_notes = source.at("migrationNotes");
  const auto &new_notes = migrated.at("migrationNotes");
  require(new_notes.size() >= old_notes.size(),
          "migration must retain every existing note");
  require(std::equal(old_notes.begin(), old_notes.end(), new_notes.begin()),
          "migration must preserve existing note order and spelling");

  Json old_fields = source;
  Json new_fields = migrated;
  old_fields.erase("schema");
  new_fields.erase("schema");
  old_fields.erase("migrationNotes");
  new_fields.erase("migrationNotes");
  require(source.at("decks").size() == migrated.at("decks").size(),
          "deck count preserved");
  for (std::size_t d = 0; d < source.at("decks").size(); ++d) {
    const auto &old_modules = source.at("decks")[d].at("modules");
    const auto &new_modules = migrated.at("decks")[d].at("modules");
    require(old_modules.size() == new_modules.size(), "module count preserved");
    for (std::size_t m = 0; m < old_modules.size(); ++m) {
      if (old_modules[m].contains("craft") &&
          !old_modules[m].at("craft").is_null()) {
        preserved(old_modules[m].at("craft"), new_modules[m].at("craft"));
        // Child metadata was checked recursively; compare the parent's full
        // remaining payload.
        old_fields["decks"][d]["modules"][m].erase("craft");
        new_fields["decks"][d]["modules"][m].erase("craft");
      }
    }
  }
  require(old_fields == new_fields,
          "migration changed geometry, deck/module order, IDs, labels, "
          "interfaces, budget or loads");
}

const Issue &pending_f20(const Result<ImportedDefinition> &result) {
  const auto found = std::find_if(
      result.issues.begin(), result.issues.end(), [](const Issue &entry) {
        return entry.code == "pending_F20" || entry.code == "pending F20";
      });
  require(found != result.issues.end(),
          "applicable import must report structured pending F20");
  require(found->severity == "warning",
          "pending F20 must not reject an inspectable import");
  require(found->path == "/blueprint",
          "pending F20 identifies the affected blueprint");
  require(!found->message.empty(), "pending F20 must have a readable message");
  require(found->details.at("finding") == "F20",
          "structured finding identifier");
  return *found;
}

std::set<std::string> evaluation_set(const Json &evaluations) {
  require(evaluations.is_array(), "pending evaluations are structured entries");
  std::set<std::string> values;
  for (const auto &evaluation : evaluations)
    values.insert(canonical_json(evaluation));
  require(values.size() == evaluations.size(),
          "pending evaluations are not duplicated");
  return values;
}

Json burn_evaluations() {
  return Json::array(
      {{{"formula", "burnSeconds"},
        {"scenario", "full"},
        {"operand", "burnRate"}},
       {{"formula", "burnSeconds"},
        {"scenario", "current"},
        {"operand", "burnRate"}},
       {{"formula", "exhaustVelocity"}, {"operand", "burnRate"}}});
}

void no_excluded_values(const Json &value) {
  // No performance implementation is supplied by #49. Excluded F20 results must
  // be absent, including nested scenario results; null/zero are not
  // substitutes.
  constexpr std::array excluded{"acceleration", "range",
                                "burnSeconds",  "exhaustVelocity",
                                "deltaV",       "fuelToDryMassRatio"};
  if (value.is_object()) {
    for (auto entry = value.begin(); entry != value.end(); ++entry) {
      require(std::find(excluded.begin(), excluded.end(), entry.key()) ==
                  excluded.end(),
              "excluded F20 result emitted: " + entry.key());
      no_excluded_values(entry.value());
    }
  } else if (value.is_array()) {
    for (const auto &entry : value)
      no_excluded_values(entry);
  }
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  suite.run(
      "rules SHA-256 identifies exact bytes, including insignificant "
      "whitespace",
      [&] {
        const auto bytes = read("rules/apsis-rules-v4.json");
        require(active.sha256() == sha256(bytes),
                "active SHA-256 must hash original bytes");
        require(active.sha256() == "2af029afebc235ead1cdf0c1191a6e0b0c7dffcdb1"
                                   "d37cb4f7657d7eec2f9175",
                "reviewed v4 rules byte identity");
        const auto changed_bytes = bytes + "\n";
        const auto changed = load_rules(changed_bytes, schema_set);
        require(changed.ok(), "trailing JSON whitespace must be accepted");
        require(changed.value->data() == active.data(),
                "whitespace leaves semantic rules unchanged");
        require(changed.value->sha256() == sha256(changed_bytes),
                "changed identity hashes exact new bytes");
        require(changed.value->sha256() != active.sha256(),
                "rules identity must not hash canonical JSON");

        const auto imported = import_definition(canonical_json(blank()),
                                                *changed.value, schema_set);
        require(imported.ok(), "blank import with whitespace-distinct rules");
        require(imported.value->definition.at("rulesSha256") ==
                    sha256(changed_bytes),
                "export reports the active exact-byte identity");
      });

  const auto historical_rules =
      Json::parse(read("fixtures/v3/apsis-rules-v3.json"));
  for (const auto &entry : fixtures) {
    suite.run(
        std::string(entry.name) +
            ": v3 geometry preservation and v4 round trip",
        [&] {
          const auto source = fixture(entry);
          require(source.at("blueprint").at("name") == entry.name,
                  "canonical fixture identity");
          require(source.at("rules") == historical_rules,
                  "historical embedded rules match standalone v3");
          const auto imported =
              import_definition(canonical_json(source), active, schema_set);
          require(imported.ok(), std::string(entry.name) +
                                     " must import to an inspectable draft");
          const auto &migrated = imported.value->definition;
          preserved(source.at("blueprint"), migrated.at("blueprint"));
          require(migrated.at("schema") == "apsis-ship-definition-v4",
                  "v4 export wrapper");
          require(migrated.at("rulesVersion") == 4, "v4 export rules version");
          require(migrated.at("rulesSha256") == active.sha256(),
                  "active rule byte identity exported");
          require(migrated.at("rules") == active.data(),
                  "historical snapshot replaced by active rules");
          require(imported.value->migration.sourceVersion == 3 &&
                      imported.value->migration.targetVersion == 4,
                  "migration report identifies source and target");
          require(imported.value->migration.rulesIdentityChanged,
                  "migration reports changed rules");
          require(!imported.value->migration.changes.empty(),
                  "migration changes remain inspectable");

          const auto serialized = canonical_json(migrated);
          const auto reimported =
              import_definition(serialized, active, schema_set);
          require(reimported.ok(), "exported v4 wrapper must reimport");
          require(canonical_json(reimported.value->definition) == serialized,
                  "v4 reimport must preserve canonical definition and not "
                  "append migration notes");
          require(reimported.value->migration.sourceVersion == 4 &&
                      reimported.value->migration.targetVersion == 4,
                  "v4 reimport report");
          require(!reimported.value->migration.rulesIdentityChanged,
                  "same rules identity on v4 reimport");
          require(reimported.value->migration.changes.empty(),
                  "v4 reimport has no migration changes");

          const auto bare = import_definition(
              canonical_json(source.at("blueprint")), active, schema_set);
          require(bare.ok(), "bare canonical v3 blueprint is supported");
          preserved(source.at("blueprint"),
                    bare.value->definition.at("blueprint"));
        });
  }

  suite.run(
      "Rustbucket remains unrepaired; structural validation belongs to issue "
      "50",
      [&] {
        const auto source = fixture(fixtures.back());
        require(source.at("validation").at("ready") == false,
                "historical negative fixture retained");
        const auto imported =
            import_definition(canonical_json(source), active, schema_set);
        require(imported.ok(), "Rustbucket is retained as a draft, not "
                               "silently repaired or discarded");
        preserved(source.at("blueprint"),
                  imported.value->definition.at("blueprint"));
        require(
            imported.value->definition.at("validation").at("ready").is_null(),
            "loader must not claim structural approval");
        require(issue(imported.issues, "pending_validation", "/validation"),
                "structural-validation boundary remains explicit");
      });

  suite.run(
      "imported historical caches and embedded rules never drive totals", [&] {
        const auto source = fixture(fixtures.front());
        const auto expected =
            import_definition(canonical_json(source), active, schema_set);
        require(expected.ok(), "unmodified reference imports");
        auto poisoned = source;
        poisoned["derived"]["cost"] = -123456;
        poisoned["derived"]["mass"] = 987654321;
        poisoned["derived"]["performance"] = {{"range", 999999},
                                              {"formula", "execute nothing"}};
        poisoned["validation"]["ready"] = true;
        poisoned["validation"]["issues"] = Json::array();
        poisoned["rules"]["floorCost"] = -999;
        poisoned["rules"]["formulas"] = {
            {"structureMass", Json::array({"unknownOperator", 0})}};
        const auto imported =
            import_definition(canonical_json(poisoned), active, schema_set);
        require(imported.ok(),
                "historical caches are informational, never executed");
        require(canonical_json(imported.value->definition) ==
                    canonical_json(expected.value->definition),
                "poisoned historical caches and rules must not affect exported "
                "active definition");
      });

  suite.run("v4 stale identities and caches are replaced by active rules", [&] {
    const auto expected = import_definition(
        canonical_json(fixture(fixtures[0])), active, schema_set);
    require(expected.ok(), "reference Wren import");
    auto poisoned = expected.value->definition;
    poisoned["derived"]["cost"] = -1;
    poisoned["derived"]["performance"] = {{"range", 999999}};
    poisoned["validation"]["ready"] = true;
    poisoned["validation"]["jumpReady"] = true;
    poisoned["validation"]["issues"] = Json::array();
    const auto cache_import =
        import_definition(canonical_json(poisoned), active, schema_set);
    require(cache_import.ok(), "v4 cached values may be stale");
    require(canonical_json(cache_import.value->definition) ==
                canonical_json(expected.value->definition),
            "v4 cached values are discarded and recomputed");

    poisoned["rulesSha256"] = std::string(64, '0');
    poisoned["rules"]["floorCost"] = 0;
    const auto stale_import =
        import_definition(canonical_json(poisoned), active, schema_set);
    require(stale_import.ok(), "v4 stale identity is informational");
    require(stale_import.value->migration.rulesIdentityChanged,
            "v4 stale identity must be reported");
    require(stale_import.value->definition.at("derived") ==
                expected.value->definition.at("derived"),
            "v4 stale rules cannot affect direct totals");
    require(stale_import.value->definition.at("rules") == active.data(),
            "v4 export replaces stale embedded rules");
    require(stale_import.value->definition.at("rulesSha256") == active.sha256(),
            "v4 export identifies active exact rules bytes");
  });

  suite.run(
      "five explicit F01 jump-drive prices are applied during migration", [&] {
        constexpr std::array ids{"drive05", "drive10", "drive15", "drive20",
                                 "drive30"};
        constexpr std::array prices{1500, 2500, 3750, 5000, 7500};
        for (std::size_t i = 0; i < ids.size(); ++i) {
          auto source = blank();
          source["schema"] = "apsis-shipyard-v3";
          source["decks"][0]["modules"] = Json::array(
              {{{"id", 7}, {"type", ids[i]}, {"x", 0}, {"y", 0}, {"r", 0}}});
          source["nextId"] = 8;
          const auto imported =
              import_definition(canonical_json(source), active, schema_set);
          require(imported.ok(),
                  std::string("v3 single-drive draft imports: ") + ids[i]);
          require(
              imported.value->definition.at("derived").at("cost") == prices[i],
              std::string("explicit owner-picked migrated price: ") + ids[i]);
          preserved(source, imported.value->definition.at("blueprint"));
        }
      });

  for (const char *type : {"ship", "station"}) {
    suite.run(
        std::string("blank ") + type +
            ": exact pending F20 cases, no excluded values",
        [&] {
          const auto imported = import_definition(canonical_json(blank(type)),
                                                  active, schema_set);
          require(imported.ok(),
                  "blank imports must succeed while F20 is pending");
          auto expected = burn_evaluations();
          for (const char *formula : {"acceleration", "range"}) {
            expected.push_back({{"formula", formula}, {"operand", "mass"}});
          }
          expected.push_back({{"formula", "deltaV"}, {"operand", "endMass"}});
          expected.push_back({{"formula", "deltaV"}, {"operand", "ln(0)"}});
          expected.push_back(
              {{"formula", "fuelToDryMassRatio"}, {"operand", "dryMass"}});
          require(
              evaluation_set(pending_f20(imported).details.at("evaluations")) ==
                  evaluation_set(expected),
              "blank pending report names exactly the agreed F20 exclusions");
          no_excluded_values(imported.value->definition.at("derived"));
        });
  }

  suite.run(
      "Wayfarer and a nonblank engine-less ship report only burnRate F20 "
      "exclusions",
      [&] {
        auto ship = blank();
        ship["decks"][0]["floors"] = {{"0,0", true}};
        for (const auto &source : {fixture(fixtures[3]), ship}) {
          const auto imported =
              import_definition(canonical_json(source), active, schema_set);
          require(imported.ok(),
                  "engine-less import must succeed without guessed F20 values");
          require(evaluation_set(pending_f20(imported).details.at(
                      "evaluations")) == evaluation_set(burn_evaluations()),
                  "engine-less pending report names full/current burnSeconds "
                  "and exhaustVelocity only");
          no_excluded_values(imported.value->definition.at("derived"));
        }
      });

  suite.run(
      "v1/v2 blueprints and wrappers return actionable unsupported-version "
      "errors",
      [&] {
        for (const char *prefix :
             {"apsis-shipyard-v", "apsis-ship-definition-v"}) {
          for (const int version : {1, 2}) {
            const Json source = {
                {"schema", std::string(prefix) + std::to_string(version)}};
            const auto imported =
                import_definition(canonical_json(source), active, schema_set);
            require(!imported.ok(), "v1/v2 migration must not be guessed");
            require(issue(imported.issues, "unsupported_version", "/schema"),
                    "unsupported version has a stable code and field path");
            const auto &message = imported.issues.front().message;
            require(message.find("v3") != std::string::npos &&
                        message.find("v4") != std::string::npos,
                    "unsupported-version message tells callers which export "
                    "versions to provide");
          }
        }
      });
  return suite.finish();
}
