#include "dock_test_support.hpp"

#include <apsis/shipyard/perf.hpp>
#include <apsis/shipyard/render.hpp>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace {
using namespace dock_test;

Json hold(int first = 0, int last = 2, int x = 8, int y = 5, int w = 3, int h = 4) {
  return {{"x", x}, {"y", y}, {"w", w}, {"h", h},
          {"firstDeck", first}, {"lastDeck", last}};
}

Json volume(int count = 3, int ladder_x = 7, int ladder_y = 6,
            Json record = hold()) {
  auto design = base("station");
  const auto floors = design.at("decks")[0].at("floors");
  for (int d = 1; d < count; ++d)
    design["decks"].push_back({{"name", "Deck " + std::to_string(d + 1)},
                               {"floors", floors}, {"modules", Json::array()}});
  for (int d = 0; d < count; ++d) {
    const auto id = design.at("nextId").get<int>();
    design["decks"][d]["modules"].push_back(
        {{"id", id}, {"type", "ladder"}, {"x", ladder_x}, {"y", ladder_y}, {"r", 0}});
    design["nextId"] = id + 1;
    if (d > 0)
      design["decks"][d]["floors"].erase(
          std::to_string(record.at("x").get<int>() + 1) + "," +
          std::to_string(record.at("y").get<int>() + 1));
  }
  design["holds"] = Json::array({record});
  return design;
}

const Json &finding(const Json &report, std::string_view code, std::string_view path) {
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code && item.at("path") == path) {
      require(item.at("severity") == "error" && !item.at("message").get<std::string>().empty(),
              "Hold failures retain readable structured errors");
      return item;
    }
  throw std::runtime_error("Missing " + std::string(code) + " at " + std::string(path) + ": " + report.dump());
}

void accepted(const Json &input, const RulesSnapshot &active, const SchemaSet &schema) {
  const auto result = validate(input.dump(), active, schema);
  require(result.at("pass") == true, "Real hold design must validate: " + result.at("issues").dump());
  for (const auto &item : result.at("issues")) {
    const auto code = item.at("code").get<std::string>();
    require(!code.starts_with("pending_F1") && code.find("railing") == std::string::npos &&
                code.find("fall") == std::string::npos,
            "Luna, under owner delegation 2026-11-23: decided catwalks need no fall rules");
  }
  const auto &output = result.at("data").at("definition").at("blueprint");
  require(output == input, "Hold import preserves all ordinary deck names, indices, material and IDs");
}

