#include "test_support.hpp"

#include <string>
#include <utility>

namespace {
using namespace test;

Json embedded_formula(Json definition, std::size_t depth) {
  Json nested = 1;
  for (std::size_t i = 0; i < depth; ++i)
    nested = Json::array({"floor", std::move(nested)});
  definition["rules"]["formulas"]["structureMass"] = std::move(nested);
  return definition;
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);
  const auto imported = import_definition(blank().dump(), active, schema_set);
  require(imported.ok(), "baseline v4 definition must import");
  InputLimits generous;
  generous.maxNesting = 256;

  suite.run("identifier pattern agrees with the explicit ASCII alphabet", [&] {
    const std::string letters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const std::string trailing = letters + "0123456789_";
    auto blueprint = blank();
    blueprint["decks"][0]["modules"].push_back(
        {{"id", 1}, {"type", "A"}, {"x", 0}, {"y", 0}, {"r", 0}});
    blueprint["nextId"] = 2;
    for (int byte = 0; byte < 128; ++byte) {
      const char character = static_cast<char>(byte);
      for (const bool first : {true, false}) {
        const std::string name = (first ? std::string() : "A") + character;
        blueprint["decks"][0]["modules"][0]["type"] = name;
        const auto issues = schema_set.validate(blueprint, "apsis-shipyard-v4");
        const bool expected =
            (first ? letters : trailing).find(character) != std::string::npos;
        require(issues.empty() == expected,
                "Identifier alphabet mismatch at byte " + std::to_string(byte) +
                    (first ? " first" : " trailing"));
        if (!expected)
          require(issue(issues, "schema_error", "/decks/0/modules/0/type"),
                  "Invalid identifier must identify the module type path");
      }
    }
  });

  suite.run(
      "SHA256 pattern admits only lowercase hex and exactly 64 bytes", [&] {
        const std::string alphabet = "0123456789abcdef";
        auto definition = imported.value->definition;
        for (int byte = 0; byte < 128; ++byte) {
          const char character = static_cast<char>(byte);
          definition["rulesSha256"] = std::string(64, character);
          const auto issues =
              schema_set.validate(definition, "apsis-ship-definition-v4");
          require(issues.empty() ==
                      (alphabet.find(character) != std::string::npos),
                  "SHA256 alphabet mismatch at byte " + std::to_string(byte));
          if (!issues.empty())
            require(issue(issues, "schema_error", "/rulesSha256"),
                    "Invalid SHA256 byte must retain its public schema path");
        }
        for (const std::size_t length : {std::size_t{0}, std::size_t{63},
                                         std::size_t{64}, std::size_t{65}}) {
          definition["rulesSha256"] = std::string(length, 'a');
          const auto issues =
              schema_set.validate(definition, "apsis-ship-definition-v4");
          require(issues.empty() == (length == 64),
                  "SHA256 length must be exactly 64");
        }
      });

  // Embedded rules are informational, so their formulas reach the real schema
  // checker without the active-rules 64-level semantic guard. With the bundled
  // wrapper/ref/anyOf/items structure, 127 floor arrays reach step 512 exactly;
  // the next array exceeds CONTRACT.md's independent schema recursion cap.
  suite.run("embedded formula at schema step 512 is accepted", [&] {
    const auto definition = embedded_formula(imported.value->definition, 127);
    require(schema_set.validate(definition, "apsis-ship-definition-v4").empty(),
            "schema step 512 must remain inclusive (kills 512 -> 384)");
    const auto result =
        import_definition(definition.dump(), active, schema_set, generous);
    require(result.ok(), "raised parser bound must admit schema endpoint: " +
                             Json(result.issues).dump());
    require(result.value->definition.at("rules") == active.data(),
            "deep embedded rules must still be replaced by active rules");
  });

  suite.run("next embedded formula level fails cleanly at schema cap", [&] {
    const auto definition = embedded_formula(imported.value->definition, 128);
    const auto bytes = definition.dump();
    require(parse_json(bytes, generous).ok(),
            "parser nesting must not mask schema recursion regression");
    const auto issues =
        schema_set.validate(definition, "apsis-ship-definition-v4");
    require(issue(issues, "schema_error", "/rules/formulas/structureMass"),
            "schema cap must return a readable structured formula error");
    const auto result = import_definition(bytes, active, schema_set, generous);
    require(!result.ok(), "unbounded schema traversal must not accept input");
    require(issue(result.issues, "schema_error",
                  "/rules/formulas/structureMass"),
            "public import must report schema cap as a structured error");
    require(std::all_of(result.issues.begin(), result.issues.end(),
                        [](const Issue &item) {
                          return item.severity == "error" &&
                                 !item.message.empty();
                        }),
            "cap failure must carry readable error diagnostics");
  });
  return suite.finish();
}
