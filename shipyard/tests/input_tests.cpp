#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace test;

Json module(std::string type = "cockpit", int id = 1) {
  return {{"id", id}, {"type", std::move(type)}, {"x", 0}, {"y", 0}, {"r", 0}};
}

Json one_module() {
  auto blueprint = blank();
  blueprint["decks"][0]["modules"].push_back(module());
  blueprint["nextId"] = 2;
  return blueprint;
}

void rejected(const Json &input, const RulesSnapshot &active,
              const SchemaSet &schema, const std::string &code,
              const std::string &path) {
  const auto result = import_definition(input.dump(), active, schema);
  require(!result.ok(), "invalid input must not produce a definition: " + path);
  require(issue(result.issues, code, path), "expected " + code + " at " + path);
  require(std::any_of(result.issues.begin(), result.issues.end(),
                      [&](const Issue &found) {
                        return found.code == code && found.path == path &&
                               found.severity == "error" &&
                               !found.message.empty();
                      }),
          "rejection needs a readable field error: " + path);
}

void preserved(const Json &input, const RulesSnapshot &active,
               const SchemaSet &schema, const std::string &field) {
  const auto result = import_definition(input.dump(), active, schema);
  require(result.ok(), "valid boundary input must import: " + field);
  require(result.value->definition.at("blueprint").at(field) == input.at(field),
          "input must be retained without rounding, truncation, or clamping: " +
              field);
}

