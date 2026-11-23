#include "test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <chrono>
#include <vector>

namespace {
using namespace test;

std::string repeat(std::string_view unit, std::size_t count) {
  std::string result;
  for (std::size_t i = 0; i < count; ++i)
    result += unit;
  return result;
}

void accepted(const Json &blueprint, const RulesSnapshot &active,
              const SchemaSet &schema) {
  const auto imported = import_definition(blueprint.dump(), active, schema);
  require(imported.ok(), "grapheme-boundary input must import: " +
                             Json(imported.issues).dump());
  require(imported.value->definition.at("blueprint") == blueprint,
          "name validation must retain all input bytes and unrelated fields");
  const auto again = import_definition(imported.value->definition.dump(), active,
                                       schema);
  require(again.ok() && again.value->definition == imported.value->definition,
          "export and reimport must preserve grapheme name bytes");
}

void rejected_name(const Json &blueprint, const RulesSnapshot &active,
                   const SchemaSet &schema, std::string_view path = "/name") {
  const auto imported = import_definition(blueprint.dump(), active, schema);
  require(!imported.ok() && issue(imported.issues, "field_limit", path),
          "one cluster beyond active name limit must reject at " +
              std::string(path) + ": " + Json(imported.issues).dump());
}

RulesSnapshot cap(const RulesSnapshot &active, const SchemaSet &schema,
                  std::size_t maximum) {
  auto data = active.data();
  data["technicalLimits"]["maxBlueprintNameCharacters"] = maximum;
  data["defaults"]["shipName"] = "S";
  data["defaults"]["stationName"] = "T";
  auto loaded = load_rules(data.dump(), schema);
  require(loaded.ok(), "coherent active grapheme cap must load");
  return std::move(*loaded.value);
}

struct TemporarySchemas {
  std::filesystem::path directory;
  TemporarySchemas() {
    directory = std::filesystem::temp_directory_path() /
                ("shipyard-grapheme-schemas-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    for (const auto &entry : std::filesystem::directory_iterator(
             std::filesystem::path(SHIPYARD_SOURCE_DIR) / "schemas"))
      std::filesystem::copy_file(entry.path(), directory / entry.path().filename());
    std::ofstream stream(directory / "scalar.schema.json");
    stream << R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://test.invalid/scalar","type":"string","minLength":2,"maxLength":2})";
    require(stream.good(), "real temporary scalar schema must be written");
  }
  ~TemporarySchemas() { std::filesystem::remove_all(directory); }
};
} // namespace