void fill(Json &design, int deck, int x, int y, int w, int h) {
  for (int a = x; a < x + w; ++a)
    for (int b = y; b < y + h; ++b)
      design["decks"][deck]["floors"][std::to_string(a) + "," + std::to_string(b)] = true;
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  Suite suite;
  suite.run("Luna hold spans use two through the actual full ship deck count", [&] {
    const int maximum = active.data().at("limits").at("maxDecks").get<int>();
    for (int decks = 2; decks <= maximum; ++decks)
      accepted(volume(decks, 7, 6, hold(0, decks - 1)), active, schema);
    accepted(volume(4, 7, 6, hold(1, 3)), active, schema);
  });
  suite.run("hold work reuses repeated records and bounds distinct geometry", [&] {
    const auto opening = hold(0, 1, 5, 6, 12, 12);
    auto design = volume(2, 7, 6, opening);
    for (int deck : {0, 1})
      fill(design, deck, 2, 2, 24, 22);
    design["decks"][1]["floors"].erase("14,12");
    const auto port = add_module(design, "dockingPort", 6, 22);

    design["holds"] = std::vector<Json>(2048, opening);
    accepted(design, active, schema);

    // Every rectangle shares the opening and the paired ladder route.
    auto records = Json::array();
    for (int y = 6; y <= 12; ++y)
      for (int x = 5; x <= 14; ++x)
        for (int right = 17; right <= 21; ++right)
          for (int bottom = 18; bottom <= 19; ++bottom)
            records.push_back(hold(0, 1, x, y, right - x, bottom - y));
    // On this fixture, both decks are labelled once. Each distinct record
    // adds two deck visits, both route scans, the upper floor scan, three
    // comparisons per module, and two aligned ladder checks. Every record
    // also costs one unit, including each repeat used to reach equality.
    constexpr int limit = 1'000'000;
    constexpr int shared_labels = 528 + 527;
    constexpr int distinct_work = 1 + 2 + (528 + 527) + 527 + 3 * 7 + 2;
    constexpr int distinct_records = (limit - shared_labels) / distinct_work;
    constexpr int repeated_records = (limit - shared_labels) % distinct_work;
    design["holds"] = Json(records.begin(), records.begin() + distinct_records);
    for (int i = 0; i < repeated_records; ++i)
      design["holds"].push_back(opening);
    accepted(design, active, schema);

    design["holds"].push_back(opening);
    const auto source = design.dump();
    const auto limited = [&](const Json &report, std::string_view path) {
      require(report.at("status") == "FAIL" && report.at("pass") == false &&
                  report.at("data").is_null(),
              "hold work exhaustion must return a terminal public failure");
      require(finding(report, "structure_resource_limit", path)
                      .at("details").at("limit") == limit,
              "the diagnostic must expose the structural work limit");
    };
    limited(validate(source, active, schema), "/blueprint/holds");
    limited(perf(source, active, schema), "/blueprint/holds");
    DockOptions options;
    options.structural = false;
    limited(dock(source, 0, port, visitor().dump(), active, schema, options),
            "/host/blueprint/holds");
    const auto rendered = render(source, active, schema);
    limited(rendered.envelope, "/blueprint/holds");
    require(rendered.artifacts.empty(),
            "the renderer must emit no artifacts after work exhaustion");
  });
  suite.run("hold import rejects one deck, reversal and inclusive past-end spans precisely", [&] {
    for (const auto &span : {std::pair{0, 0}, std::pair{2, 1}, std::pair{0, 3}}) {
      auto design = volume();
      design["holds"][0]["firstDeck"] = span.first;
      design["holds"][0]["lastDeck"] = span.second;
      const auto result = import_definition(design.dump(), active, schema);
      require(!result.ok() && issue(result.issues, "hold_span", "/holds/0"),
              "Invalid hold span rejects in public import");
      for (const auto &item : result.issues)
        if (item.code == "hold_span")
          require(item.details == Json{{"firstDeck", span.first}, {"lastDeck", span.second},
                                       {"minimum", 2}, {"deckCount", 3}},
                  "Span diagnostics identify independently supplied endpoints and design count");
    }
  });
  suite.run("legacy v4 rules without additive holds policy retain the settled minimum two", [&] {
    auto changed = active.data();
    changed.erase("holds");
    const auto legacy = load_rules(changed.dump(), schema);
    require(legacy.ok(), "Previously valid rules snapshots stay loadable");
    accepted(volume(2, 7, 6, hold(0, 1)), *legacy.value, schema);
    auto single = volume(2, 7, 6, hold(0, 0));
    const auto report = validate(single.dump(), *legacy.value, schema);
    require(finding(report, "hold_span", "/holds/0").at("details").at("minimum") == 2,
            "Fallback is exactly Luna minimum2, with the strong span guard retained");
  });
  suite.run("hold rectangles obey exact active grid edges without overflow or schema shortcuts", [&] {
    const int width = active.data().at("grid").at("w").get<int>();
    const int height = active.data().at("grid").at("h").get<int>();
    auto design = volume();
    design["holds"][0] = hold(0, 2, width - 1, height - 1, 1, 1);
    require(import_definition(design.dump(), active, schema).ok(), "Last grid cell is inclusive");
    for (const auto &record : {hold(0, 2, width, 0, 1, 1), hold(0, 2, 0, height, 1, 1),
                              hold(0, 2, 0, 0, width + 1, 1), hold(0, 2, 0, 0, 1, height + 1)}) {
      design["holds"][0] = record;
      const auto report = validate(design.dump(), active, schema);
      const auto &item = finding(report, "hold_bounds", "/holds/0");
      require(item.at("details") == Json{{"gridWidth", width}, {"gridHeight", height}},
              "Bounds diagnostics use the active grid");
    }
    design["holds"][0] = hold();
    design["holds"][0]["w"] = 18446744073709551615ULL;
    require(!import_definition(design.dump(), active, schema).ok(), "Huge positive width never wraps into the grid");
    for (const auto &bad : {Json(0), Json::object(), Json::array({Json{{"x", 0}}})}) {
      design["holds"] = bad;
      const auto report = validate(design.dump(), active, schema);
      require(report.at("data").is_null() && has_issue(report, "schema_error"),
              "Malformed holds retain normal schema rejection");
    }
    for (const auto *field : {"x", "y", "w", "h", "firstDeck", "lastDeck"}) {
      design["holds"] = Json::array({hold()});
      design["holds"][0][field] = 0.5;
      require(!import_definition(design.dump(), active, schema).ok(), "Hold coordinates and indices are integers");
    }
  });
  suite.run("hold full-grid width and height equalities import without shrinking the volume", [&] {
    const int width = active.data().at("grid").at("w").get<int>();
    const int height = active.data().at("grid").at("h").get<int>();
    require(width == 26 && height == 24 && active.data().at("cellSizeM") == 2,
            "Bundled physical grid gives independent52m/48m exact outer boundaries");
    for (const auto &record : {hold(0, 2, 0, 5, width, 4), hold(0, 2, 8, 0, 3, height)}) {
      auto design = volume(); design["holds"][0] = record;
      const auto result = import_definition(design.dump(), active, schema);
      require(result.ok() && result.value->definition.at("blueprint").at("holds") == Json::array({record}),
              "Full active-grid width/height equality is accepted and preserved exactly");
      const auto &derived = result.value->definition.at("derived");
      require(record.at("w") == width ? derived.at("widthM") == 52 : derived.at("lengthM") == 48,
              "Each full-grid dimension reaches its independent inclusive physical extent");
      require(std::none_of(result.issues.begin(), result.issues.end(), [](const Issue &item) {
                return item.code == "hold_bounds";
              }), "Equality must not become an out-of-bounds diagnostic");
    }
  });
  suite.run("hold integer fields reject huge finite integral doubles before semantic casts", [&] {
    for (const auto *field : {"x", "y", "w", "h", "firstDeck", "lastDeck"}) {
      for (const auto &too_large : {Json(1e100), Json(9007199254740992ULL)}) {
        auto design = volume();
        design["holds"][0][field] = too_large;
        const auto result = import_definition(design.dump(), active, schema);
        require(!result.ok() && issue(result.issues, "schema_error", "/holds/0/" + std::string(field)),
                "Each new integer field rejects outside exact interoperable range at its own schema path");
        require(std::none_of(result.issues.begin(), result.issues.end(), [](const Issue &item) {
                  return item.code == "hold_bounds" || item.code == "hold_span";
                }), "Schema rejects huge integral floats before converted semantic hold checks");
      }
    }
  });
  suite.run("declared hold volume contributes exact bounds while all material totals stay unchanged", [&] {
    auto sparse = blank();
    sparse["decks"].push_back(sparse["decks"][0]);
    sparse["holds"] = Json::array({hold(0, 1, 20, 15, 6, 9)});
    const auto empty_volume = import_definition(sparse.dump(), active, schema);
    require(empty_volume.ok(), "Material-free declared volume remains an inspectable import");
    const auto &zero = empty_volume.value->definition.at("derived");
    require(zero.at("widthM") == 12 && zero.at("lengthM") == 18 &&
                zero.at("floorCount") == 0 && zero.at("areaM2") == 0 &&
                zero.at("cost") == 0 && zero.at("dryMass") == 0,
            "A6x9-cell hold supplies12x18m bounds with no synthetic material");
    auto design = volume();
    const auto baseline = import_definition(design.dump(), active, schema);
    require(baseline.ok(), "Ordinary hold source imports before bbox extension");
    struct Bounds { int x, y, w, h, width, length; };
    for (const auto b : {Bounds{0, 5, 11, 4, 24, 20}, Bounds{8, 0, 3, 11, 20, 24},
                         Bounds{8, 5, 12, 4, 36, 20}, Bounds{8, 5, 3, 15, 20, 36},
                         Bounds{0, 0, 20, 20, 40, 40}}) {
      design["holds"][0] = hold(0, 2, b.x, b.y, b.w, b.h);
      const auto imported = import_definition(design.dump(), active, schema);
      require(imported.ok(), "Declared rectangle inside grid imports");
      auto actual = imported.value->definition.at("derived");
      auto original = baseline.value->definition.at("derived");
      require(actual.at("widthM") == b.width && actual.at("lengthM") == b.length,
              "All four bbox extrema use declared volume and actual floors, including inclusive end cells");
      actual.erase("widthM"); actual.erase("lengthM");
      original.erase("widthM"); original.erase("lengthM");
      require(actual == original,
              "Every material/capacity/cost/mass/area total remains exactly equal when only empty volume bounds grow");
    }
    auto rule_data = active.data();
    for (auto &part : rule_data["parts"]) if (part.at("id") == "stationHangar") {
      part["docking"]["maxWidthM"] = 20;
      part["docking"]["maxLengthM"] = 20;
    }
    const auto loaded = load_rules(rule_data.dump(), schema);
    require(loaded.ok(), "Active exact20x20 hangar envelope loads through public API");
    const auto host = read("fixtures/v3/apsis-wayfarer-anchorage-definition-v3.json");
    DockOptions options; options.structural = false; options.portReachability = false;
    options.matchingAirlock = false; options.approachClearance = false;
    auto ship = volume(2, 7, 6, hold(0, 1)); ship["buildType"] = "ship";
    const auto fits = dock(host, 1, 15, ship.dump(), *loaded.value, schema, options);
    require(fits.at("pass") == true && fits.at("data").at("hangar").at("widthLengthFit") == true,
            "Actual20x20m material envelope is an inclusive positive docking control");
    ship["holds"][0] = hold(0, 1, 0, 0, 20, 20);
    const auto too_wide = dock(host, 1, 15, ship.dump(), *loaded.value, schema, options);
    const auto &hangar = too_wide.at("data").at("hangar");
    require(too_wide.at("pass") == false && hangar.at("visitorWidthM") == 40 &&
                hangar.at("visitorLengthM") == 40 && hangar.at("widthLengthFit") == false &&
                has_issue(too_wide, "dock_hangar_size_limit"),
            "Declared empty hold volume participates in the real public hangar envelope rejection");
  });
  suite.run("all hold records are imported and structurally checked at their exact second index", [&] {
    const auto control = volume();
    accepted(control, active, schema);
    for (const auto &bad : {hold(0, 2, 26, 5, 1, 4), hold(0, 0)}) {
      auto design = control;
      design["holds"].push_back(bad);
      const auto result = import_definition(design.dump(), active, schema);
      const auto code = bad.at("x") == 26 ? "hold_bounds" : "hold_span";
      require(!result.ok() && issue(result.issues, code, "/holds/1"),
              "A valid first hold cannot hide the second record importer bounds/span failure");
      require(std::none_of(result.issues.begin(), result.issues.end(), [](const Issue &item) {
                return (item.code == "hold_bounds" || item.code == "hold_span") && item.path == "/holds/0";
              }), "The valid first declaration retains its independent identity");
    }
    auto structural = control;
    // The middle deck covers this corner and the top deck has no route to it.
    auto &top_floors = structural["decks"][2]["floors"];
    for (const auto *cell : {"2,9", "3,9", "2,10", "3,10", "4,10", "2,11", "3,11", "4,11"})
      top_floors.erase(cell);
    accepted(structural, active, schema);
    structural["holds"].push_back(hold(0, 2, 2, 10, 2, 2));
    require(import_definition(structural.dump(), active, schema).ok(),
            "Second hold is a well-shaped declaration requiring real structural inspection");
    const auto checked = validate(structural.dump(), active, schema);
    require(checked.at("pass") == false && !checked.at("data").is_null(),
            "Second structural hold failure retains inspectable validation data");
    finding(checked, "hold_access", "/blueprint/holds/1");
    finding(checked, "hold_open_column", "/blueprint/holds/1");
    require(!has_issue(checked, "hold_access", "/blueprint/holds/0") &&
                !has_issue(checked, "hold_open_column", "/blueprint/holds/0"),
            "The later findings must belong only to the second declaration");
  });
  suite.run("partial mezzanines need a single common full-height open column", [&] {
    accepted(volume(), active, schema);
    auto blocked = volume();
    blocked["decks"][1]["floors"]["9,6"] = true;
    blocked["holds"].push_back(hold());
    const auto report = validate(blocked.dump(), active, schema);
    const auto &item = finding(report, "hold_open_column", "/blueprint/holds/0");
    require(item.at("details").at("firstDeck") == 0 && item.at("details").at("lastDeck") == 2,
            "A completely covered intermediate plane blocks the whole declared span");
    require(finding(report, "hold_open_column", "/blueprint/holds/1").at("details") ==
                item.at("details"),
            "Reusing a geometry check must retain findings at each record path");
    auto staggered = volume();
    staggered["decks"][2]["floors"]["9,6"] = true;
    staggered["decks"][2]["floors"].erase("10,6");
    finding(validate(staggered.dump(), active, schema), "hold_open_column", "/blueprint/holds/0");
    auto open_bottom = volume();
    open_bottom["decks"][0]["floors"].erase("9,6");
    accepted(open_bottom, active, schema);
  });
  suite.run("ladder chain must include first, middle and last deck", [&] {
    for (int missing : {0, 1, 2}) {
      auto design = volume();
      auto &modules = design["decks"][missing]["modules"];
      modules.erase(std::remove_if(modules.begin(), modules.end(), [](const Json &m) {
        return m.at("type") == "ladder";
      }), modules.end());
      finding(validate(design.dump(), active, schema), "hold_access", "/blueprint/holds/0");
    }
    auto split = volume(4, 7, 6, hold(0, 3));
    for (int d : {2, 3}) split["decks"][d]["modules"][0]["y"] = 7;
    finding(validate(split.dump(), active, schema), "hold_access", "/blueprint/holds/0");
  });
  suite.run("hold access needs a cardinal route around an occupied floor", [&] {
    accepted(volume(), active, schema);
    accepted(volume(3, 8, 5), active, schema);
    accepted(volume(3, 5, 6), active, schema); // Several cells from the hold.
    accepted(volume(3, 6, 4), active, schema); // Offset on both axes.

    for (const auto &anchor : {std::pair{9, 7}, std::pair{7, 5}, std::pair{11, 6},
                               std::pair{8, 4}, std::pair{9, 9}}) {
      auto sparse = volume(3, anchor.first, anchor.second);
      const auto cell = std::to_string(anchor.first) + "," + std::to_string(anchor.second);
      for (int d : {1, 2})
        sparse["decks"][d]["floors"] = {{cell, true}};
      accepted(sparse, active, schema);
    }

    auto catwalk = volume(3, 5, 7);
    auto &middle_deck = catwalk["decks"][1];
    middle_deck["floors"] = {{"5,6", true}, {"6,6", true}, {"7,6", true},
                              {"5,7", true}, {"6,7", true}};
    accepted(catwalk, active, schema);
    const auto seats = catwalk.at("nextId").get<int>();
    catwalk["nextId"] = seats + 1;
    middle_deck["modules"].push_back(
        {{"id", seats}, {"type", "shuttleSeats"}, {"x", 6}, {"y", 6}, {"r", 0}});
    const auto blocked = validate(catwalk.dump(), active, schema);
    finding(blocked, "hold_access", "/blueprint/holds/0");
    require(blocked.at("pass") == false && !has_issue(blocked, "disconnected_hull"),
            "A diagonal step past the seats cannot connect the walkways even on a connected hull");
    middle_deck["floors"]["7,7"] = true;
    accepted(catwalk, active, schema);

    auto bottom_route = volume(3, 13, 6);
    for (int d : {0, 1, 2}) fill(bottom_route, d, 12, 6, 2, 1);
    accepted(bottom_route, active, schema);
    add_module(bottom_route, "shuttleSeats", 12, 6);
    finding(validate(bottom_route.dump(), active, schema), "hold_access", "/blueprint/holds/0");

    auto missing_floor = volume();
    missing_floor["decks"][1]["floors"].erase("7,6");
    const auto report = validate(missing_floor.dump(), active, schema);
    require(report.at("pass") == false && has_issue(report, "missing_floor"),
            "Declaring volume must not synthesize ladder support material");
    auto isolated = volume();
    isolated["decks"][1]["floors"] = {{"7,6", true}};
    isolated["decks"][2]["floors"] = {{"7,6", true}, {"9,8", true}};
    const auto unreachable = validate(isolated.dump(), active, schema);
    require(unreachable.at("pass") == false && has_issue(unreachable, "disconnected_hull"),
            "Empty volume does not bridge an isolated catwalk cell");
  });
  suite.run("hold metadata adds no material while actual ladders retain catalogue cost and mass", [&] {
    const auto design = volume();
    auto without = design;
    without.erase("holds");
    const auto with = import_definition(design.dump(), active, schema);
    const auto plain = import_definition(without.dump(), active, schema);
    require(with.ok() && plain.ok() && with.value->definition.at("derived") == plain.value->definition.at("derived"),
            "Spatial hold metadata contributes no invented volume totals");
    for (auto &deck : without["decks"])
      deck["modules"].erase(std::remove_if(deck["modules"].begin(), deck["modules"].end(),
          [](const Json &m) { return m.at("type") == "ladder"; }), deck["modules"].end());
    const auto bare = import_definition(without.dump(), active, schema);
    require(bare.ok(), "Independent floor-and-systems source imports");
    const auto &actual = with.value->definition.at("derived");
    const auto &prior = bare.value->definition.at("derived");
    require(actual.at("cost").get<double>() - prior.at("cost").get<double>() == 750 &&
                std::abs(actual.at("componentMass").get<double>() - prior.at("componentMass").get<double>() - 0.6) < 1e-10,
            "Three actual catalogue ladders cost3x250 and weigh3x0.2, exactly once");
    require(actual.at("floorCount") == 298 && actual.at("cargo") == 0,
            "Independent real floor count and zero cargo-module capacity stay unchanged");
  });
  suite.run("a hold spanning upper decks is neutral to calculated cargo and mass", [&] {
    auto station = volume(4, 7, 6, hold(1, 3));
    add_module(station, "cargo", 3, 7);
    station["loads"]["cargo"] = 0.25;
    auto without_hold = station;
    without_hold.erase("holds");
    const double epsilon = active.data().at("numeric").at("epsilon").get<double>();
    for (const auto &[override, fraction] :
         {std::pair{LoadFractions{}, 0.25}, std::pair{LoadFractions{.cargo = 0.75}, 0.75},
          std::pair{LoadFractions{.cargo = 0.0}, 0.0}}) {
      const auto checked = perf(station.dump(), active, schema, override);
      const auto reference = perf(without_hold.dump(), active, schema, override);
      const auto &data = checked.at("data");
      const auto &reference_data = reference.at("data");
      require(checked.at("pass") == true && reference.at("pass") == true &&
                  data.at("structuralPass") == true && reference_data.at("structuralPass") == true,
              "Declaring a hold over the same station must preserve its successful performance report");
      const auto &definition = data.at("definition");
      auto expected_blueprint = station;
      expected_blueprint["loads"]["cargo"] = fraction;
      require(definition.at("blueprint") == expected_blueprint,
              "The calculated definition must preserve the upper-deck hold and effective cargo load");
      const auto &derived = definition.at("derived");
      const auto &performance = data.at("performance");
      require(derived == reference_data.at("definition").at("derived") &&
                  performance == reference_data.at("performance"),
              "Adding the hold must leave all derived values and scenarios equal");
      const double cargo_mass = fraction * 10;
      require(derived.at("floorCount") == 397 && derived.at("cargo") == 10 &&
                  performance.at("cargoMass") == cargo_mass &&
                  performance.at("currentMass") == derived.at("currentMass") &&
                  std::abs(derived.at("currentMass").get<double>() -
                           derived.at("dryMass").get<double>() - cargo_mass) <= epsilon,
              "Only the 397 real floors and the 10-unit cargo module contribute to these totals");
    }
  });
  suite.run("hangar touching edge is allowed; positive shared-deck overlap rejects", [&] {
    auto design = volume();
    add_module(design, "stationHangar", 11, 5);
    fill(design, 0, 11, 5, 5, 7);
    accepted(design, active, schema);
    design["holds"][0]["w"] = 4;
    finding(validate(design.dump(), active, schema), "hold_hangar_overlap", "/blueprint/holds/0");
    design["holds"][0]["firstDeck"] = 1;
    accepted(design, active, schema);
  });
  suite.run("hold geometry diagnostics follow the invalid blueprint check", [&] {
    const auto record = hold(0, 1);
    auto sealed = volume(2, 7, 6, record);
    sealed["decks"][1]["floors"]["9,6"] = true;
    auto isolated = volume(2, 6, 6, record);
    isolated["decks"][1]["floors"] = {{"6,6", true}};
    auto hangar = volume(2, 7, 6, record);
    add_module(hangar, "stationHangar", 10, 7);
    fill(hangar, 0, 10, 7, 5, 7);

    auto rule_data = active.data();
    auto &validation = rule_data["validation"];
    validation["defaultSeverity"] = "warning";
    for (bool reject : {true, false}) {
      const auto *severity = reject ? "error" : "warning";
      validation["checks"]["invalid_blueprint"]["severity"] = severity;
      const auto policy = load_rules(rule_data.dump(), schema);
      require(policy.ok(), "The default and the explicit shape severity are configurable");
      accepted(volume(2, 7, 6, record), *policy.value, schema);
      for (const auto &[design, code] : {std::pair{&sealed, "hold_open_column"},
                                         std::pair{&isolated, "hold_access"},
                                         std::pair{&hangar, "hold_hangar_overlap"}}) {
        const auto report = validate(design->dump(), *policy.value, schema);
        const auto &diagnostics = report.at("issues");
        const auto matches = std::count_if(diagnostics.begin(), diagnostics.end(), [&](const Json &item) {
          return item.at("code") == code && item.at("path") == "/blueprint/holds/0" &&
                 item.at("severity") == severity && !item.at("message").get<std::string>().empty() &&
                 item.at("details").at("rule") == "/validation/checks/invalid_blueprint";
        });
        const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(), [](const Json &item) {
          return item.at("severity") == "error";
        });
        require(matches == 1 && errors == (reject ? 1 : 0) &&
                    report.at("pass") == !reject && !report.at("data").is_null(),
                std::string(code) + " must follow the explicit shape severity: " + diagnostics.dump());
      }
    }
  });
  suite.run("hangar overlap on only the inclusive last hold deck is still forbidden", [&] {
    auto design = volume(4, 7, 6, hold(0, 2));
    const int id = design.at("nextId").get<int>();
    design["nextId"] = id + 1;
    const Json hangar = {{"id", id}, {"type", "stationHangar"}, {"x", 11}, {"y", 5}, {"r", 0}};
    design["decks"][2]["modules"].push_back(hangar);
    fill(design, 2, 11, 5, 5, 7);
    accepted(design, active, schema);
    design["holds"][0]["w"] = 4;
    const auto overlap = validate(design.dump(), active, schema);
    const auto &item = finding(overlap, "hold_hangar_overlap", "/blueprint/holds/0");
    require(overlap.at("pass") == false && item.at("details").at("deck") == 2 &&
                item.at("details").at("moduleId") == id,
            "Actual hangar overlaps the same hold only on its inclusive last deck2");
    design["decks"][2]["modules"].erase(design["decks"][2]["modules"].end() - 1);
    design["decks"][3]["modules"].push_back(hangar);
    fill(design, 3, 11, 5, 5, 7);
    accepted(design, active, schema);
  });
  suite.run("hangar collision uses actual rotated footprint", [&] {
    auto design = volume();
    const auto id = add_module(design, "stationHangar", 3, 8);
    fill(design, 0, 3, 8, 7, 7);
    ValidationOptions options; options.approachClearance = false;
    const auto unrotated = validate(design.dump(), active, schema, options);
    require(!has_issue(unrotated, "hold_hangar_overlap"), "Five-cell hangar width touches x8 hold edge");
    for (auto &m : design["decks"][0]["modules"]) if (m.at("id") == id) m["r"] = 1;
    finding(validate(design.dump(), active, schema, options), "hold_hangar_overlap", "/blueprint/holds/0");
  });
  suite.run("host hold span never consumes a hangar visitor deck allowance", [&] {
    auto host = volume(3);
    const auto id = add_module(host, "stationHangar", 11, 5);
    fill(host, 0, 11, 5, 5, 7);
    accepted(host, active, schema);
    DockOptions options; options.matchingAirlock = false;
    auto ship = visitor();
    const auto ladder = add_module(ship, "ladder", 8, 6);
    for (int d = 1; d <= 2; ++d) {
      ship["decks"].push_back({{"name", "Visitor deck " + std::to_string(d)},
        {"floors", {{"8,6", true}}}, {"modules", Json::array({{{"id", ladder + d},
          {"type", "ladder"}, {"x", 8}, {"y", 6}, {"r", 0}}})}});
      ship["nextId"] = ladder + d + 1;
      const auto report = dock(host.dump(), 0, id, ship.dump(), active, schema, options);
      const auto &hangar = report.at("data").at("hangar");
      require(hangar.at("visitorDeckCount") == d + 1 && hangar.at("maxDecks") == 2 &&
                  hangar.at("deckCountFits") == (d == 1) &&
                  has_issue(report, "dock_hangar_deck_limit") == (d == 2),
              "Only actual visitor deck count consumes the independent hangar cap");
    }
  });
  suite.run("cargo and standard airlocks can open directly into real hold catwalks", [&] {
    for (const auto *interface : {"cargo", "standard"}) {
      auto design = volume(3, 4, 5, hold(0, 2, 2, 4, 2, 4));
      design["decks"][0]["modules"][3]["interface"] = interface;
      accepted(design, active, schema);
    }
    auto design = volume(3, 7, 6, hold(0, 2, 4, 5, 3, 5));
    const auto port = add_module(design, "dockingPort", 2, 7, 1, "cargo");
    accepted(design, active, schema);
    for (auto &m : design["decks"][0]["modules"]) if (m.at("id") == port) m["r"] = 3;
    for (int x = 4; x < 26; ++x) for (int y = 7; y < 9; ++y)
      design["decks"][0]["floors"].erase(std::to_string(x) + "," + std::to_string(y));
    const auto report = validate(design.dump(), active, schema);
    require(report.at("pass") == false && has_issue(report, "blocked_approach"),
            "An empty interior hold is never mistaken for clear exterior cargo approach");
    DockOptions options; options.structural = false;
    options.portReachability = false; options.matchingAirlock = false;
    const auto docked = dock(design.dump(), 0, port, visitor().dump(), active, schema, options);
    const auto &outward = docked.at("data").at("checks").at("d");
    require(outward.at("status") == "FAIL" &&
                outward.at("details").at("exteriorOutward") == false &&
                outward.at("details").at("approachClear") == false,
            "With all actual outward floors removed, hold volume alone remains interior and obstructs exterior approach");
  });
  suite.run("cargo port sees declared interior on both inclusive hold endpoint decks", [&] {
    const int grid_width = active.data().at("grid").at("w").get<int>();
    for (const int deck : {0, 2}) {
      auto design = volume(3, 7, 6, hold(0, 2, 4, 5, 3, 5));
      const auto id = design.at("nextId").get<int>();
      design["nextId"] = id + 1;
      const auto index = design["decks"][deck]["modules"].size();
      design["decks"][deck]["modules"].push_back({{"id", id}, {"type", "dockingPort"},
          {"x", 2}, {"y", 7}, {"r", 1}, {"interface", "cargo"}});
      accepted(design, active, schema);
      design["decks"][deck]["modules"][index]["r"] = 3;
      for (int x = 4; x < grid_width; ++x) for (int y = 7; y < 9; ++y)
        design["decks"][deck]["floors"].erase(std::to_string(x) + "," + std::to_string(y));
      for (int x = 4; x < grid_width; ++x) for (int y = 7; y < 9; ++y)
        require(!design["decks"][deck]["floors"].contains(std::to_string(x) + "," + std::to_string(y)),
                "The complete real outward floor ray is absent through the active grid edge");
      const std::string path = "/blueprint/decks/" + std::to_string(deck) +
                               "/modules/" + std::to_string(index);
      const auto checked = validate(design.dump(), active, schema);
      finding(checked, "blocked_approach", path);
      DockOptions options; options.structural = false;
      options.portReachability = false; options.matchingAirlock = false;
      const auto docked = dock(design.dump(), deck, id, visitor().dump(), active, schema, options);
      const auto &target = docked.at("data").at("target");
      const auto &outward = docked.at("data").at("checks").at("d");
      require(target.at("deck") == deck && target.at("moduleId") == id &&
                  outward.at("status") == "FAIL" &&
                  outward.at("details").at("exteriorOutward") == false &&
                  outward.at("details").at("approachClear") == false,
              "Both first and last spanned deck retain interior classification and obstructed approach with no real outward floors");
      finding(docked, "dock_blocked_approach", "/host" + path);
    }
  });
  suite.run("carried hold volume uses each exact envelope edge independently of material", [&] {
    const auto child_at = [](int dx, int dy) {
      auto child = blank(); child["name"] = "Real carried hold";
      child["decks"].push_back({{"name", "Upper catwalk"}, {"floors", Json::object()},
                                {"modules", Json::array()}});
      for (int d = 0; d < 2; ++d)
        for (int x = 0; x < 3; ++x) for (int y = 0; y < 5; ++y)
          child["decks"][d]["floors"][std::to_string(x + dx) + "," + std::to_string(y + dy)] = true;
      child["decks"][1]["floors"].erase(std::to_string(2 + dx) + "," + std::to_string(1 + dy));
      for (const auto &m : {Json{{"id", 1}, {"type", "cockpit"}, {"x", dx}, {"y", dy}, {"r", 0}},
                           Json{{"id", 2}, {"type", "microReactor"}, {"x", 1 + dx}, {"y", dy}, {"r", 0}},
                           Json{{"id", 3}, {"type", "microLifeSupport"}, {"x", 2 + dx}, {"y", dy}, {"r", 0}},
                           Json{{"id", 4}, {"type", "airlock"}, {"x", dx}, {"y", 3 + dy}, {"r", 1}},
                           Json{{"id", 5}, {"type", "microEngine"}, {"x", 2 + dx}, {"y", 3 + dy}, {"r", 0}},
                           Json{{"id", 6}, {"type", "ladder"}, {"x", 1 + dx}, {"y", 2 + dy}, {"r", 0}}})
        child["decks"][0]["modules"].push_back(m);
      child["decks"][1]["modules"].push_back({{"id", 7}, {"type", "ladder"},
          {"x", 1 + dx}, {"y", 2 + dy}, {"r", 0}});
      child["nextId"] = 8;
      child["holds"] = Json::array({hold(0, 1, 1 + dx, 1 + dy, 2, 2)});
      return child;
    };
    const auto carrier_for = [&](const Json &child) {
      auto parent = station();
      const auto berth = add_module(parent, "shuttleDock", 12, 5);
      require(berth == 6, "Real carrier fixture keeps independently known module identity");
      fill(parent, 0, 12, 5, 4, 6);
      parent["decks"][0]["modules"].back()["craft"] = child;
      return parent;
    };
    const Json *limits = nullptr;
    for (const auto &part : active.data().at("parts"))
      if (part.at("id") == "shuttleDock") limits = &part.at("carrier");
    require(limits && limits->at("maxWidthM") == 6 && limits->at("maxLengthM") == 10 &&
                limits->at("maxDecks") == 5,
            "The real catalogue6x10 shuttle berth is the tested inclusive envelope");
    struct Extension { int dx, dy, x, y, w, h, width, length; };
    for (const auto edge : {Extension{0, 0, 2, 1, 2, 2, 8, 10},
                            Extension{1, 0, 0, 1, 2, 2, 8, 10},
                            Extension{0, 1, 1, 0, 2, 3, 6, 12},
                            Extension{0, 0, 0, 3, 2, 3, 6, 12}}) {
      auto child = child_at(edge.dx, edge.dy);
      accepted(child, active, schema);
      const auto original_child = import_definition(child.dump(), active, schema);
      auto parent = carrier_for(child);
      accepted(parent, active, schema);
      const auto before = import_definition(parent.dump(), active, schema);
      require(original_child.ok() && before.ok() &&
                  original_child.value->definition.at("derived").at("widthM") == 6 &&
                  original_child.value->definition.at("derived").at("lengthM") == 10,
              "Real3x5 material and partial second-deck opening fit6x10 before volume extension");
      child["holds"][0] = hold(0, 1, edge.x, edge.y, edge.w, edge.h);
      accepted(child, active, schema);
      parent["decks"][0]["modules"].back()["craft"] = child;
      const auto report = validate(parent.dump(), active, schema);
      const auto &item = finding(report, "carried_fit", "/blueprint/decks/0/modules/5/craft");
      require(report.at("pass") == false && item.at("details") ==
                  Json{{"deck", 0}, {"moduleId", 6}, {"widthM", edge.width},
                       {"lengthM", edge.length}, {"deckCount", 2}, {"limits", *limits},
                       {"rule", "/validation/checks/invalid_blueprint"}},
              "Each carried bbox arithmetic edge reports exact measured dimensions, active limits and nested source identity");
      require(!has_issue(report, "carried_not_ready") && !has_issue(report, "hold_access") &&
                  !has_issue(report, "hold_open_column"),
              "The actual child remains structurally sound; only its carried envelope is too large");
      const auto after = import_definition(parent.dump(), active, schema);
      const auto enlarged_child = import_definition(child.dump(), active, schema);
      require(after.ok() && enlarged_child.ok(), "Oversize carried drafts remain inspectable imports");
      auto old_child_totals = original_child.value->definition.at("derived");
      auto child_totals = enlarged_child.value->definition.at("derived");
      old_child_totals.erase("widthM"); old_child_totals.erase("lengthM");
      child_totals.erase("widthM"); child_totals.erase("lengthM");
      require(child_totals == old_child_totals,
              "All child material totals are unchanged by each empty hold extension");
      auto old_parent_totals = before.value->definition.at("derived");
      auto parent_totals = after.value->definition.at("derived");
      require(parent_totals.at("carriedCraft")[0].at("widthM") == edge.width &&
                  parent_totals.at("carriedCraft")[0].at("lengthM") == edge.length,
              "Independent normalized carried manifest agrees with structural envelope dimensions");
      for (auto *totals : {&old_parent_totals, &parent_totals})
        for (auto &craft : (*totals)["carriedCraft"]) {
          craft.erase("widthM"); craft.erase("lengthM");
        }
      require(parent_totals == old_parent_totals,
              "Parent material/cost/capacity totals remain identical; only child envelope metadata grows");
      DockOptions options; options.matchingAirlock = false;
      const auto docked = dock(parent.dump(), 0, 5, visitor().dump(), active, schema, options);
      const auto &nested = finding(docked, "carried_fit", "/host/blueprint/decks/0/modules/5/craft");
      require(nested.at("details") == item.at("details") &&
                  docked.at("data").at("checks").at("a").at("status") == "FAIL",
              "Real docking retains the exact carried-fit nested host path and enabled structural failure");
    }
  });
  suite.run("nested and wrapped hold errors preserve their public source paths", [&] {
    auto child = volume(); child["holds"][0]["lastDeck"] = 3;
    auto carrier = base("station");
    add_module(carrier, "shuttleDock", 12, 5);
    carrier["decks"][0]["modules"].back()["craft"] = child;
    const auto path = "/decks/0/modules/4/craft/holds/0";
    finding(validate(carrier.dump(), active, schema), "hold_span", path);
    const auto wrapped = Json{{"schema", "apsis-ship-definition-v4"}, {"blueprint", carrier},
      {"rulesVersion", 4}, {"rulesSha256", active.sha256()}, {"rules", active.data()},
      {"derived", Json::object()}, {"validation", nullptr}};
    const auto wrapped_report = validate(wrapped.dump(), active, schema);
    finding(wrapped_report, "hold_span", std::string("/blueprint") + path);
    const auto docked = dock(station().dump(), 0, 5, carrier.dump(), active, schema);
    finding(docked, "hold_span", std::string("/visitor") + path);
    finding(dock(carrier.dump(), 0, 5, visitor().dump(), active, schema), "hold_span", std::string("/host") + path);
  });
  return suite.finish();
}
