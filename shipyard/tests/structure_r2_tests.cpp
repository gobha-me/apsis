#include "dock_test_support.hpp"

#include <array>
#include <string>

namespace {
using namespace dock_test;

const Json &finding(const Json &report, std::string_view code,
                    std::string_view path) {
  const auto &issues = report.at("issues");
  const auto found = std::find_if(issues.begin(), issues.end(),
                                  [&](const Json &item) {
    return item.at("code") == code && item.at("path") == path;
  });
  require(found != issues.end(), "Missing " + std::string(code) + " at " +
                                     std::string(path) + ": " + issues.dump());
  require(found->at("severity") == "error" &&
              !found->at("message").get<std::string>().empty(),
          "Structural faults must be readable errors");
  return *found;
}
} // namespace

int main() {
  using namespace dock_test;
  Suite suite;
  const auto schema_set = schemas();
  const auto active = rules(schema_set);

  suite.run("N02: enclosed command retains command-rooted hull connectivity", [&] {
    auto source = station();
    require(validate(source.dump(), active, schema_set).at("pass") == true,
            "Unenclosed station is a valid control");
    for (const auto &[x, y] :
         std::array{std::pair{3, 2}, std::pair{2, 3}, std::pair{4, 3},
                    std::pair{3, 4}})
      add_module(source, "microReactor", x, y);
    const auto report = validate(source.dump(), active, schema_set);
    require(report.at("pass") == false,
            "An enclosed command station must fail crew access");
    require(finding(report, "command_access", "/blueprint/decks/0/modules/0")
                    .at("details").at("moduleId") == 1,
            "The enclosed command module must be identified");
    require(!has_issue(report, "disconnected_hull"),
            "Connected hull must flood from the command origin even without crew access");
  });

  suite.run("N03: bridge service selection prefers bottom over top", [&] {
    auto source = blank("station");
    fill_rectangle(source, 1, 4, 7, 1);
    fill_rectangle(source, 1, 6, 7, 1);
    source["decks"][0]["floors"]["4,5"] = true;
    add_module(source, "cockpit", 4, 5);
    add_module(source, "microReactor", 1, 4);
    add_module(source, "microLifeSupport", 7, 6);
    const auto report = validate(source.dump(), active, schema_set);
    require(report.at("pass") == false,
            "Separate crew routes leave the top-row reactor unreachable");
    std::size_t unreachable = 0;
    for (const auto &item : report.at("issues")) {
      if (item.at("code") != "unreachable_component")
        continue;
      ++unreachable;
      require(item.at("path") == "/blueprint/decks/0/modules/1" &&
                  item.at("details").at("moduleId") == 2,
              "Bottom-first bridgeStart must leave reactor index 1 unreached");
    }
    require(unreachable == 1,
            "Only the top-row reactor may be unreachable from the bottom service cell");
    finding(report, "unreachable_component", "/blueprint/decks/0/modules/1");
    require(!has_issue(report, "unreachable_component",
                       "/blueprint/decks/0/modules/2") &&
                !has_issue(report, "disconnected_hull"),
            "The bottom-row life support is reached and the command joins the hull rows");
  });

  suite.run("N06: missing command does not invent unreachable components", [&] {
    auto source = station();
    auto &modules = source["decks"][0]["modules"];
    require(modules[0].at("type") == "cockpit",
            "Station helper must place its command module first");
    modules.erase(modules.begin());
    const auto report = validate(source.dump(), active, schema_set);
    require(report.at("pass") == false,
            "A station without its required bridge must fail");
    require(finding(report, "required_system", "/blueprint/decks")
                    .at("details").at("system") == "bridge",
            "The missing command must be reported as a required bridge");
    require(!has_issue(report, "unreachable_component"),
            "Crew reachability findings require an existing command module");
  });

  suite.run("N07: disabling crew routes retains local hull connectivity", [&] {
    const auto imported = import_definition(
        read("fixtures/v3/apsis-wren-definition-v3.json"), active, schema_set);
    require(imported.ok(), "Canonical Wren must import through the public loader");
    auto source = imported.value->definition.at("blueprint");
    source["decks"].push_back({{"name", "Loft"},
                                 {"floors", {{"0,0", true}, {"2,0", true}}},
                                 {"modules", Json::array()}});
    ValidationOptions options;
    options.crewRoutes = false;
    const auto report = validate(source.dump(), active, schema_set, options);
    require(report.at("pass") == false,
            "Disabling crew checks must not certify disconnected local floors");
    require(finding(report, "disconnected_hull", "/blueprint/decks/1/floors")
                    .at("details").at("components") == 2,
            "The disconnected Loft floor regions must remain a structural fault");
    require(report.at("data").at("checks").at("crewRoutes") == false,
            "The public options seam must confirm that crew routes were disabled");
  });

  return suite.finish();
}