int main() {
  using namespace test;
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;

  // Luna, under owner delegation 2026-11-23: independent literal examples of
  // extended grapheme clusters; expected counts never call a production helper.
  const std::vector<std::pair<std::string, std::string>> clusters{
      {"ASCII", "a"}, {"astral emoji", "\U0001F680"},
      {"combining acute", "e\u0301"},
      {"emoji modifier", "\U0001F44D\U0001F3FD"},
      {"ZWJ family", "\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466"},
      {"regional indicator pair", "\U0001F1FA\U0001F1F8"},
      {"Indic conjunct", "\u0915\u094D\u0937"},
      {"Hangul syllable", "\u1100\u1161\u11A8"},
      {"composed acute", "\u00E9"}};
  for (const auto &[label, cluster] : clusters) {
    suite.run(label + " 80 clusters import byte-exact and 81 reject", [&] {
      auto blueprint = blank();
      blueprint["name"] = repeat(cluster, 80);
      accepted(blueprint, active, schema);
      blueprint["name"] = repeat(cluster, 81);
      rejected_name(blueprint, active, schema);
    });
  }

  suite.run("regional indicator state groups both even and odd runs", [&] {
    auto blueprint = blank();
    for (const auto scalar_count : {159, 160}) {
      blueprint["name"] = repeat("\U0001F1FA", scalar_count);
      accepted(blueprint, active, schema); // ceil(count/2) is exactly 80.
    }
    blueprint["name"] = repeat("\U0001F1FA", 161);
    rejected_name(blueprint, active, schema); // Exactly 81 independent clusters.
  });

  suite.run("one-grapheme and leading combining names keep exact boundaries", [&] {
    const auto narrow = cap(active, schema, 1);
    auto blueprint = blank();
    blueprint["name"] = "e\u0301";
    accepted(blueprint, narrow, schema);
    blueprint["name"] = "e\u0301x";
    rejected_name(blueprint, narrow, schema);
    blueprint["name"] = repeat("\u0301", 80);
    accepted(blueprint, narrow, schema); // Leading Extend run is one cluster.
    blueprint["name"] = repeat("\u0301", 80) + "x";
    rejected_name(blueprint, narrow, schema);
  });

  suite.run("canonically equivalent names retain distinct original bytes", [&] {
    auto composed = blank();
    auto decomposed = blank();
    composed["name"] = repeat("\u00E9", 80);
    decomposed["name"] = repeat("e\u0301", 80);
    accepted(composed, active, schema);
    accepted(decomposed, active, schema);
    require(composed.at("name") != decomposed.at("name"),
            "the independent fixtures must have different Unicode encodings");
  });

  suite.run("nested craft name uses the same cap and precise child path", [&] {
    auto carrier = blank();
    auto child = blank();
    child["name"] = repeat("\U0001F44D\U0001F3FD", 80);
    carrier["decks"][0]["modules"].push_back(
        {{"id", 1}, {"type", "shuttleDock"}, {"x", 1}, {"y", 1}, {"r", 0},
         {"craft", child}});
    carrier["nextId"] = 2;
    accepted(carrier, active, schema);
    carrier["decks"][0]["modules"][0]["craft"]["name"] =
        repeat("\U0001F44D\U0001F3FD", 81);
    rejected_name(carrier, active, schema, "/decks/0/modules/0/craft/name");
  });

  suite.run("active caps 3 and 100 govern names and both design defaults", [&] {
    for (const std::size_t maximum : {3, 100}) {
      const auto configured = cap(active, schema, maximum);
      auto blueprint = blank();
      blueprint["name"] = repeat("e\u0301", maximum);
      accepted(blueprint, configured, schema);
      blueprint["name"] = repeat("e\u0301", maximum + 1);
      rejected_name(blueprint, configured, schema);
      for (const auto *key : {"shipName", "stationName"}) {
        auto data = configured.data();
        const auto name = repeat("\U0001F468\u200D\U0001F469\u200D\U0001F467", maximum);
        data["defaults"][key] = name;
        const auto loaded = load_rules(data.dump(), schema);
        require(loaded.ok() && loaded.value->data().at("defaults").at(key) == name,
                "rule design defaults must accept grapheme endpoint byte-exact");
        require(blank_blueprint(*loaded.value,
                                std::string_view(key) == "shipName"
                                    ? BlankType::ship : BlankType::station).at("name") == name,
                "public factory must emit the active exact default name");
        data["defaults"][key] = name + "x";
        const auto over = load_rules(data.dump(), schema);
        require(!over.ok() && issue(over.issues, "invalid_rules",
                                   "/defaults/" + std::string(key)),
                "over-cap rule design default must reject at its own field");
      }
    }
  });

  suite.run("deck note and generated-prefix limits still count scalars", [&] {
    auto blueprint = blank();
    blueprint["decks"][0]["name"] = repeat("e\u0301", 24); // 48 scalars.
    accepted(blueprint, active, schema);
    blueprint["decks"][0]["name"] = repeat("e\u0301", 25);
    auto over = import_definition(blueprint.dump(), active, schema);
    require(!over.ok() && issue(over.issues, "field_limit", "/decks/0/name"),
            "deck names must retain their scalar limit");
    blueprint = blank();
    blueprint["migrationNotes"] = Json::array({repeat("e\u0301", 150)});
    accepted(blueprint, active, schema); // 300 scalars.
    blueprint["migrationNotes"][0] = repeat("e\u0301", 151);
    over = import_definition(blueprint.dump(), active, schema);
    require(!over.ok() && issue(over.issues, "field_limit", "/migrationNotes/0"),
            "ordinary notes must retain their scalar limit");
    auto data = active.data();
    data["defaults"]["deckName"] = repeat("e\u0301", 24);
    require(load_rules(data.dump(), schema).ok(), "48-scalar deck default fits");
    data["defaults"]["deckName"] = repeat("e\u0301", 25);
    auto bad = load_rules(data.dump(), schema);
    require(!bad.ok() && issue(bad.issues, "invalid_rules", "/defaults/deckName"),
            "rule deck defaults must retain their scalar limit");
    data = active.data();
    data["defaults"]["deckNamePrefix"] = repeat("e\u0301", 23);
    require(load_rules(data.dump(), schema).ok(), "46-scalar prefix plus level fits");
    data["defaults"]["deckNamePrefix"] = repeat("e\u0301", 24);
    bad = load_rules(data.dump(), schema);
    require(!bad.ok() && issue(bad.issues, "invalid_rules", "/defaults/deckNamePrefix"),
            "prefix scalar count includes the generated decimal deck suffix");
  });

  suite.run("generic JSON Schema lengths retain independent scalar semantics", [&] {
    TemporarySchemas files;
    const auto loaded = SchemaSet::load(files.directory);
    require(loaded.ok(), "real scalar test schema must load with bundled schemas");
    require(loaded.value->validate("e\u0301", "scalar").empty(),
            "two scalars forming one grapheme satisfy schema length two");
    require(issue(loaded.value->validate("\U0001F680", "scalar"), "schema_error", ""),
            "one astral scalar is below schema minLength two");
    require(issue(loaded.value->validate("e\u0301x", "scalar"), "schema_error", ""),
            "three scalars exceed schema maxLength two");
  });

  suite.run("grapheme counting keeps control and invalid UTF8 rejections", [&] {
    for (const auto c : {'\0', '\t', '\n', '\x7F'}) {
      auto blueprint = blank();
      blueprint["name"] = std::string("e\u0301") + c;
      const auto bad = import_definition(blueprint.dump(), active, schema);
      require(!bad.ok() && issue(bad.issues, "invalid_text", "/name"),
              "control policy must not change with grapheme counting");
      auto data = cap(active, schema, 3).data();
      data["defaults"]["shipName"] = std::string("e\u0301") + c;
      const auto invalid_default = load_rules(data.dump(), schema);
      require(!invalid_default.ok() && issue(invalid_default.issues, "invalid_rules",
                                            "/defaults/shipName"),
              "rule design default controls must still reject");
    }
    auto blueprint = blank();
    const auto bytes = blueprint.dump();
    const auto pos = bytes.find("Blank");
    auto malformed = bytes;
    malformed.replace(pos, 5, "\xF0\x28\x8C\x28");
    const auto bad = import_definition(malformed, active, schema);
    require(!bad.ok(), "malformed UTF8 must reject before grapheme decoding");
  });
  return suite.finish();
}
