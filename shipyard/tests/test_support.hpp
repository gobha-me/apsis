#pragma once

#include <apsis/shipyard/loader.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace test {
using namespace apsis::shipyard;

inline void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

inline std::string read(const std::filesystem::path &relative) {
  const auto path = std::filesystem::path(SHIPYARD_SOURCE_DIR) / relative;
  std::ifstream stream(path, std::ios::binary);
  require(stream.is_open(), "Cannot read test input: " + relative.string());
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

inline SchemaSet schemas() {
  auto result =
      SchemaSet::load(std::filesystem::path(SHIPYARD_SOURCE_DIR) / "schemas");
  require(result.ok(), "Bundled schemas must load");
  return std::move(*result.value);
}

inline RulesSnapshot rules(const SchemaSet &schema_set) {
  auto result = load_rules(read("rules/apsis-rules-v4.json"), schema_set);
  require(result.ok(), "Bundled v4 rules must load");
  return std::move(*result.value);
}

inline Json blank(std::string_view build_type = "ship") {
  return {{"schema", "apsis-shipyard-v4"},
          {"name", "Blank"},
          {"buildType", build_type},
          {"budget", 100000},
          {"loads", {{"cargo", 0}, {"jumpFuel", 1}, {"sublightFuel", 1}}},
          {"decks", Json::array({{{"name", "Main deck"},
                                  {"floors", Json::object()},
                                  {"modules", Json::array()}}})},
          {"nextId", 1},
          {"migrationNotes", Json::array()}};
}

inline bool issue(const std::vector<Issue> &issues, std::string_view code,
                  std::string_view path) {
  return std::any_of(issues.begin(), issues.end(), [&](const Issue &item) {
    return item.code == code && item.path == path && !item.message.empty();
  });
}

class Suite {
public:
  template <class Function>
  void run(const std::string &name, Function &&function) {
    ++cases_;
    try {
      std::forward<Function>(function)();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
      ++failures_;
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
  }

  int finish() const {
    std::cout << cases_ << " cases, " << failures_ << " failures\n";
    return failures_ == 0 ? 0 : 1;
  }

private:
  unsigned cases_ = 0;
  unsigned failures_ = 0;
};
} // namespace test
