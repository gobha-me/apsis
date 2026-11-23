#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace test;

// Check the public error contract without depending on diagnostic wording.
template <class T>
void rejected(const Result<T> &result, const std::string &path,
              const std::string &code = {}) {
  require(!result.ok(), "invalid input produced a value at " + path);
  require(std::any_of(result.issues.begin(), result.issues.end(),
                      [&](const Issue &item) {
                        return item.severity == "error" &&
                               !item.message.empty() &&
                               item.path.starts_with(path) &&
                               (code.empty() || item.code == code);
                      }),
          "missing readable structured error at " + path + " (" + code + ")");
}

std::size_t part_index(const Json &rules, const std::string &id) {
  const auto &parts = rules.at("parts");
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (parts[i].at("id") == id)
      return i;
  }
  throw std::runtime_error("missing catalogue part " + id);
}

Json expression(const std::string &op, std::size_t count) {
  Json value = Json::array({op});
  for (std::size_t i = 0; i < count; ++i)
    value.push_back(1);
  return value;
}
} // namespace

int main(int argc, char **argv) {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);
  const auto original = active.data();

  // This enabled CTest entry preserves the original formula-depth regression
  // expectations. See tests/README.md for the independent nesting guards.
  if (argc == 2 && std::string_view(argv[1]) == "--formula-nesting") {
    for (const std::size_t depth : {64, 65}) {
      suite.run("formula nesting depth " + std::to_string(depth), [&] {
        InputLimits generous;
        generous.maxNesting = 128;
        Json nested = 1;
        for (std::size_t i = 0; i < depth; ++i)
          nested = Json::array({"floor", std::move(nested)});
        auto changed = original;
        changed["formulas"]["structureMass"] = std::move(nested);
        const auto loaded = load_rules(changed.dump(), schema_set, generous);
        if (depth == 64)
          require(loaded.ok(), "documented formula nesting endpoint: " +
                                   Json(loaded.issues).dump());
        else
          rejected(loaded, "/formulas/structureMass", "invalid_formula");
      });
    }
    return suite.finish();
  }
  require(argc == 1, "Unknown test argument");

  suite.run("rules snapshot has exactly the five owner-picked jump prices",
            [&] {
              struct Price {
                const char *id;
                double rating;
                int credits;
              };
              constexpr std::array prices{
                  Price{"drive05", 0.5, 1500}, Price{"drive10", 1.0, 2500},
                  Price{"drive15", 1.5, 3750}, Price{"drive20", 2.0, 5000},
                  Price{"drive30", 3.0, 7500}};
              require(active.version() == 4, "loaded rules version");
              std::size_t drives = 0;
              for (const auto &part : original.at("parts")) {
                if (part.value("system", std::string()) == "jumpDrive")
                  ++drives;
              }
              require(drives == prices.size(),
                      "jump catalogue must contain five explicit entries");
              for (const auto &expected : prices) {
                const auto &part =
                    original.at("parts")[part_index(original, expected.id)];
                require(part.at("effect").at("rating") == expected.rating,
                        std::string(expected.id) + " rating");
                require(part.at("cost") == expected.credits,
                        std::string(expected.id) + " price");
              }
            });

  suite.run(
      "ports omit size envelopes and hangars retain all three limits", [&] {
        const auto historical =
            Json::parse(read("fixtures/v3/apsis-rules-v3.json"));
        const auto &port =
            original.at("parts")[part_index(original, "dockingPort")];
        require(port.at("docking") ==
                    Json({{"kind", "port"}, {"interface", "standard"}}),
                "port docking record must have no visitor envelope");
        const auto hangar_index = part_index(original, "stationHangar");
        const auto &hangar = original.at("parts")[hangar_index].at("docking");
        const auto &old_hangar =
            historical.at("parts")[part_index(historical, "stationHangar")].at(
                "docking");
        require(hangar.at("maxWidthM") == 10 && hangar.at("maxLengthM") == 18 &&
                    hangar.at("maxDecks") == 2,
                "installed hangar width, length, and deck count");
        for (const auto *key : {"maxWidthM", "maxLengthM", "maxDecks"}) {
          require(hangar.at(key) == old_hangar.at(key),
                  "historical hangar limit preserved");
          auto missing = original;
          missing["parts"][hangar_index]["docking"].erase(key);
          rejected(load_rules(missing.dump(), schema_set),
                   "/parts/" + std::to_string(hangar_index) + "/docking");

          auto envelope = original;
          const auto index = part_index(envelope, "dockingPort");
          envelope["parts"][index]["docking"][key] = old_hangar.at(key);
          rejected(load_rules(envelope.dump(), schema_set),
                   "/parts/" + std::to_string(index) + "/docking");
        }
      });

  suite.run("malformed rules JSON and nonfinite literals are rejected", [&] {
    for (const auto *bytes :
         {"{", "null", "[]", "{\"version\":4,}", "{\"version\":NaN}",
          "{\"version\":Infinity}", "{\"version\":1e999}"}) {
      rejected(load_rules(bytes, schema_set), "");
    }
  });

  suite.run("unknown rule fields, versions, and missing catalogue fields are "
            "rejected",
            [&] {
              for (const auto *pointer :
                   {"/unexpected", "/technicalLimits/unexpected",
                    "/parts/0/unexpected", "/formulas/unexpected"}) {
                auto changed = original;
                changed[Json::json_pointer(pointer)] = 1;
                rejected(load_rules(changed.dump(), schema_set), pointer);
              }
              for (const auto version : {3.0, 4.0000001, 5.0}) {
                auto changed = original;
                changed["version"] = version;
                rejected(load_rules(changed.dump(), schema_set), "/version");
              }
              auto changed = original;
              changed["schema"] = "apsis-ship-rules-v5";
              rejected(load_rules(changed.dump(), schema_set), "/schema");
              changed = original;
              changed["parts"][0].erase("mass");
              rejected(load_rules(changed.dump(), schema_set), "/parts/0");
              for (const auto &[name, unused] :
                   original.at("formulas").items()) {
                (void)unused;
                changed = original;
                changed["formulas"].erase(name);
                rejected(load_rules(changed.dump(), schema_set), "/formulas");
              }
            });

  suite.run("rules enforce numeric and interoperable integer bounds", [&] {
    const std::vector<std::pair<std::string, Json>> invalid{
        {"/numeric/precision", -1},
        {"/numeric/precision", 16},
        {"/numeric/precision", 1.5},
        {"/numeric/epsilon", 0},
        {"/numeric/epsilon", -0.000001},
        {"/grid/w", 0},
        {"/grid/w", 26.5},
        {"/grid/w", 9007199254740992ULL},
        {"/limits/maxModuleId", 9007199254740991ULL},
        {"/technicalLimits/maxBlueprintNameCharacters", 0},
        {"/technicalLimits/maxBudgetInputCr", -1},
        {"/technicalLimits/maxBudgetInputCr", 9007199254740992ULL},
        {"/defaults/loads/cargo", 1.0000001},
        {"/parts/0/mass", -0.0000001},
        {"/performance/jumpFuelPerJump", 0}};
    for (const auto &[pointer, value] : invalid) {
      auto changed = original;
      changed[Json::json_pointer(pointer)] = value;
      rejected(load_rules(changed.dump(), schema_set), pointer);
    }
    for (const auto precision : {0, 15}) {
      auto changed = original;
      changed["numeric"]["precision"] = precision;
      require(load_rules(changed.dump(), schema_set).ok(),
              "supported precision endpoint");
    }
  });

  suite.run(
      "blueprint name limit includes the largest interoperable integer", [&] {
        constexpr auto ceiling = 9007199254740991ULL;
        const std::string path = "/technicalLimits/maxBlueprintNameCharacters";
        auto changed = original;
        auto &limit = changed[Json::json_pointer(path)];
        limit = ceiling;
        const auto loaded = load_rules(changed.dump(), schema_set);
        require(loaded.ok(), "The exact integer ceiling must load: " +
                                 Json(loaded.issues).dump());
        require(loaded.value->data().at(Json::json_pointer(path)) == ceiling,
                "The accepted rule retains the exact ceiling");
        limit = ceiling + 1;
        rejected(load_rules(changed.dump(), schema_set), path);
      });

  suite.run(
      "every formula rejects unknown operators and variables at load time "
      "(F10)",
      [&] {
        for (const auto &[name, unused] : original.at("formulas").items()) {
          (void)unused;
          for (const Json &bad :
               {Json("unknownVariable"), Json::array({"execute", 1, 2})}) {
            auto changed = original;
            changed["formulas"][name] = bad;
            rejected(load_rules(changed.dump(), schema_set),
                     "/formulas/" + name, "invalid_formula");
          }
        }
        auto changed = original;
        changed["formulas"]["structureMass"] =
            Json::array({"add", 1, Json::array({"mul", "mass", 2})});
        rejected(load_rules(changed.dump(), schema_set),
                 "/formulas/structureMass/2/1", "invalid_formula");
      });

  suite.run("malformed formula leaves and operator arrays are rejected", [&] {
    const std::vector<Json> invalid{nullptr,
                                    true,
                                    Json::object(),
                                    Json::array(),
                                    Json::array({1, 2}),
                                    Json::array({"add", 1, false}),
                                    Json::array({"mul", 1, Json::object()})};
    for (const auto &bad : invalid) {
      auto changed = original;
      changed["formulas"]["structureMass"] = bad;
      rejected(load_rules(changed.dump(), schema_set),
               "/formulas/structureMass");
    }
  });

  suite.run("all operator arity endpoints are checked at load time", [&] {
    struct Arity {
      const char *op;
      std::size_t minimum;
      std::size_t maximum;
    };
    constexpr std::array arities{Arity{"add", 2, 64},  Arity{"mul", 2, 64},
                                 Arity{"sub", 2, 2},   Arity{"div", 2, 2},
                                 Arity{"floor", 1, 1}, Arity{"ln", 1, 1},
                                 Arity{"min", 1, 64},  Arity{"max", 1, 64}};
    for (const auto &arity : arities) {
      for (const auto count : {arity.minimum, arity.maximum}) {
        auto changed = original;
        changed["formulas"]["structureMass"] = expression(arity.op, count);
        require(load_rules(changed.dump(), schema_set).ok(),
                std::string(arity.op) + " valid arity " +
                    std::to_string(count));
      }
      for (const auto count : {arity.minimum - 1, arity.maximum + 1}) {
        auto changed = original;
        changed["formulas"]["structureMass"] = expression(arity.op, count);
        rejected(load_rules(changed.dump(), schema_set),
                 "/formulas/structureMass");
      }
    }
  });

  suite.run(
      "undefined direct-total formulas fail import without zero fallback (F10)",
      [&] {
        // These failures concern direct mass totals, outside the excluded F20
        // performance cases. Valid formula syntax is checked separately above.
        const std::vector<Json> invalid{
            Json::array({"div", 1, 0}),
            Json::array({"ln", 0}),
            Json::array({"ln", -1}),
            Json::array({"mul", 1e308, 1e308}),
            Json::array({"sub", -1e308, 1e308}),
            Json::array({"add", 1e308, 1e308, -1e308})};
        for (const auto &formula : invalid) {
          auto changed = original;
          changed["formulas"]["structureMass"] = formula;
          const auto loaded = load_rules(changed.dump(), schema_set);
          require(loaded.ok(),
                  "syntactically valid runtime-failure formula must load");
          rejected(import_definition(blank().dump(), *loaded.value, schema_set),
                   "/formulas/structureMass", "formula_error");
        }
      });
  return suite.finish();
}
