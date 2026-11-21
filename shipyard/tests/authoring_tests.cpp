#include "test_support.hpp"

#include <apsis/shipyard/authoring.hpp>

#include <limits>

namespace {
using namespace test;

Json edited(Result<Json> result) {
  Json diagnostics = result.issues;
  require(result.ok(), "Authoring must succeed: " + diagnostics.dump());
  require(result.value->at("schema") == "apsis-shipyard-v4",
          "Authoring returns a normalized blueprint, not a definition wrapper");
  return std::move(*result.value);
}

Json normalized(const Json &source, const RulesSnapshot &active,
                const SchemaSet &schema) {
  const auto result = import_definition(canonical_json(source), active, schema);
  Json diagnostics = result.issues;
  require(result.ok(), "Test source must import: " + diagnostics.dump());
  return result.value->definition.at("blueprint");
}

Json deck(std::string name, std::optional<bool> generated, int id, int x) {
  Json result = {{"name", std::move(name)},
                 {"floors", {{std::to_string(x) + ",3", true}}},
                 {"modules", Json::array({{{"id", id},
                                           {"type", "shuttleSeats"},
                                           {"x", x},
                                           {"y", 3},
                                           {"r", 0}}})}};
  if (generated.has_value())
    result["nameGenerated"] = *generated;
  return result;
}

Json source(const RulesSnapshot &active, const SchemaSet &schema) {
  auto result = blank_blueprint(active);
  result["decks"] = Json::array({deck("Deck 1", true, 11, 2),
                                 deck("Deck 4", std::nullopt, 27, 7),
                                 deck("Deck 3", true, 43, 12)});
  result["nextId"] = 90;
  return normalized(result, active, schema);
}

void labels(const Json &blueprint, const Json &expected) {
  Json actual = Json::array();
  for (const auto &item : blueprint.at("decks"))
    actual.push_back(item.at("name"));
  require(actual == expected,
          "Names must match physical levels while retaining explicit labels: " +
              actual.dump());
}

void contents(const Json &actual, const Json &expected) {
  auto left = actual;
  auto right = expected;
  for (auto *item : {&left, &right}) {
    item->erase("name");
    item->erase("nameGenerated");
  }
  require(
      left == right,
      "A retained deck must preserve every module, ID, coordinate and floor");
}

void unchanged_metadata(const Json &actual, const Json &expected) {
  auto left = actual;
  auto right = expected;
  left.erase("decks");
  right.erase("decks");
  require(left == right,
          "Deck edits preserve nextId, loads, budget, type and other metadata");
}

void no_f20_values(const Json &value) {
  if (value.is_object()) {
    for (const auto *metric :
         {"acceleration", "range", "deltaV", "fuelToDryMassRatio",
          "burnSeconds", "fullBurnSeconds", "currentBurnSeconds",
          "exhaustVelocity"})
      require(!value.contains(metric), "Excluded F20 metrics must be absent, "
                                       "including null or numeric substitutes");
    for (const auto &child : value)
      no_f20_values(child);
  } else if (value.is_array()) {
    for (const auto &child : value)
      no_f20_values(child);
  }
}

void rejected(const Result<Json> &result, std::string_view code) {
  require(!result.ok(),
          "A failed edit must not expose a partially edited value");
  require(std::any_of(result.issues.begin(), result.issues.end(),
                      [&](const Issue &item) {
                        return item.code == code && item.severity == "error" &&
                               !item.path.empty() && !item.message.empty();
                      }),
          "Failure must carry a structured " + std::string(code) + " error");
}

void any_error(const Result<Json> &result) {
  require(!result.ok() &&
              std::any_of(result.issues.begin(), result.issues.end(),
                          [](const Issue &item) {
                            return item.severity == "error" &&
                                   !item.code.empty() && !item.message.empty();
                          }),
          "Invalid source or label must reject with structured errors");
}

RulesSnapshot changed_rules(Json data, const SchemaSet &schema) {
  auto result = load_rules(canonical_json(data), schema);
  Json diagnostics = result.issues;
  require(result.ok(), "Authored rules must load: " + diagnostics.dump());
  return std::move(*result.value);
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

  suite.run(
      "F04 blank defaults remain independent of preceding station sources",
      [&] {
        const auto before = blank_blueprint(active);
        const auto station = blank_blueprint(active, BlankType::station);
        require(station.at("buildType") == "station" &&
                    normalized(station, active, schema).at("buildType") ==
                        "station",
                "Explicit station mode survives ordinary import");
        const auto historical = import_definition(
            read("fixtures/v3/apsis-wayfarer-anchorage-definition-v3.json"),
            active, schema);
        require(
            historical.ok() &&
                historical.value->definition.at("blueprint").at("buildType") ==
                    "station",
            "An imported historical station must stay a station");
        const auto after = blank_blueprint(active);
        require(before == after &&
                    after == blank_blueprint(active, BlankType::ship) &&
                    after.at("buildType") == "ship",
                "Neither explicit nor imported stations may change the blank "
                "default");
      });

  suite.run(
      "F05 blank sources name every active minimum deck by its level", [&] {
        auto data = active.data();
        data["limits"]["minDecks"] = 3;
        data["defaults"]["deckNamePrefix"] = "Level %{n} ";
        const auto custom = changed_rules(data, schema);
        for (const auto type : {BlankType::ship, BlankType::station}) {
          const auto blueprint = blank_blueprint(custom, type);
          labels(blueprint, {"Level %{n} 1", "Level %{n} 2", "Level %{n} 3"});
          require(blueprint.at("decks").size() == 3 &&
                      blueprint.at("name") == data.at("defaults")
                                                  .at(type == BlankType::station
                                                          ? "stationName"
                                                          : "shipName"),
                  "Blank source must use active defaults and minDecks");
          for (const auto &item : blueprint.at("decks"))
            require(item.at("nameGenerated") == true &&
                        item.at("floors").empty() && item.at("modules").empty(),
                    "Each blank deck must be empty and explicitly automatic");
          require(normalized(blueprint, custom, schema) == blueprint,
                  "A multi-deck blank must import without changing provenance");
        }
      });

  suite.run(
      "F05 older v4 rules use the documented literal name prefix fallback",
      [&] {
        auto data = active.data();
        data["defaults"].erase("deckNamePrefix");
        data["defaults"]["deckName"] = " Legacy ";
        const auto legacy = changed_rules(data, schema);
        auto blueprint = blank_blueprint(legacy);
        labels(blueprint, {" Legacy  1"});
        blueprint =
            edited(insert_deck(canonical_json(blueprint), 0, legacy, schema));
        labels(blueprint, {" Legacy  1", " Legacy  2"});
      });

  suite.run(
      "F05 insertion at front middle and end renumbers only automatic decks",
      [&] {
        const auto original = source(active, schema);
        for (const std::size_t index : {0U, 1U, 3U}) {
          const auto result = edited(
              insert_deck(canonical_json(original), index, active, schema));
          const auto expected =
              index == 0   ? Json{"Deck 1", "Deck 2", "Deck 4", "Deck 4"}
              : index == 1 ? Json{"Deck 1", "Deck 2", "Deck 4", "Deck 4"}
                           : Json{"Deck 1", "Deck 4", "Deck 3", "Deck 4"};
          labels(result, expected);
          require(result.at("decks")[index].at("nameGenerated") == true &&
                      result.at("decks")[index].at("floors").empty() &&
                      result.at("decks")[index].at("modules").empty(),
                  "Insertion creates an empty automatically named deck");
          for (std::size_t old = 0; old < 3; ++old)
            contents(result.at("decks")[old < index ? old : old + 1],
                     original.at("decks")[old]);
          unchanged_metadata(result, original);
        }
      });

  suite.run(
      "F05 explicit inserted names are never inferred from their spelling",
      [&] {
        for (const auto &name : {std::string("Deck 1"), std::string(""),
                                 std::string("  Operations  ")}) {
          auto blueprint =
              edited(insert_deck(canonical_json(blank_blueprint(active)), 0,
                                 active, schema, name));
          require(
              !blueprint.at("decks")[0].value("nameGenerated", false),
              "An explicitly supplied name cannot have generated ownership");
          blueprint = edited(
              move_deck(canonical_json(blueprint), 0, 1, active, schema));
          labels(blueprint, {"Deck 1", name});
        }
      });

  suite.run(
      "F05 moves in both directions retain deck identity and module IDs", [&] {
        const auto original = source(active, schema);
        const auto down =
            edited(move_deck(canonical_json(original), 0, 2, active, schema));
        labels(down, {"Deck 4", "Deck 2", "Deck 3"});
        contents(down.at("decks")[0], original.at("decks")[1]);
        contents(down.at("decks")[1], original.at("decks")[2]);
        contents(down.at("decks")[2], original.at("decks")[0]);
        unchanged_metadata(down, original);
        const auto up =
            edited(move_deck(canonical_json(down), 2, 0, active, schema));
        require(up == original,
                "Moving a deck back restores its exact public blueprint");
        require(
            edited(move_deck(canonical_json(original), 1, 1, active, schema)) ==
                original,
            "Moving a deck to itself preserves an already normalized source");
      });

  suite.run(
      "F05 removal deletes only the selected deck and renumbers survivors",
      [&] {
        const auto original = source(active, schema);
        for (const std::size_t index : {0U, 1U, 2U}) {
          const auto result = edited(
              remove_deck(canonical_json(original), index, active, schema));
          labels(result, index == 0   ? Json{"Deck 4", "Deck 2"}
                         : index == 1 ? Json{"Deck 1", "Deck 2"}
                                      : Json{"Deck 1", "Deck 4"});
          for (std::size_t old = 0, now = 0; old < 3; ++old)
            if (old != index)
              contents(result.at("decks")[now++], original.at("decks")[old]);
          unchanged_metadata(result, original);
        }
      });

  suite.run(
      "F05 explicit rename clears ownership even for the current label", [&] {
        auto blueprint = source(active, schema);
        const auto original = blueprint;
        blueprint = edited(rename_deck(canonical_json(blueprint), 0, "Deck 1",
                                       active, schema));
        unchanged_metadata(blueprint, original);
        for (std::size_t index = 0; index < original.at("decks").size();
             ++index)
          contents(blueprint.at("decks")[index], original.at("decks")[index]);
        require(!blueprint.at("decks")[0].value("nameGenerated", false),
                "Renaming to identical text still makes the label explicit");
        blueprint =
            edited(move_deck(canonical_json(blueprint), 0, 2, active, schema));
        labels(blueprint, {"Deck 4", "Deck 2", "Deck 1"});
        blueprint = edited(rename_deck(canonical_json(blueprint), 2,
                                       std::nullopt, active, schema));
        contents(blueprint.at("decks")[2], original.at("decks")[0]);
        unchanged_metadata(blueprint, original);
        labels(blueprint, {"Deck 4", "Deck 2", "Deck 3"});
        require(blueprint.at("decks")[2].at("nameGenerated") == true,
                "Null optional rename restores generated ownership");
        blueprint =
            edited(move_deck(canonical_json(blueprint), 2, 0, active, schema));
        labels(blueprint, {"Deck 1", "Deck 4", "Deck 3"});
      });

  suite.run(
      "F05 import preserves true false and absent provenance without guessing",
      [&] {
        auto blueprint = source(active, schema);
        blueprint["decks"][0]["name"] = "Old automatic label";
        blueprint["decks"][1]["name"] = "Deck 1";
        blueprint["decks"][2]["name"] = "Deck 2";
        blueprint["decks"][2]["nameGenerated"] = false;
        const auto imported =
            import_definition(canonical_json(blueprint), active, schema);
        require(imported.ok() &&
                    imported.value->definition.at("blueprint") == blueprint,
                "Import must preserve provenance and text, including stale "
                "automatic labels");
        const auto bytes = canonical_json(imported.value->definition);
        const auto reimported = import_definition(bytes, active, schema);
        require(reimported.ok() &&
                    canonical_json(reimported.value->definition) == bytes,
                "Export/reimport must preserve every provenance state exactly");
        const auto moved = edited(move_deck(bytes, 2, 0, active, schema));
        labels(moved, {"Deck 2", "Deck 2", "Deck 1"});
        require(moved.at("decks")[0].at("nameGenerated") == false &&
                    moved.at("decks")[1].at("nameGenerated") == true &&
                    !moved.at("decks")[2].contains("nameGenerated"),
                "Editing a wrapper changes only automatic root labels");
      });

  suite.run(
      "F05 historical deck names and carried-craft labels survive root edits",
      [&] {
        const auto historical = import_definition(
            read("fixtures/v3/apsis-kestrel-tender-definition-v3.json"), active,
            schema);
        require(historical.ok(), "Historical Kestrel must import normally");
        auto blueprint = historical.value->definition.at("blueprint");
        for (auto &item : blueprint["decks"])
          for (auto &module : item["modules"])
            if (module.contains("craft")) {
              module["craft"]["decks"][0]["name"] = "Deck 99";
              module["craft"]["decks"][0]["nameGenerated"] = true;
            }
        blueprint = normalized(blueprint, active, schema);
        const auto moved =
            edited(move_deck(canonical_json(blueprint), 2, 0, active, schema));
        labels(moved, {"Flight deck", "Engineering", "Cargo deck"});
        require(moved.at("decks")[0] == blueprint.at("decks")[2] &&
                    moved.at("decks")[1] == blueprint.at("decks")[0] &&
                    moved.at("decks")[2] == blueprint.at("decks")[1],
                "Reordering historical decks preserves all nested craft and "
                "geometry");
        unchanged_metadata(moved, blueprint);
        const auto inserted =
            edited(insert_deck(canonical_json(moved), 1, active, schema));
        labels(inserted,
               {"Flight deck", "Deck 2", "Engineering", "Cargo deck"});
        require(edited(remove_deck(canonical_json(inserted), 1, active,
                                   schema)) == moved,
                "An inserted then removed deck must not alter historical "
                "labels or payloads");
      });

  suite.run(
      "F05 invalid indices return errors without partial results or mutation",
      [&] {
        const auto original = source(active, schema);
        const auto bytes = canonical_json(original);
        const auto saved = bytes;
        for (const std::size_t bad :
             {std::size_t{3}, std::numeric_limits<std::size_t>::max()}) {
          rejected(move_deck(bytes, bad, 0, active, schema),
                   "invalid_deck_index");
          rejected(move_deck(bytes, 0, bad, active, schema),
                   "invalid_deck_index");
          rejected(move_deck(bytes, bad, bad, active, schema),
                   "invalid_deck_index");
          rejected(remove_deck(bytes, bad, active, schema),
                   "invalid_deck_index");
          rejected(rename_deck(bytes, bad, "Keep", active, schema),
                   "invalid_deck_index");
        }
        rejected(insert_deck(bytes, 4, active, schema), "invalid_deck_index");
        rejected(insert_deck(bytes, std::numeric_limits<std::size_t>::max(),
                             active, schema),
                 "invalid_deck_index");
        require(bytes == saved &&
                    normalized(Json::parse(bytes), active, schema) == original,
                "Rejected edits leave the caller's source unchanged");
        require(edited(move_deck(bytes, 0, 0, active, schema)) == original,
                "A valid edit must still work after rejected requests");
      });

  suite.run(
      "F05 deck count limits use active minimum and maximum exactly", [&] {
        auto data = active.data();
        data["limits"]["minDecks"] = 2;
        data["limits"]["maxDecks"] = 3;
        const auto bounded = changed_rules(data, schema);
        auto blueprint = blank_blueprint(bounded);
        rejected(remove_deck(canonical_json(blueprint), 0, bounded, schema),
                 "deck_limit");
        blueprint =
            edited(insert_deck(canonical_json(blueprint), 2, bounded, schema));
        labels(blueprint, {"Deck 1", "Deck 2", "Deck 3"});
        rejected(insert_deck(canonical_json(blueprint), 1, bounded, schema),
                 "deck_limit");
        blueprint =
            edited(remove_deck(canonical_json(blueprint), 1, bounded, schema));
        labels(blueprint, {"Deck 1", "Deck 2"});
      });

  suite.run(
      "F05 label bounds and malformed input reject without hiding valid edits",
      [&] {
        const auto blueprint = blank_blueprint(active);
        const auto bytes = canonical_json(blueprint);
        const auto maximum =
            active.data().at("limits").at("maxDeckName").get<std::size_t>();
        const std::string boundary(maximum, 'X');
        const auto named =
            edited(rename_deck(bytes, 0, boundary, active, schema));
        labels(named, {boundary});
        for (const auto &bad :
             {boundary + "X", std::string("Bad\nlabel"),
              std::string("\xC3\x28"), std::string("\x80"),
              std::string("Bad\x7Flabel"), std::string("Bad\0label", 9)}) {
          any_error(rename_deck(bytes, 0, bad, active, schema));
          any_error(insert_deck(bytes, 0, active, schema, bad));
        }
        auto invalid = blueprint;
        invalid["decks"][0]["nameGenerated"] = "true";
        any_error(move_deck(canonical_json(invalid), 0, 0, active, schema));
        any_error(insert_deck("{", 0, active, schema));
        any_error(move_deck("{", 0, 0, active, schema));
        any_error(remove_deck("{", 0, active, schema));
        any_error(rename_deck("{", 0, "Valid", active, schema));
        InputLimits limits;
        limits.maxBytes = bytes.size() - 1;
        any_error(move_deck(bytes, 0, 0, active, schema, limits));
        limits.maxBytes = bytes.size();
        require(edited(move_deck(bytes, 0, 0, active, schema, limits)) ==
                    blueprint,
                "The exact input-byte boundary must remain valid");
      });

  suite.run(
      "F05 generated prefix limits are checked through public rules loading",
      [&] {
        auto data = active.data();
        const auto limit =
            data.at("limits").at("maxDeckName").get<std::size_t>();
        data["defaults"]["deckNamePrefix"] = std::string(limit - 1, 'P');
        const auto boundary = changed_rules(data, schema);
        require(blank_blueprint(boundary)
                        .at("decks")[0]
                        .at("name")
                        .get<std::string>()
                        .size() == limit,
                "A generated label at the active limit must be accepted");
        for (const auto &bad :
             {std::string(limit, 'P'), std::string("Bad\nprefix ")}) {
          data["defaults"]["deckNamePrefix"] = bad;
          const auto result = load_rules(canonical_json(data), schema);
          require(!result.ok() && !result.issues.empty(),
                  "Invalid prefixes must fail before authoring can emit "
                  "invalid names");
        }
      });

  suite.run("F20 authoring a blank retains structured pending omissions", [&] {
    const auto result =
        insert_deck(canonical_json(blank_blueprint(active)), 0, active, schema);
    require(
        result.ok() &&
            std::any_of(result.issues.begin(), result.issues.end(),
                        [](const Issue &item) {
                          return item.code == "pending_F20" &&
                                 item.severity == "warning" &&
                                 item.details.at("finding") == "F20" &&
                                 !item.details.at("evaluations").empty();
                        }),
        "Authoring must carry the ordinary nonblocking structured F20 report");
    const auto imported =
        import_definition(canonical_json(*result.value), active, schema);
    require(imported.ok(),
            "Edited blank remains importable without resolving F20");
    no_f20_values(imported.value->definition.at("derived"));
  });

  return suite.finish();
}
