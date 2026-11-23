#include "dock_test_support.hpp"

#include <apsis/shipyard/render.hpp>
#include <array>
#include <cmath>
#include <functional>

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
  suite.run("partial mezzanines need a single common full-height open column", [&] {
    accepted(volume(), active, schema);
    auto blocked = volume();
    blocked["decks"][1]["floors"]["9,6"] = true;
    const auto report = validate(blocked.dump(), active, schema);
    const auto &item = finding(report, "hold_open_column", "/blueprint/holds/0");
    require(item.at("details").at("firstDeck") == 0 && item.at("details").at("lastDeck") == 2,
            "A completely covered intermediate plane blocks the whole declared span");
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
  suite.run("hold access accepts real adjacent catwalks but excludes distant or diagonal chains", [&] {
    accepted(volume(), active, schema);
    accepted(volume(3, 8, 5), active, schema);
    for (const auto &position : {std::pair{6, 6}, std::pair{7, 4}})
      finding(validate(volume(3, position.first, position.second).dump(), active, schema),
              "hold_access", "/blueprint/holds/0");
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
