#include "test_support.hpp"

#include <array>
#include <tuple>

namespace {
using namespace test;

// Luna, under owner delegation 2026-11-23. These literal expectations are
// independent of the importer retention implementation.
constexpr const char *notice = "Older migration notes were dropped.";
constexpr const char *compact = "\xE2\x80\xA6";
constexpr const char *identity_note =
    "Imported rule identity differed; active v4 rules were applied and direct totals recomputed.";
constexpr const char *v3_note =
    "Migrated apsis-shipyard-v3 to apsis-shipyard-v4 with active rules; existing geometry, IDs, names, budget and loads retained.";

RulesSnapshot policy(const RulesSnapshot &active, const SchemaSet &schema,
                     int count, int length = 300) {
  auto authored = active.data();
  authored["limits"]["maxMigrationNotes"] = count;
  authored["limits"]["maxNoteLength"] = length;
  auto loaded = load_rules(authored.dump(), schema);
  require(loaded.ok(), "Positive note count and length bounds remain valid rules");
  return std::move(*loaded.value);
}

std::size_t warnings(const Result<ImportedDefinition> &result,
                     const std::string &path) {
  return static_cast<std::size_t>(std::count_if(
      result.issues.begin(), result.issues.end(), [&](const Issue &entry) {
        return entry.code == "migration_note_limit" && entry.path == path;
      }));
}

void warning(const Result<ImportedDefinition> &result, const std::string &path,
             int limit, int dropped, int retained) {
  require(warnings(result, path) == 1,
          "Each changed craft note list has exactly one count warning: " + path);
  for (const auto &entry : result.issues) {
    if (entry.code != "migration_note_limit" || entry.path != path) continue;
    require(entry.severity == "warning" &&
                entry.message == "Older migration notes were dropped; the newest notes and one notice were retained." &&
                entry.details == Json{{"limit", limit}, {"dropped", dropped}, {"retained", retained}},
            "Count warning pins full explanation and independent retention arithmetic");
  }
}

void stable(const Result<ImportedDefinition> &result, const RulesSnapshot &active,
            const SchemaSet &schema) {
  require(result.ok(), "A bounded note result must export a definition");
  const auto serialized = canonical_json(result.value->definition);
  const auto again = import_definition(serialized, active, schema);
  require(again.ok() && again.value->migration.changes.empty() &&
              !again.value->migration.rulesIdentityChanged,
          "Canonical notes reimport without another migration change");
  require(canonical_json(again.value->definition) == serialized,
          "The first reimport preserves the entire canonical definition byte for byte");
  require(std::none_of(again.issues.begin(), again.issues.end(), [](const Issue &entry) {
    return entry.code == "migration_note_limit";
  }), "Stable exported notes cannot repeat the count warning");
}

Result<ImportedDefinition> expect(Json source, const Json &expected,
                                  const RulesSnapshot &active,
                                  const SchemaSet &schema, bool has_notice = false) {
  const auto result = import_definition(source.dump(), active, schema);
  require(result.ok(), "Ordinary count overflow must not reject a valid source");
  source["migrationNotes"] = expected;
  if (has_notice) source["migrationNotesDropped"] = true;
  require(result.value->definition.at("blueprint") == source,
          "Only the exact independently expected notes may change");
  require(expected.size() <= active.data().at("limits").at("maxMigrationNotes").get<std::size_t>(),
          "The history notice consumes one slot inside the active cap");
  auto no_notes = source;
  no_notes["migrationNotes"] = Json::array();
  no_notes.erase("migrationNotesDropped");
  const auto bare = import_definition(no_notes.dump(), active, schema);
  require(bare.ok() && bare.value->definition.at("derived") == result.value->definition.at("derived"),
          "History retention must not alter material, IDs, loads or direct totals");
  stable(result, active, schema);
  return result;
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto three = policy(active, schema, 3);
  Suite suite;

  suite.run("exact cap and below-cap ordinary notes retain order and duplicates", [&] {
    for (const auto &notes : {Json::array(), Json::array({"A"}),
                              Json::array({"A", "B", "C"}), Json::array({"A", "A", "B"}),
                              Json::array({"A", compact}), Json::array({notice, "A", compact})}) {
      auto source = blank();
      source["migrationNotes"] = notes;
      const auto result = expect(source, notes, three, schema);
      require(warnings(result, "/migrationNotes") == 0 && result.value->migration.changes.empty(),
              "A fitting ordinary list is preserved without a warning or report change");
    }
  });

  suite.run("one over and far over retain only the newest suffix plus notice", [&] {
    auto source = blank();
    source["decks"][0]["floors"]["2,3"] = true;
    source["migrationNotes"] = Json::array({"A", "B", "C", "D"});
    warning(expect(source, Json::array({notice, "C", "D"}), three, schema, true),
            "/migrationNotes", 3, 2, 2);
    source["migrationNotes"] = Json::array();
    for (int i = 0; i < 1000; ++i)
      source["migrationNotes"].push_back("history-" + std::to_string(i));
    warning(expect(source, Json::array({notice, "history-998", "history-999"}), three, schema, true),
            "/migrationNotes", 3, 998, 2);
    source["migrationNotes"] = Json::array({"A", "C", "B", "B"});
    warning(expect(source, Json::array({notice, "B", "B"}), three, schema, true),
            "/migrationNotes", 3, 2, 2);
    source["migrationNotes"] = Json::array({"A", "B", notice, compact});
    warning(expect(source, Json::array({notice, notice, compact}), three, schema, true),
            "/migrationNotes", 3, 2, 2);
  });

  suite.run("existing notices canonicalize once and preserve evidence of lost history", [&] {
    auto source = blank();
    source["migrationNotes"] = Json::array({"A", "B", "C", "D"});
    const auto generated = expect(source, Json::array({notice, "C", "D"}), three, schema, true);
    const auto &exported = generated.value->definition.at("blueprint");
    warning(expect(exported, Json::array({notice, "D"}), policy(active, schema, 2), schema, true),
            "/migrationNotes", 2, 1, 1);
    warning(expect(exported, Json::array({notice}), policy(active, schema, 1), schema, true),
            "/migrationNotes", 1, 2, 0);
    const auto fitting = expect(exported, Json::array({notice, "C", "D"}),
                                policy(active, schema, 4), schema, true);
    require(fitting.value->migration.changes.empty() && warnings(fitting, "/migrationNotes") == 0,
            "A real prior notice remains stable even with a short suffix");
  });

  suite.run("notice provenance requires the documented leading text", [&] {
    auto source = blank();
    source["migrationNotesDropped"] = true;
    auto result = import_definition(source.dump(), three, schema);
    require(!result.ok() && issue(result.issues, "schema_error", "/migrationNotes"),
            "An empty list cannot carry dropped-note provenance");
    source["migrationNotes"] = Json::array({"A", notice});
    result = import_definition(source.dump(), three, schema);
    require(!result.ok() && issue(result.issues, "schema_error", "/migrationNotes/0"),
            "Provenance only applies to a valid notice in the first slot");
    source["migrationNotes"] = Json::array({compact, notice});
    result = import_definition(source.dump(), policy(active, schema, 3, 1), schema);
    require(!result.ok() && issue(result.issues, "field_limit", "/migrationNotes/1"),
            "An ordinary note that spells out the notice still has a text limit");
  });

  suite.run("caps one and two reserve a bounded history slot", [&] {
    auto source = blank();
    source["migrationNotes"] = Json::array({"A", "B", "C"});
    warning(expect(source, Json::array({notice}), policy(active, schema, 1), schema, true),
            "/migrationNotes", 1, 3, 0);
    warning(expect(source, Json::array({notice, "C"}), policy(active, schema, 2), schema, true),
            "/migrationNotes", 2, 2, 1);
    source["migrationNotes"] = Json::array({"A"});
    const auto result = expect(source, Json::array({"A"}), policy(active, schema, 1), schema);
    require(warnings(result, "/migrationNotes") == 0, "Cap one still retains one fitting ordinary note");
  });

  suite.run("full notice exact length boundary and compact scalar obey active rules", [&] {
    require(std::string(notice).size() == 35, "Independent ASCII notice length is35");
    for (const auto &[length, marker] : std::array<std::pair<int, const char *>, 4>{
             {{35, notice}, {34, compact}, {1, compact}, {300, notice}}}) {
      auto source = blank();
      source["migrationNotes"] = Json::array({"A", "B", "C", "D"});
      warning(expect(source, Json::array({marker, "C", "D"}), policy(active, schema, 3, length), schema, true),
              "/migrationNotes", 3, 2, 2);
    }
    for (const auto &[old_length, length, input_marker, expected_marker] :
         std::array<std::tuple<int, int, const char *, const char *>, 2>{
             {{35, 1, notice, compact}, {1, 35, compact, notice}}}) {
      auto source = blank();
      source["migrationNotes"] = Json::array({"A", "B", "C"});
      const auto generated = expect(source, Json::array({input_marker, "C"}),
                                    policy(active, schema, 2, old_length), schema, true);
      warning(expect(generated.value->definition.at("blueprint"), Json::array({expected_marker, "C"}),
                     policy(active, schema, 2, length), schema, true),
              "/migrationNotes", 2, 0, 1);
    }
    auto source = blank();
    source["migrationNotes"] = Json::array({"A", "B", "\xF0\x9F\x9A\x80"});
    warning(expect(source, Json::array({compact, "\xF0\x9F\x9A\x80"}),
                   policy(active, schema, 2, 1), schema, true),
            "/migrationNotes", 2, 2, 1);
  });

  suite.run("nested craft retain independent note suffixes and warning paths", [&] {
    auto parent = blank();
    auto child = blank();
    parent["migrationNotes"] = Json::array({"P0", "P1", "P2", "P3"});
    child["migrationNotes"] = Json::array({"C0", "C1", "C2", "C3", "C4"});
    parent["decks"][0]["modules"].push_back(
        {{"id", 1}, {"type", "shuttleDock"}, {"x", 0}, {"y", 0}, {"r", 0}, {"craft", child}});
    parent["nextId"] = 2;
    const auto result = import_definition(parent.dump(), three, schema);
    require(result.ok(), "Real carried craft with overflowing notes must import");
    auto expected = parent;
    expected["migrationNotes"] = Json::array({notice, "P2", "P3"});
    expected["migrationNotesDropped"] = true;
    expected["decks"][0]["modules"][0]["craft"]["migrationNotes"] = Json::array({notice, "C3", "C4"});
    expected["decks"][0]["modules"][0]["craft"]["migrationNotesDropped"] = true;
    require(result.value->definition.at("blueprint") == expected,
            "Parent and child retain exact independent newest suffixes without other edits");
    warning(result, "/migrationNotes", 3, 2, 2);
    warning(result, "/decks/0/modules/0/craft/migrationNotes", 3, 3, 2);
    stable(result, three, schema);
  });

  suite.run("evicted ordinary invalid notes still reject at original input paths", [&] {
    const auto narrow = policy(active, schema, 2, 3);
    for (const auto &invalid : {Json("long"), Json(notice), Json(std::string("A\n")),
                                Json(std::string("A\x7F")), Json(std::string("A\0", 2)), Json(7)}) {
      auto source = blank();
      source["migrationNotes"] = Json::array({"A", "B", "C", "D", invalid, "E", "F", "G"});
      const auto result = import_definition(source.dump(), narrow, schema);
      const auto code = invalid.is_number() ? "schema_error"
          : (invalid == "long" || invalid == notice) ? "field_limit" : "invalid_text";
      require(!result.ok() && issue(result.issues, code, "/migrationNotes/4"),
              "Retention cannot conceal an invalid note at its original index4");
    }
    auto nested = blank();
    auto child = blank();
    child["migrationNotes"] = Json::array({"A", "B", "long", "C", "D", "E"});
    nested["decks"][0]["modules"].push_back(
        {{"id", 1}, {"type", "shuttleDock"}, {"x", 0}, {"y", 0}, {"r", 0}, {"craft", child}});
    nested["nextId"] = 2;
    const auto result = import_definition(nested.dump(), narrow, schema);
    require(!result.ok() && issue(result.issues, "field_limit",
                                 "/decks/0/modules/0/craft/migrationNotes/2"),
            "Nested invalid old notes retain their original child index");
    auto scalar = blank();
    scalar["migrationNotes"] = Json::array({"e\xCC\x81", "A", "B"});
    const auto scalar_result = import_definition(scalar.dump(), policy(active, schema, 2, 1), schema);
    require(!scalar_result.ok() && issue(scalar_result.issues, "field_limit", "/migrationNotes/0"),
            "Ordinary note limits retain scalar counting even for an evicted combining sequence");
    scalar["migrationNotes"] = Json::array({"UTF8_BAD", "A", "B"});
    auto invalid_bytes = scalar.dump();
    invalid_bytes.replace(invalid_bytes.find("UTF8_BAD"), 8, std::string(1, '\xC3'));
    const auto invalid_utf8 = import_definition(invalid_bytes, narrow, schema);
    require(!invalid_utf8.ok() && issue(invalid_utf8.issues, "invalid_json", ""),
            "Invalid UTF8 in old notes rejects before retention");
  });

  suite.run("invalid note amplification retains the shared terminal128 issue cap", [&] {
    auto source = blank();
    source["migrationNotes"] = Json::array();
    for (int i = 0; i < 200; ++i) source["migrationNotes"].push_back("long");
    const auto result = import_definition(source.dump(), policy(active, schema, 2, 3), schema);
    require(!result.ok() && result.issues.size() == 128,
            "Invalid old notes still stop at exactly127 findings plus a marker");
    require(std::count_if(result.issues.begin(), result.issues.end(), [](const Issue &entry) {
      return entry.code == "field_limit" && entry.severity == "error";
    }) == 127, "Culling cannot replace the bounded original validation prefix");
    require(std::count_if(result.issues.begin(), result.issues.end(), [](const Issue &entry) {
      return entry.code == "issues_truncated" && entry.path.empty() && entry.severity == "error" &&
             entry.details.at("limit") == 128 && entry.details.at("suppressedAtLeast").get<int>() >= 1;
    }) == 1, "Exactly one root terminal marker survives note amplification");
  });

  suite.run("v3 and stale rule identity append newest automatic notes before retention", [&] {
    auto source = blank();
    source["schema"] = "apsis-shipyard-v3";
    source["migrationNotes"] = Json::array({"A", "B", "C"});
    const auto migrated = import_definition(source.dump(), three, schema);
    require(migrated.ok() && migrated.value->migration.rulesIdentityChanged &&
                migrated.value->definition.at("blueprint").at("migrationNotes") ==
                    Json::array({notice, v3_note, identity_note}),
            "The newest v3 and active-rule automatic notes survive a full input list");
    warning(migrated, "/migrationNotes", 3, 3, 2);
    stable(migrated, three, schema);
    const auto original = import_definition(blank().dump(), three, schema);
    require(original.ok(), "A real current v4 wrapper must be available");
    auto stale = original.value->definition;
    stale["rulesSha256"] = std::string(64, '0');
    stale["blueprint"]["migrationNotes"] = Json::array({"A", "B", "C"});
    const auto imported = import_definition(stale.dump(), three, schema);
    require(imported.ok() && imported.value->migration.rulesIdentityChanged &&
                imported.value->definition.at("blueprint").at("migrationNotes") ==
                    Json::array({notice, "C", identity_note}),
            "The fresh rule-identity note takes its newest bounded slot");
    warning(imported, "/blueprint/migrationNotes", 3, 2, 2);
    stable(imported, three, schema);
    source["migrationNotes"] = Json::array({"A", "B", "C"});
    const auto narrow = policy(active, schema, 3, 3);
    const auto text_limited = import_definition(source.dump(), narrow, schema);
    require(text_limited.ok() && text_limited.value->definition.at("blueprint").at("migrationNotes") ==
                                   Json::array({"A", "B", "C"}) &&
                warnings(text_limited, "/migrationNotes") == 2 &&
                !text_limited.value->migration.changes.empty(),
            "Too-long automatic notes remain explained in the report without breaking ordinary text bounds");
    stable(text_limited, narrow, schema);
  });

  suite.run("a stale wrapper ensures the rule identity explanation survives retention", [&] {
    const auto original = import_definition(blank().dump(), three, schema);
    require(original.ok(), "A current v4 wrapper must be available");
    auto stale = original.value->definition;
    stale["rulesSha256"] = std::string(64, '0');
    const std::array cases{
        std::pair{Json::array({identity_note, "A", "B", "C"}),
                  Json::array({notice, "C", identity_note})},
        std::pair{Json::array({identity_note, "A", "B"}),
                  Json::array({identity_note, "A", "B"})},
        std::pair{Json::array({"A", "B", identity_note, "C"}),
                  Json::array({notice, identity_note, "C"})},
    };
    for (const auto &[notes, expected] : cases) {
      stale["blueprint"]["migrationNotes"] = notes;
      const auto imported = import_definition(stale.dump(), three, schema);
      require(imported.ok() && imported.value->migration.rulesIdentityChanged &&
                  imported.value->definition.at("blueprint").at("migrationNotes") == expected,
              "The current rule change keeps its explanation after retention: " + notes.dump());
      stable(imported, three, schema);
    }
  });

  suite.run("an exactly fitting rule identity note survives reimport", [&] {
    const auto exact = policy(active, schema, 3, 91);
    const auto original = import_definition(blank().dump(), exact, schema);
    require(original.ok(), "A current v4 wrapper must import at the exact note limit");
    auto stale = original.value->definition;
    stale["rulesSha256"] = std::string(64, '0');
    const auto imported = import_definition(stale.dump(), exact, schema);
    require(imported.ok() && imported.value->migration.rulesIdentityChanged &&
                imported.value->definition.at("blueprint").at("migrationNotes") ==
                    Json::array({identity_note}),
            "The 91-character automatic note fits the 91-character limit");
    stable(imported, exact, schema);
  });

  return suite.finish();
}
