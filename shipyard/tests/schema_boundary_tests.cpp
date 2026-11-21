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