std::string repeated(const std::string &text, std::size_t count) {
  std::string result;
  for (std::size_t i = 0; i < count; ++i)
    result += text;
  return result;
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema = schemas();
  const auto active = rules(schema);

  suite.run(
      "F09 name limits count Unicode scalar values and retain exact labels",
      [&] {
        require(active.data()
                        .at("technicalLimits")
                        .at("maxBlueprintNameCharacters") == 80,
                "owner-selected name cap must be 80 scalar values");
        const std::vector<std::string> at_limit = {
            std::string(80, 'a'),
            repeated("\xF0\x9F\x9A\x80",
                     80),              // Non-BMP scalar, four UTF-8 bytes.
            repeated("e\xCC\x81", 40), // Two scalars per combining sequence.
            repeated("\xC3\xA9", 80),
            " " + std::string(78, 'a') + " ",
        };
        for (const auto &name : at_limit) {
          auto blueprint = blank();
          blueprint["name"] = name;
          preserved(blueprint, active, schema, "name");
          blueprint["name"] = name + "x";
          rejected(blueprint, active, schema, "field_limit", "/name");
        }
      });

  suite.run(
      "F09 exact budget sanity cap never rounds or admits epsilon overage",
      [&] {
        require(active.data().at("technicalLimits").at("maxBudgetInputCr") ==
                    100000000,
                "owner-selected budget input sanity cap must be 100000000 cr");
        for (const auto &budget :
             std::vector<Json>{0, 99999999, 100000000, 100000000.0}) {
          auto blueprint = blank();
          blueprint["budget"] = budget;
          preserved(blueprint, active, schema, "budget");
        }
        auto blueprint = blank();
        blueprint["budget"] = 100000001;
        rejected(blueprint, active, schema, "field_limit", "/budget");
        // The next representable value is less than numeric.epsilon over the
        // cap.
        blueprint["budget"] = std::nextafter(
            100000000.0, std::numeric_limits<double>::infinity());
        rejected(blueprint, active, schema, "schema_error", "/budget");
        blueprint["budget"] = -1;
        rejected(blueprint, active, schema, "schema_error", "/budget");
      });

  suite.run("F22a UTF-8 name boundaries preserve complete graphemes", [&] {
    // Each repeated character is one grapheme. U+0080 is a C1 control;
    // the public text contract rejects C0 and DEL, not C1. U+0100 is a
    // precomposed letter. Neither needs a combining-sequence interpretation.
    for (const auto &grapheme :
         {std::string("\xC2\x80"), std::string("\xC4\x80")}) {
      for (const std::size_t count : {1, 79, 80}) {
        auto blueprint = blank();
        blueprint["name"] = repeated(grapheme, count);
        preserved(blueprint, active, schema, "name");
      }
      auto blueprint = blank();
      blueprint["name"] = repeated(grapheme, 81);
      rejected(blueprint, active, schema, "field_limit", "/name");
    }

    auto blueprint = blank();
    blueprint["name"] = "A B" + repeated("\xC4\x80", 77);
    preserved(blueprint, active, schema, "name");
  });

  suite.run(
      "technical input caps come from the active rules in both directions",
      [&] {
        auto changed = active.data();
        changed["technicalLimits"]["maxBlueprintNameCharacters"] = 100;
        changed["technicalLimits"]["maxBudgetInputCr"] = 200000000;
        const auto wider = load_rules(changed.dump(), schema);
        require(wider.ok(), "rules with wider technical caps must load");
        auto blueprint = blank();
        blueprint["name"] = std::string(81, 'n');
        blueprint["budget"] = 100000001;
        preserved(blueprint, *wider.value, schema, "name");
        preserved(blueprint, *wider.value, schema, "budget");

        changed["technicalLimits"]["maxBlueprintNameCharacters"] = 3;
        changed["technicalLimits"]["maxBudgetInputCr"] = 7;
        changed["defaults"]["shipName"] = "S";
        changed["defaults"]["stationName"] = "T";
        changed["defaults"]["budget"] = 7;
        const auto narrower = load_rules(changed.dump(), schema);
        require(narrower.ok(),
                "rules with coherent narrower technical caps must load");
        blueprint["name"] = "abc";
        blueprint["budget"] = 7;
        preserved(blueprint, *narrower.value, schema, "name");
        preserved(blueprint, *narrower.value, schema, "budget");
        blueprint["name"] = "abcd";
        rejected(blueprint, *narrower.value, schema, "field_limit", "/name");
        blueprint["name"] = "abc";
        blueprint["budget"] = 8;
        rejected(blueprint, *narrower.value, schema, "field_limit", "/budget");
      });

  suite.run(
      "a planning budget below computed cost is accepted without affordability "
      "enforcement",
      [&] {
        auto blueprint = one_module();
        blueprint["budget"] = 0;
        const auto result = import_definition(blueprint.dump(), active, schema);
        require(
            result.ok(),
            "a zero planning budget must not prevent an inspectable import");
        require(result.value->definition.at("blueprint").at("budget") == 0,
                "budget must not be raised to computed cost");
        require(
            result.value->definition.at("derived").at("cost").get<double>() > 0,
            "test design must actually cost more than its budget");
        require(std::none_of(result.issues.begin(), result.issues.end(),
                             [](const Issue &found) {
                               return found.severity == "error";
                             }),
                "affordability must not become a shipyard import error");
      });

  suite.run(
      "unknown authoritative fields and unsupported blueprint schemas reject "
      "clearly",
      [&] {
        using Mutation = std::pair<std::string, std::function<void(Json &)>>;
        const std::vector<Mutation> mutations = {
            {"/unknown", [](Json &b) { b["unknown"] = 0; }},
            {"/loads/unknown", [](Json &b) { b["loads"]["unknown"] = 0; }},
            {"/decks/0/unknown", [](Json &b) { b["decks"][0]["unknown"] = 0; }},
            {"/decks/0/modules/0/unknown",
             [](Json &b) { b["decks"][0]["modules"][0]["unknown"] = 0; }},
        };
        for (const auto &[path, mutate] : mutations) {
          auto blueprint = one_module();
          mutate(blueprint);
          rejected(blueprint, active, schema, "schema_error", path);
        }
        auto blueprint = blank();
        blueprint["schema"] = "apsis-shipyard-v5";
        rejected(blueprint, active, schema, "unsupported_schema", "/schema");
        blueprint.erase("schema");
        rejected(blueprint, active, schema, "unsupported_schema", "/schema");
        blueprint = blank();
        blueprint.erase("loads");
        rejected(blueprint, active, schema, "schema_error", "/loads");
        blueprint = blank();
        blueprint["decks"] = Json::object();
        rejected(blueprint, active, schema, "schema_error", "/decks");
      });

  suite.run("module IDs and nextId enforce exact integer and active bounds",
            [&] {
              auto blueprint = one_module();
              blueprint["decks"][0]["modules"][0]["id"] = 1000000000;
              blueprint["nextId"] = 1000000001;
              preserved(blueprint, active, schema, "decks");
              blueprint["decks"][0]["modules"][0]["id"] = 1000000001;
              rejected(blueprint, active, schema, "invalid_id",
                       "/decks/0/modules/0/id");
              blueprint = one_module();
              blueprint["nextId"] = 1000000002;
              rejected(blueprint, active, schema, "invalid_next_id", "/nextId");
              blueprint["nextId"] = 1;
              rejected(blueprint, active, schema, "invalid_next_id", "/nextId");
              blueprint = one_module();
              blueprint["decks"][0]["modules"].push_back(module("cockpit", 1));
              rejected(blueprint, active, schema, "invalid_id",
                       "/decks/0/modules/1/id");
              blueprint = one_module();
              blueprint["decks"][0]["modules"][0]["id"] = 1.0;
              blueprint["nextId"] = 2.0;
              preserved(blueprint, active, schema, "decks");
              blueprint["decks"][0]["modules"][0]["id"] = 1.0000001;
              rejected(blueprint, active, schema, "schema_error",
                       "/decks/0/modules/0/id");
              blueprint["decks"][0]["modules"][0]["id"] = 0;
              rejected(blueprint, active, schema, "schema_error",
                       "/decks/0/modules/0/id");
              blueprint["decks"][0]["modules"][0]["id"] = 9007199254740992ULL;
              rejected(blueprint, active, schema, "invalid_id",
                       "/decks/0/modules/0/id");
            });

  suite.run(
      "coordinates rotations types and interfaces use active catalogue "
      "references",
      [&] {
        auto blueprint = one_module();
        const auto width = active.data().at("grid").at("w").get<int>();
        const auto height = active.data().at("grid").at("h").get<int>();
        const auto rotations = active.data().at("rotations").size();
        blueprint["decks"][0]["modules"][0]["x"] = width - 1;
        blueprint["decks"][0]["modules"][0]["y"] = height - 1;
        blueprint["decks"][0]["modules"][0]["r"] = rotations - 1;
        blueprint["decks"][0]["floors"][std::to_string(width - 1) + "," +
                                        std::to_string(height - 1)] = true;
        preserved(blueprint, active, schema, "decks");
        for (const auto &[axis, bound] :
             std::vector<std::pair<std::string, int>>{{"x", width},
                                                      {"y", height}}) {
          auto invalid = one_module();
          invalid["decks"][0]["modules"][0][axis] = bound;
          rejected(invalid, active, schema, "invalid_coordinate",
                   "/decks/0/modules/0/" + axis);
          invalid["decks"][0]["modules"][0][axis] = -1;
          rejected(invalid, active, schema, "schema_error",
                   "/decks/0/modules/0/" + axis);
        }
        blueprint = one_module();
        blueprint["decks"][0]["modules"][0]["r"] = rotations;
        rejected(blueprint, active, schema, "invalid_rotation",
                 "/decks/0/modules/0/r");
        blueprint["decks"][0]["modules"][0]["r"] = 0.0000001;
        rejected(blueprint, active, schema, "schema_error",
                 "/decks/0/modules/0/r");
        blueprint = one_module();
        blueprint["decks"][0]["modules"][0]["type"] = "unknownPart";
        rejected(blueprint, active, schema, "invalid_part_type",
                 "/decks/0/modules/0/type");
        blueprint = one_module();
        blueprint["decks"][0]["modules"][0]["interface"] = "standard";
        rejected(blueprint, active, schema, "invalid_interface",
                 "/decks/0/modules/0/interface");
        blueprint["decks"][0]["modules"][0]["type"] = "airlock";
        blueprint["decks"][0]["modules"][0]["interface"] = "unknownInterface";
        rejected(blueprint, active, schema, "invalid_interface",
                 "/decks/0/modules/0/interface");
        blueprint = blank();
        blueprint["decks"][0]["floors"][std::to_string(width) + ",0"] = true;
        rejected(blueprint, active, schema, "invalid_coordinate",
                 "/decks/0/floors/" + std::to_string(width) + ",0");
        blueprint = blank();
        blueprint["decks"][0]["floors"]["01,0"] = true;
        rejected(blueprint, active, schema, "schema_error",
                 "/decks/0/floors/01,0");
        blueprint = blank();
        blueprint["decks"][0]["floors"]["0,0"] = false;
        rejected(blueprint, active, schema, "schema_error",
                 "/decks/0/floors/0,0");
      });

  suite.run("load fractions have exact bounds and retain input precision", [&] {
    for (const auto *field : {"cargo", "jumpFuel", "sublightFuel"}) {
      for (double fraction : {0.0, 1.0, 0.123456789012345}) {
        auto blueprint = blank();
        blueprint["loads"][field] = fraction;
        preserved(blueprint, active, schema, "loads");
      }
      for (double fraction :
           {std::nextafter(0.0, -1.0), std::nextafter(1.0, 2.0)}) {
        auto blueprint = blank();
        blueprint["loads"][field] = fraction;
        rejected(blueprint, active, schema, "schema_error",
                 "/loads/" + std::string(field));
      }
    }
  });

  suite.run("deck counts and carried nesting observe active rule limits", [&] {
    auto blueprint = blank();
    const auto deck = blueprint.at("decks")[0];
    const auto maximum =
        active.data().at("limits").at("maxDecks").get<std::size_t>();
    blueprint["decks"] = Json::array();
    rejected(blueprint, active, schema, "deck_limit", "/decks");
    for (std::size_t i = 0; i < maximum; ++i)
      blueprint["decks"].push_back(deck);
    preserved(blueprint, active, schema, "decks");
    blueprint["decks"].push_back(deck);
    rejected(blueprint, active, schema, "deck_limit", "/decks");

    require(active.data().at("limits").at("maxCarryDepth") == 1,
            "bundled carry depth is one");
    auto carrier = one_module();
    carrier["decks"][0]["modules"][0]["type"] = "shuttleDock";
    carrier["decks"][0]["modules"][0]["craft"] = blank();
    preserved(carrier, active, schema, "decks");
    auto nested = carrier;
    nested["decks"][0]["modules"][0]["craft"] = carrier;
    rejected(nested, active, schema, "carry_depth",
             "/decks/0/modules/0/craft/decks/0/modules/0/craft");
    auto noncarrier = one_module();
    noncarrier["decks"][0]["modules"][0]["craft"] = blank();
    rejected(noncarrier, active, schema, "invalid_carrier",
             "/decks/0/modules/0/craft");
  });

  suite.run(
      "label controls and note limits reject without rewriting metadata", [&] {
        for (const auto control : {'\0', '\n', '\x7F'}) {
          auto blueprint = blank();
          blueprint["name"] = std::string("prefix") + control + "suffix";
          rejected(blueprint, active, schema, "invalid_text", "/name");
        }
        auto blueprint = blank();
        const auto maximum =
            active.data().at("limits").at("maxDeckName").get<std::size_t>();
        blueprint["decks"][0]["name"] = std::string(maximum, 'd');
        preserved(blueprint, active, schema, "decks");
        blueprint["decks"][0]["name"] = std::string(maximum + 1, 'd');
        rejected(blueprint, active, schema, "field_limit", "/decks/0/name");
        blueprint = blank();
        const auto note_length =
            active.data().at("limits").at("maxNoteLength").get<std::size_t>();
        blueprint["migrationNotes"].push_back(
            std::string(note_length + 1, 'n'));
        rejected(blueprint, active, schema, "field_limit", "/migrationNotes/0");
        blueprint = blank();
        const auto note_count = active.data()
                                    .at("limits")
                                    .at("maxMigrationNotes")
                                    .get<std::size_t>();
        for (std::size_t i = 0; i <= note_count; ++i)
          blueprint["migrationNotes"].push_back("note");
        // Luna, under owner delegation 2026-11-23: count overflow retains
        // newest notes and one notice inside the original active bound.
        const auto retained = import_definition(blueprint.dump(), active, schema);
        require(retained.ok(), "Note count overflow must remain importable");
        Json expected = Json::array({"Older migration notes were dropped."});
        for (std::size_t i = 1; i < note_count; ++i)
          expected.push_back("note");
        require(retained.value->definition.at("blueprint").at("migrationNotes") == expected,
                "Overflow keeps newest duplicate notes plus exactly one bounded notice");
      });

  suite.run("import forwards operational input byte and nesting limits", [&] {
    const auto bytes = blank().dump();
    require(import_definition(bytes, active, schema, {bytes.size(), 64}).ok(),
            "document exactly at byte cap must import");
    const auto too_many_bytes =
        import_definition(bytes, active, schema, {bytes.size() - 1, 64});
    require(!too_many_bytes.ok() &&
                issue(too_many_bytes.issues, "resource_limit", ""),
            "over-byte-limit input must fail before calculations");
    const auto too_deep =
        import_definition(bytes, active, schema, {bytes.size(), 1});
    require(!too_deep.ok() && issue(too_deep.issues, "resource_limit", ""),
            "import must apply caller nesting cap");
  });

  return suite.finish();
}
