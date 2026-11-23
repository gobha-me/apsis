#include "test_support.hpp"

#include <apsis/shipyard/authoring.hpp>

#include <vector>

namespace {
using namespace test;
bool name_change(const MigrationReport &report, std::string_view path) {
  return std::any_of(report.changes.begin(), report.changes.end(),
                     [&](const auto &change) {
                       return change.starts_with(std::string(path) + ":");
                     });
}

void normalized(const Json &source, const Json &expected,
                const RulesSnapshot &active, const SchemaSet &schema) {
  const auto result = import_definition(source.dump(), active, schema);
  require(result.ok(), "valid design name must normalize: " +
                           Json(result.issues).dump());
  require(result.value->definition.at("blueprint") == expected,
          "normalization must change only the independently expected name");
  require(name_change(result.value->migration, "/name") == (source != expected),
          "name migration change exists exactly when name bytes change");
  const auto again = import_definition(result.value->definition.dump(), active,
                                       schema);
  require(again.ok() && again.value->definition == result.value->definition &&
              again.value->migration.changes.empty() &&
              !again.value->migration.rulesIdentityChanged,
          "normalized export reimports exactly without another change");
}

void invalid(const Json &blueprint, const RulesSnapshot &active,
             const SchemaSet &schema, std::string_view code = "invalid_text",
             std::string_view path = "/name") {
  const auto result = import_definition(blueprint.dump(), active, schema);
  require(!result.ok() && issue(result.issues, code, path),
          "invalid original name rejects at original path: " +
              Json(result.issues).dump());
}
} // namespace

