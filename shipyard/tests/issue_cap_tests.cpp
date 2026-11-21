#include "test_support.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace {
using namespace test;

// CONTRACT.md places the marker inside the public 128-slot allowance. These
// deliberately amplifying inputs must reach that exact endpoint, not merely
// return a short list because a parser or unrelated shape check rejected them.
void capped(const std::vector<Issue> &issues, std::string_view ordinary_code) {
  require(issues.size() == 128,
          "amplifying input must return exactly 127 diagnostics plus a marker");
  std::size_t markers = 0;
  std::size_t ordinary = 0;
  for (const auto &item : issues) {
    require(!item.message.empty(), "every diagnostic must be readable");
    if (item.code != "issues_truncated") {
      if (item.code == ordinary_code)
        ++ordinary;
      continue;
    }
    ++markers;
    require(item.severity == "error" && item.path.empty(),
            "truncation must be a root error");
    require(item.details.is_object() && item.details.at("limit") == 128,
            "truncation must expose the documented 128-slot limit");
    require(item.details.at("suppressedAtLeast").is_number_integer() &&
                item.details.at("suppressedAtLeast").get<std::size_t>() >= 1,
            "truncation must report at least one omitted diagnostic");
  }
  require(markers == 1, "exactly one truncation marker must be retained");
  require(ordinary > 0, "input must reach the intended diagnostic producer");
}

template <class Function>
void bounded_call(Function &&function) {
  const auto start = std::chrono::steady_clock::now();
  std::forward<Function>(function)();
  // This deliberately generous watchdog supplements CTest's process timeout.
  // It is an operational regression guard, not a library latency promise.
  require(std::chrono::steady_clock::now() - start < std::chrono::seconds(30),
          "bounded diagnostic collection must complete within 30 seconds");
}

std::string invalid_blueprint_bytes(std::size_t decks,
                                    std::size_t modules_per_deck) {
  auto blueprint = blank();
  blueprint["decks"] = Json::array();
  const std::string module =
      Json({{"id", 1}, {"type", "UnknownPart"}, {"x", 26}, {"y", 24}, {"r", 4}})
          .dump();
  // Construct the body as bytes so the test does not retain a second large DOM
  // while exercising the public parser and normalization path.
  auto bytes = blueprint.dump();
  const auto empty_decks = bytes.find("\"decks\":[]");
  require(empty_decks != std::string::npos, "blueprint has a deck placeholder");
  std::string deck_bytes = "[";
  for (std::size_t d = 0; d < decks; ++d) {
    if (d != 0)
      deck_bytes += ',';
    deck_bytes += "{\"name\":\"Main deck\",\"floors\":{},\"modules\":[";
    for (std::size_t m = 0; m < modules_per_deck; ++m) {
      if (m != 0)
        deck_bytes += ',';
      deck_bytes += module;
    }
    deck_bytes += "]}";
  }
  deck_bytes += ']';
  bytes.replace(empty_decks + std::string("\"decks\":").size(), 2,
                deck_bytes);
  require(bytes.size() < InputLimits{}.maxBytes,
          "amplifying blueprint must fit the default public byte allowance");
  return bytes;
}

Json invalid_formula_tree(std::size_t levels) {
  Json result = "unknownVariable";
  for (std::size_t level = 0; level < levels; ++level) {
    Json parent = Json::array({"add"});
    for (std::size_t operand = 0; operand < 64; ++operand)
      parent.push_back(result);
    result = std::move(parent);
  }
  return result;
}
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  suite.run("127 diagnostics fit and the 128th becomes the sole marker", [&] {
    for (const auto duplicates : {127U, 128U}) {
      auto changed = active.data();
      changed["buildTypes"] = Json::array({"ship", "station"});
      for (std::size_t i = 0; i < duplicates; ++i)
        changed["buildTypes"].push_back("ship");
      const auto result = load_rules(changed.dump(), schema_set);
      require(!result.ok(), "duplicate identifiers must fail at either boundary");
      if (duplicates == 127) {
        require(result.issues.size() == 127,
                "127 real errors must remain representable without truncation");
        require(std::all_of(result.issues.begin(), result.issues.end(),
                            [](const Issue &item) {
                              return item.code == "invalid_rules";
                            }),
                "no truncation marker may replace the 127th real diagnostic");
      } else {
        capped(result.issues, "invalid_rules");
      }
    }
  });

  suite.run("large invalid blueprint normalization shares the 128-issue cap",
            [&] {
              // The second body has 150,000 modules across the five allowed
              // decks, reproducing the review input without crossing 8 MiB.
              for (const auto count : {1000U, 30000U}) {
                const auto bytes = invalid_blueprint_bytes(5, count);
                bounded_call([&] {
                  const auto result =
                      import_definition(bytes, active, schema_set);
                  require(!result.ok(), "invalid blueprint must fail import");
                  capped(result.issues, "invalid_part_type");
                  require(issue(result.issues, "grid_capacity", "/decks/0"),
                          "normalization, rather than schema parsing, must run");
                });
              }
            });

  suite.run("branching formula errors share the 128-issue cap through load_rules",
            [&] {
              for (const auto levels : {2U, 3U}) {
                auto changed = active.data();
                changed["formulas"]["structureMass"] =
                    invalid_formula_tree(levels);
                const auto bytes = changed.dump();
                require(bytes.size() < InputLimits{}.maxBytes,
                        "formula amplification must fit the default byte cap");
                bounded_call([&] {
                  const auto result = load_rules(bytes, schema_set);
                  require(!result.ok(), "unknown formula variables must fail");
                  capped(result.issues, "invalid_formula");
                });
              }
            });

  suite.run("rules schema diagnostics share the 128-issue cap", [&] {
    auto changed = active.data();
    changed["parts"] = Json::array();
    for (std::size_t i = 0; i < 30000; ++i)
      changed["parts"].push_back(Json::object());
    const auto bytes = changed.dump();
    bounded_call([&] {
      const auto result = load_rules(bytes, schema_set);
      require(!result.ok(), "rules with incomplete parts must fail");
      capped(result.issues, "schema_error");
    });
  });

  suite.run("rules semantic diagnostics share the 128-issue cap", [&] {
    auto changed = active.data();
    // All entries satisfy the real schema. Duplicate identifiers therefore
    // reach semantic validation instead of being rejected as malformed JSON.
    for (std::size_t i = 0; i < 30000; ++i)
      changed["buildTypes"].push_back("ship");
    const auto bytes = changed.dump();
    bounded_call([&] {
      const auto result = load_rules(bytes, schema_set);
      require(!result.ok(), "duplicate rules identifiers must fail");
      capped(result.issues, "invalid_rules");
    });
  });

  return suite.finish();
}
