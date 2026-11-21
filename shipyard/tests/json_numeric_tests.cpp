#include "test_support.hpp"

#include <cmath>
#include <limits>
#include <locale>
#include <vector>

namespace {
using namespace test;

template <class Exception, class Function> void throws(Function &&function) {
  bool caught = false;
  try {
    std::forward<Function>(function)();
  } catch (const Exception &) {
    caught = true;
  }
  require(caught, "Expected the documented exception");
}

struct CommaDecimal : std::numpunct<char> {
  char do_decimal_point() const override { return ','; }
};

struct LocaleGuard {
  std::locale previous = std::locale();
  LocaleGuard() {
    std::locale::global(std::locale(previous, new CommaDecimal));
  }
  ~LocaleGuard() { std::locale::global(previous); }
};
} // namespace

int main() {
  using namespace test;
  Suite suite;
  const auto schema_set = schemas();
  const auto snapshot = rules(schema_set);

  suite.run("strict malformed JSON and decoded duplicate keys", [] {
    for (const auto *input :
         {"", "{", "[1,]", "{\"a\":1,}", "// comment\n{}", "{} {}", "NaN",
          "Infinity", "01", "1e", "\"\\ud800\""}) {
      const auto result = parse_json(input);
      require(!result.ok() && issue(result.issues, "invalid_json", ""), input);
    }
    for (const auto *input : {R"({"a":1,"a":2})", R"({"a":1,"\u0061":2})",
                              R"([{"nested":{"x":1,"x":2}}])"}) {
      const auto result = parse_json(input);
      require(!result.ok() && issue(result.issues, "duplicate_key", ""), input);
    }
    require(parse_json(R"([{"a":1},{"a":2}])").ok(),
            "Keys in separate objects are distinct");
  });

  suite.run("UTF-8 validation and finite numeric range", [] {
    for (const auto &input :
         {std::string("\"\xc0\xaf\""), std::string("\"\xed\xa0\x80\""),
          std::string("\"\xf4\x90\x80\x80\""), std::string("\"\xff\"")}) {
      const auto result = parse_json(input);
      require(!result.ok() && issue(result.issues, "invalid_json", ""),
              "Invalid UTF-8 accepted");
    }
    for (const auto *input : {"1e400", "-1e400"}) {
      const auto result = parse_json(input);
      require(!result.ok() && issue(result.issues, "invalid_number", ""),
              "Overflow accepted");
    }
    require(
        parse_json(R"({"name":"星🚀","number":1.7976931348623157e308})").ok(),
        "Valid scalar UTF-8 and largest finite binary64 must parse");
  });

  suite.run("byte and mixed container nesting bounds are exact", [] {
    require(parse_json("{}", {2, 1}).ok(), "Byte bound is inclusive");
    require(!parse_json("{} ", {2, 1}).ok(), "Whitespace counts toward bytes");
    require(issue(parse_json("{} ", {2, 1}).issues, "resource_limit", ""),
            "Clear byte-limit issue");
    require(parse_json(R"({"a":[{}]})", {64, 3}).ok(), "Mixed depth at limit");
    require(issue(parse_json(R"({"a":[{}]})", {64, 2}).issues, "resource_limit",
                  ""),
            "Mixed depth above limit");
    require(parse_json("0", {1, 0}).ok(), "Scalars have zero container depth");
    require(!parse_json("[]", {2, 0}).ok(),
            "First container exceeds zero depth");
    const auto at_limit = std::string(64, '[') + "0" + std::string(64, ']');
    require(parse_json(at_limit).ok(),
            "Default 64-container limit is inclusive");
    require(
        issue(parse_json("[" + at_limit + "]").issues, "resource_limit", ""),
        "Default limit rejects 65 containers");
  });

  suite.run("canonical key order, arrays, UTF-8, escapes and numeric encoding", [] {
    Json value = {{"z", Json::array({3, 1, 2})},
                  {"a", 1},
                  {"float", 1.0},
                  {"negativeZero", -0.0},
                  {"text", "星🚀\n\"\\"},
                  {"nested", {{"b", 0.1}, {"a", 1e30}}}};
    const std::string expected =
        R"({"a":1,"float":1.0,"negativeZero":0.0,"nested":{"a":1e+30,"b":0.1},"text":"星🚀\n\"\\","z":[3,1,2]})";
    require(canonical_json(value) == expected,
            "Canonical bytes must match documented encoding");
    require(std::signbit(value.at("negativeZero").get<double>()),
            "Serialization must not mutate its input");
    LocaleGuard comma_locale;
    require(canonical_json(value) == expected,
            "Encoding must be locale independent");
    require(canonical_json(Json{{"é", 1}, {"z", 2}, {"a", 3}}) ==
                R"({"a":3,"z":2,"é":1})",
            "Object key ordering uses UTF-8 lexical order");
  });

  suite.run(
      "canonical serialization preserves inputs and rejects nonfinite values",
      [] {
        const auto result =
            parse_json(R"({"a":1.23456789012345,"b":"e\u0301","c":[2,1]})");
        require(result.ok(), "Canonicalization source parses");
        require(
            canonical_json(*result.value) ==
                "{\"a\":1.23456789012345,\"b\":\"é\",\"c\":[2,1]}",
            "No precision rounding or Unicode normalization on serialization");
        for (double value : {std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
          throws<std::invalid_argument>(
              [&] { (void)canonical_json(Json{{"nested", {value}}}); });
        }
      });

  suite.run("computed comparison uses finite absolute inclusive epsilon", [&] {
    const double epsilon = snapshot.data().at("numeric").at("epsilon");
    require(epsilon == 0.000001, "Approved absolute epsilon");
    require(numerically_equal(0, epsilon, snapshot),
            "Inclusive positive boundary");
    require(numerically_equal(0, -epsilon, snapshot),
            "Inclusive negative boundary");
    require(!numerically_equal(0, std::nextafter(epsilon, 1.0), snapshot),
            "No beyond-epsilon tolerance");
    require(!numerically_equal(1e12, 1e12 + 1, snapshot),
            "No relative tolerance at large magnitude");
    require(numerically_equal(-0.0, 0.0, snapshot),
            "Signed zero compares equal");
    for (double value : {std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
      require(!numerically_equal(value, value, snapshot),
              "Nonfinite values never compare equal");
      require(!numerically_equal(0, value, snapshot),
              "Nonfinite operand rejected");
    }
    auto custom = snapshot.data();
    custom["numeric"]["epsilon"] = 0.125;
    const auto active = load_rules(canonical_json(custom), schema_set);
    require(active.ok(), "Modified epsilon rules load");
    require(numerically_equal(1, 1.125, *active.value),
            "Read epsilon from active rules");
    require(!numerically_equal(1, 1.25, *active.value),
            "Custom epsilon remains absolute");
  });

  suite.run("precision rounding and tie direction come from active rules", [&] {
    require(snapshot.data().at("numeric").at("precision") == 6,
            "Approved precision");
    require(round_computed(1.23456749, snapshot) == 1.234567,
            "Round down at six decimals");
    require(round_computed(1.23456751, snapshot) == 1.234568,
            "Round up at six decimals");
    require(round_computed(0.0000001, snapshot) == 0,
            "Small computed value rounds to zero");
    require(round_computed(-0.0000001, snapshot) == 0,
            "Small negative computed value rounds to zero");
    require(round_computed(-1.23456749, snapshot) == -1.234567,
            "Negative value rounds toward the nearer six-decimal value");
    require(round_computed(-1.23456751, snapshot) == -1.234568,
            "Negative value rounds away when the next digit exceeds five");
    auto custom = snapshot.data();
    custom["numeric"]["precision"] = 0;
    const auto active = load_rules(canonical_json(custom), schema_set);
    require(active.ok(), "Zero precision rules load");
    for (const auto &[input, expected] :
         std::vector<std::pair<double, double>>{{2.5, 3},
                                                {-2.5, -2},
                                                {0.5, 1},
                                                {-0.5, 0},
                                                {2.25, 2},
                                                {-2.75, -3}}) {
      require(round_computed(input, *active.value) == expected,
              "Nearest ties toward positive infinity");
    }
    require(round_computed(9007199254740991.0, *active.value) ==
                9007199254740991.0,
            "Large integral computed values remain unchanged");
    throws<std::invalid_argument>([&] {
      (void)round_computed(std::numeric_limits<double>::infinity(), snapshot);
    });
    throws<std::invalid_argument>([&] {
      (void)round_computed(std::numeric_limits<double>::quiet_NaN(), snapshot);
    });
    throws<std::overflow_error>([&] {
      (void)round_computed(std::numeric_limits<double>::max(), snapshot);
    });
  });

  suite.run(
      "import rounds computed masses while preserving input numbers", [&] {
        auto custom = snapshot.data();
        custom["floorMass"] = custom["floor"]["mass"] = 0.12345678;
        const auto active = load_rules(canonical_json(custom), schema_set);
        require(active.ok(), "Fractional mass rules load");
        auto blueprint = blank();
        blueprint["decks"][0]["floors"]["0,0"] = true;
        blueprint["loads"]["cargo"] = 0.123456789123;
        const auto result = import_definition(canonical_json(blueprint),
                                              *active.value, schema_set);
        require(result.ok(), "Fractional direct totals import");
        const auto &output = result.value->definition;
        for (const auto *field :
             {"structureMass", "dryMass", "mass", "currentMass"}) {
          require(output.at("derived").at(field) == 0.123457,
                  std::string("Rounded mass: ") + field);
        }
        require(output.at("blueprint").at("loads") == blueprint.at("loads"),
                "Input load fractions are never rounded");
      });

  return suite.finish();
}