int main() {
  using namespace test;
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;
  // Luna, under owner delegation 2026-11-23: trim design names, then use the
  // exact Unnamed design fallback from coherent active build-type defaults.
  suite.run("bundled factory labels and empty names are Unnamed design", [&] {
    for (const auto type : {BlankType::ship, BlankType::station}) {
      const bool station = type == BlankType::station;
      require(blank_blueprint(active, type).at("name") == "Unnamed design" &&
                  blank_blueprint(active, type).at("buildType") ==
                      (station ? "station" : "ship"),
              "both factory labels use exact decided text and retain type");
      for (const auto *name : {"", " ", "    "}) {
        auto input = blank(station ? "station" : "ship");
        input["name"] = name;
        auto expected = input;
        expected["name"] = "Unnamed design";
        normalized(input, expected, active, schema);
      }
    }
  });

  const std::vector<std::string> margins{
      " ", "\u00A0", "\u1680", "\u2000", "\u2001", "\u2002", "\u2003",
      "\u2004", "\u2005", "\u2006", "\u2007", "\u2008", "\u2009",
      "\u200A", "\u2028", "\u2029", "\u202F", "\u205F", "\u3000", "\uFEFF"};
  for (std::size_t i = 0; i < margins.size(); ++i) {
    suite.run("prototype Unicode margin " + std::to_string(i), [&] {
      for (const auto &raw : {margins[i] + "Keel" + margins[i], margins[i]}) {
        auto input = blank();
        input["name"] = raw;
        auto expected = input;
        expected["name"] = raw == margins[i] ? "Unnamed design" : "Keel";
        normalized(input, expected, active, schema);
      }
    });
  }

  suite.run("mixed margins trim while interior and non-trim bytes stay", [&] {
    auto input = blank();
    input["name"] = "\uFEFF \u00A0Keel  \u2003Beam\u2029\u3000 ";
    auto expected = input;
    expected["name"] = "Keel  \u2003Beam";
    normalized(input, expected, active, schema);
    for (const auto *name : {"Keel", "\u0085Keel\u0085", "\u200BKeel\u200B",
                             "e\u0301", "\u00E9"}) {
      input["name"] = name;
      normalized(input, input, active, schema);
    }
    input["decks"][0]["name"] = "  explicit deck  ";
    input["migrationNotes"] = Json::array({"  historical note  "});
    input["name"] = " Keel ";
    expected = input;
    expected["name"] = "Keel";
    normalized(input, expected, active, schema);
  });

  suite.run("every C0 and DEL rejects before trim at margins and interior", [&] {
    for (int value = 0; value <= 127; ++value) {
      if (value >= 32 && value != 127)
        continue;
      const char control = static_cast<char>(value);
      for (const auto &name : {std::string(1, control),
                               std::string(1, control) + "Keel",
                               std::string("Keel") + control,
                               std::string("Ke") + control + "el"}) {
        auto input = blank();
        input["name"] = name;
        invalid(input, active, schema);
      }
    }
  });

  suite.run("post-trim exact cap and coherent padded active defaults", [&] {
    auto input = blank();
    input["name"] = " " + std::string(80, 'n') + "\u3000";
    auto expected = input;
    expected["name"] = std::string(80, 'n');
    normalized(input, expected, active, schema);
    input["name"] = " " + std::string(81, 'n') + " ";
    invalid(input, active, schema, "field_limit");
    auto data = active.data();
    data["technicalLimits"]["maxBlueprintNameCharacters"] = 3;
    data["defaults"]["shipName"] = " \u00A0S\u3000 ";
    data["defaults"]["stationName"] = "\uFEFF T ";
    const auto loaded = load_rules(data.dump(), schema);
    require(loaded.ok(), "trimmed coherent defaults fit narrow active cap");
    for (const auto type : {BlankType::ship, BlankType::station}) {
      const bool station = type == BlankType::station;
      const auto fallback = station ? "T" : "S";
      require(blank_blueprint(*loaded.value, type).at("name") == fallback,
              "factory trims active padded design defaults");
      input = blank(station ? "station" : "ship");
      input["name"] = "\u3000 ";
      expected = input;
      expected["name"] = fallback;
      normalized(input, expected, *loaded.value, schema);
      input["name"] = " abc ";
      expected = input;
      expected["name"] = "abc";
      normalized(input, expected, *loaded.value, schema);
      input["name"] = " abcd ";
      invalid(input, *loaded.value, schema, "field_limit");
    }
    for (const auto *key : {"shipName", "stationName"})
      for (const auto *bad : {"", " \u3000", "\tS", "S\x7F", " abcd "}) {
        auto changed = data;
        changed["defaults"][key] = bad;
        const auto rejected = load_rules(changed.dump(), schema);
        require(!rejected.ok() && issue(rejected.issues, "invalid_rules",
                                       "/defaults/" + std::string(key)),
                "blank/control/overlong active design defaults reject");
      }
  });

  suite.run("recursive normalization preserves parent and child geometry", [&] {
    auto parent = blank("station");
    parent["name"] = " parent ";
    parent["decks"][0]["name"] = "  explicit deck  ";
    auto child = blank();
    child["name"] = " \u3000 ";
    child["migrationNotes"] = Json::array({"old child note"});
    parent["decks"][0]["modules"].push_back(
        {{"id", 9}, {"type", "shuttleDock"}, {"x", 3}, {"y", 4}, {"r", 1},
         {"craft", child}});
    parent["nextId"] = 10;
    auto expected = parent;
    expected["name"] = "parent";
    expected["decks"][0]["modules"][0]["craft"]["name"] = "Unnamed design";
    const auto result = import_definition(parent.dump(), active, schema);
    require(result.ok() && result.value->definition.at("blueprint") == expected,
            "recursive normalization changes only root and child names");
    require(name_change(result.value->migration, "/name") &&
                name_change(result.value->migration,
                            "/decks/0/modules/0/craft/name"),
            "both changed names have exact migration report paths");
    const auto again = import_definition(result.value->definition.dump(), active, schema);
    require(again.ok() && again.value->definition == result.value->definition &&
                again.value->migration.changes.empty(),
            "nested normalized names reimport idempotently");
    parent["decks"][0]["modules"][0]["craft"]["name"] = " \t ";
    invalid(parent, active, schema, "invalid_text",
            "/decks/0/modules/0/craft/name");
  });

  suite.run("wrapped v4 and historical v3 share name normalization", [&] {
    const auto base = import_definition(blank().dump(), active, schema);
    require(base.ok(), "real v4 wrapper source imports");
    auto wrapped = base.value->definition;
    wrapped["blueprint"]["name"] = "\u3000 Wrapped \u00A0";
    const auto result = import_definition(wrapped.dump(), active, schema);
    require(result.ok(), "wrapped v4 design name trims");
    wrapped["blueprint"]["name"] = "Wrapped";
    require(result.value->definition == wrapped &&
                name_change(result.value->migration, "/blueprint/name"),
            "wrapped normalization changes only name and reports wrapped path");
    const auto fixture = Json::parse(read("fixtures/v3/apsis-wren-definition-v3.json"));
    for (const bool wrapped_source : {false, true}) {
      auto source = wrapped_source ? fixture : fixture.at("blueprint");
      auto &blueprint = wrapped_source ? source["blueprint"] : source;
      blueprint["name"] = " Wren ";
      const auto old = blueprint;
      const auto migrated = import_definition(source.dump(), active, schema);
      require(migrated.ok(), "v3 source migrates and trims");
      const auto &next = migrated.value->definition.at("blueprint");
      require(next.at("name") == "Wren" &&
                  name_change(migrated.value->migration,
                              wrapped_source ? "/blueprint/name" : "/name"),
              "v3 names use precise normalization paths");
      auto before_fields = old;
      auto after_fields = next;
      for (const auto *field : {"schema", "name", "migrationNotes"}) {
        before_fields.erase(field);
        after_fields.erase(field);
      }
      require(before_fields == after_fields,
              "v3 trimming retains geometry, order, IDs, loads and other labels");
      const auto &old_notes = old.at("migrationNotes");
      const auto &new_notes = next.at("migrationNotes");
      require(new_notes.size() > old_notes.size() &&
                  std::equal(old_notes.begin(), old_notes.end(), new_notes.begin()) &&
                  std::find(new_notes.begin(), new_notes.end(),
                      "Migrated apsis-shipyard-v3 to apsis-shipyard-v4 with active rules; existing geometry, IDs, budget and loads retained; design names normalized.") != new_notes.end(),
              "new migration wording is exact; historical notes stay byte-exact");
      const auto again = import_definition(migrated.value->definition.dump(), active, schema);
      require(again.ok() && again.value->definition == migrated.value->definition &&
                  again.value->migration.changes.empty(),
              "v3 normalized export becomes stable v4 on reimport");
    }
  });

  suite.run("malformed UTF8 rejects before Unicode trimming", [&] {
    auto input = blank().dump();
    input.replace(input.find("Blank"), 5, "\xE2\x28\xA1");
    require(!import_definition(input, active, schema).ok(),
            "malformed UTF8 must not reach name trimming");
  });
  return suite.finish();
}
