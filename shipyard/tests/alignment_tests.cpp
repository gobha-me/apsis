#include "dock_test_support.hpp"

#include <array>

namespace {
using namespace dock_test;

// Real blueprints and public options isolate (c); no simulated structure or
// docking response is substituted. Full structural controls are tested below.
Json host(int rotation = 0) {
  auto result = blank("station");
  add_module(result, "dockingPort", 10, 10, rotation);
  return result;
}

Json ship(int x, int y, int rotation = 0) {
  auto result = blank();
  add_module(result, "airlock", x, y, rotation);
  result["decks"][0]["floors"][std::to_string(x) + "," + std::to_string(y)] = true;
  return result;
}

DockOptions isolated() {
  DockOptions options;
  options.structural = false;
  options.portReachability = false;
  options.approachClearance = false;
  return options;
}

Json run(const Json &station, const Json &visitor, const RulesSnapshot &rules,
         const SchemaSet &schemas, std::size_t deck = 0) {
  return dock(station.dump(), deck, 1, visitor.dump(), rules, schemas, isolated());
}

Json port_policy_details() {
  return {{"targetKind", "port"},
          {"sameDeck", true},
          {"singleAxis", true},
          {"referencePoint", "centres"},
          {"distanceMetric", "manhattan"},
          {"maximumDistanceCells", 1}};
}

void outcome(const Json &report, bool fits, std::size_t airlocks = 1,
             std::size_t interfaces = 1, std::size_t exterior = 1,
             std::size_t same_deck = 1, std::size_t aligned = 1) {
  require(report.at("operation") == "dock" && report.at("data").is_object(),
          "Public dock keeps inspectable data for a decided alignment");
  const auto &data = report.at("data");
  const auto &check = data.at("checks").at("c");
  require(check.at("enabled") == true &&
              check.at("status") == (fits ? "PASS" : "FAIL") &&
              report.at("pass") == fits &&
              report.at("status") == (fits ? "PASS" : "FAIL") &&
              data.at("pending") == false && !has_issue(report, "pending_F21"),
          "Luna, under owner delegation 2026-11-23, decides F03/F21");
  auto expected = port_policy_details();
  expected.update({{"airlockCount", airlocks},
                   {"compatibleInterfaceCount", interfaces},
                   {"exteriorOutwardCount", exterior},
                   {"sameDeckExteriorCount", same_deck},
                   {"alignedAirlockCount", aligned}});
  require(check.at("details") == expected,
          "All candidates and the exact active centre convention are observable");
  for (const auto *other : {"a", "b", "d"})
    require(data.at("checks").at(other).at("status") == "DISABLED",
            "Public switches isolate alignment without suppressing it");
  require(!data.contains("hangar"), "A port has no visitor size envelope");
  require(has_issue(report, "dock_no_matching_airlock") == !fits,
          "Only a resolved failure produces the matching-airlock diagnostic");
  if (!fits) {
    const auto found = std::find_if(report.at("issues").begin(),
                                  report.at("issues").end(), [](const Json &i) {
      return i.at("code") == "dock_no_matching_airlock";
    });
    auto details = port_policy_details();
    details.update({{"check", "c"}, {"interface", "standard"}});
    require(found->at("severity") == "error" &&
                found->at("path") == "/visitor/blueprint/decks" &&
                !found->at("message").get<std::string>().empty() &&
                found->at("details") == details,
            "Resolved errors retain exact path, severity and measurement policy");
  }
}

RulesSnapshot changed_rules(Json data, const SchemaSet &schema) {
  auto loaded = load_rules(data.dump(), schema);
  require(loaded.ok(), "Coherent real modified rules load through public API");
  return std::move(*loaded.value);
}

void dimensions(Json &data, int port_w, int port_h, int air_w, int air_h) {
  for (auto &part : data["parts"]) {
    if (part.at("id") == "dockingPort") {
      part["w"] = port_w;
      part["h"] = port_h;
    }
    if (part.at("id") == "airlock") {
      part["w"] = air_w;
      part["h"] = air_h;
    }
  }
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto orthogonal = orthogonal_rules(active, schema);
  Suite suite;

  suite.run("stock port rejects half-cell displacement along both axes", [&] {
    // The port at (10,10) is centred at (11,11). Every visitor centred
    // 0.5 cells away in each direction fails even though the distances sum to 1.
    for (int rotation = 0; rotation < 4; ++rotation) {
      const auto station = host(rotation);
      for (int x : {10, 11})
        for (int y : {10, 11})
          outcome(run(station, ship(x, y, rotation), active, schema),
                  false, 1, 1, 1, 1, 0);
    }
  });
  suite.run("one cell is inclusive only along a shared row or column", [&] {
    auto data = active.data();
    dimensions(data, 2, 2, 2, 2);
    const auto configured = changed_rules(data, schema);
    // Equal footprints make origin offsets exact centre offsets.
    struct Placement {
      int dx;
      int dy;
      bool fits;
    };
    constexpr std::array<Placement, 13> placements{{
        {0, 0, true}, {-1, 0, true}, {1, 0, true}, {0, -1, true}, {0, 1, true},
        {-2, 0, false}, {2, 0, false}, {0, -2, false}, {0, 2, false},
        {-1, -1, false}, {-1, 1, false}, {1, -1, false}, {1, 1, false}}};
    const auto station = host();
    for (const auto &[dx, dy, fits] : placements)
      outcome(run(station, ship(10 + dx, 10 + dy), configured, schema),
              fits, 1, 1, 1, 1, fits ? 1 : 0);
  });
  suite.run("same deck uses array index rather than label or candidate centre", [&] {
    auto station = host();
    auto visitor = ship(10, 10);
    const auto empty = blank().at("decks")[0];
    station["decks"].insert(station["decks"].begin(), empty);
    outcome(run(station, visitor, orthogonal, schema, 1), false, 1, 1, 1, 0, 0);
    visitor["decks"].insert(visitor["decks"].begin(), empty);
    visitor["decks"][1]["name"] = "A different deck label";
    outcome(run(station, visitor, orthogonal, schema, 1), true);
    // Target0/visitor1 exercises the opposite direction of the deck comparison.
    outcome(run(host(), visitor, orthogonal, schema), false, 1, 1, 1, 0, 0);
  });
  suite.run("all compatible candidates count and any aligned later candidate wins", [&] {
    auto visitor = aligned_visitor();
    add_module(visitor, "airlock", 6, 14, 1); // Exterior, three cells away.
    add_module(visitor, "airlock", 6, 11, 1); // Coincident centres.
    auto &modules = visitor["decks"][0]["modules"];
    std::rotate(modules.begin(), modules.end() - 2, modules.end());
    require(validate(visitor.dump(), orthogonal, schema).at("pass") == true,
            "Every candidate belongs to a genuinely valid real visitor");
    const auto station = orthogonal_station();
    const auto report = dock_at_port(station, visitor, orthogonal, schema);
    auto details = port_policy_details();
    details.update({{"airlockCount", 3}, {"compatibleInterfaceCount", 3},
                    {"exteriorOutwardCount", 3}, {"sameDeckExteriorCount", 3},
                    {"alignedAirlockCount", 2}});
    require(report.at("pass") == true &&
                report.at("data").at("checks").at("c").at("details") ==
                    details,
            "A distant first airlock cannot conceal either later aligned one");
    std::reverse(modules.begin(), modules.end());
    require(dock_at_port(station, visitor, orthogonal, schema) == report,
            "Permutation cannot expose a first-airlock picker");
    require(report.at("data").size() == 3,
            "No invented chosen candidate or docking transform is returned");
  });
  suite.run("interface exterior and absence filters remain independent", [&] {
    auto visitor = ship(10, 10);
    visitor["decks"][0]["modules"][0]["interface"] = "cargo";
    outcome(run(host(), visitor, active, schema), false, 1, 0, 0, 0, 0);
    visitor = ship(10, 10);
    visitor["decks"][0]["floors"]["10,11"] = true;
    outcome(run(host(), visitor, active, schema), false, 1, 1, 0, 0, 0);
    outcome(run(host(), blank(), active, schema), false, 0, 0, 0, 0, 0);
  });
  suite.run("disabled c suppresses only its own decision and finding", [&] {
    auto options = isolated();
    options.matchingAirlock = false;
    const auto report = dock(host().dump(), 0, 1, ship(0, 0).dump(), active,
                             schema, options);
    require(report.at("pass") == true &&
                report.at("data").at("checks").at("c").at("status") == "DISABLED" &&
                !has_issue(report, "dock_no_matching_airlock") &&
                !has_issue(report, "pending_F21"),
            "The public independent option retains its exact disabled contract");
  });
  suite.run("shipped v4 rules declare the exact port-only alignment policy", [&] {
    const Json expected = {{"targetKind", "port"},
                           {"sameDeck", true},
                           {"singleAxis", true},
                           {"referencePoint", "centres"},
                           {"distanceMetric", "manhattan"},
                           {"maxDistanceCells", 1}};
    require(active.data().at("docking").at("alignment") == expected,
            "The loaded port policy requires a shared deck and one axis");
  });
  suite.run("omitting the whole port policy preserves geometry and diagnostics", [&] {
    auto data = orthogonal.data();
    data["docking"].erase("alignment");
    const auto legacy = changed_rules(data, schema);
    const auto station = host();
    outcome(run(station, ship(10, 10), legacy, schema), true);
    outcome(run(station, ship(11, 10), legacy, schema), true);
    outcome(run(station, ship(12, 10), legacy, schema), false, 1, 1, 1, 1, 0);
    outcome(run(station, ship(11, 11), legacy, schema), false, 1, 1, 1, 1, 0);
    dimensions(data, 2, 2, 1, 1);
    const auto legacy_stock = changed_rules(data, schema);
    outcome(run(station, ship(10, 10), legacy_stock, schema),
            false, 1, 1, 1, 1, 0);
  });
  suite.run("schema rejects exceptions to the approved port policy", [&] {
    const std::array<std::pair<const char *, Json>, 10> invalid{{
        {"targetKind", "hangar"}, {"sameDeck", false}, {"singleAxis", false},
        {"referencePoint", "origins"}, {"distanceMetric", "euclidean"},
        {"maxDistanceCells", -1}, {"maxDistanceCells", 0},
        {"maxDistanceCells", 0.5}, {"maxDistanceCells", 2},
        {"maxDistanceCells", 1e100}}};
    for (const auto &[key, value] : invalid) {
      auto data = active.data();
      data["docking"]["alignment"][key] = value;
      const auto loaded = load_rules(data.dump(), schema);
      require(!loaded.ok() &&
                  issue(loaded.issues, "schema_error", "/docking/alignment/" + std::string(key)),
              "Rejected port policy value for " + std::string(key) + ": " + value.dump());
    }
    for (const auto *key : {"targetKind", "sameDeck", "singleAxis", "referencePoint",
                            "distanceMetric", "maxDistanceCells"}) {
      auto data = active.data();
      data["docking"]["alignment"].erase(key);
      const auto loaded = load_rules(data.dump(), schema);
      require(!loaded.ok() &&
                  issue(loaded.issues, "schema_error", "/docking/alignment/" + std::string(key)),
              "A present port policy must specify " + std::string(key));
    }
  });
  suite.run("nonsquare cardinal footprints use active directions not rotation index", [&] {
    auto data = active.data();
    dimensions(data, 2, 4, 2, 3);
    for (bool reordered : {false, true}) {
      if (reordered) {
        const auto before = data.at("rotations");
        data["rotations"] = Json::array({before[0], before[2], before[1], before[3]});
      }
      const auto configured = changed_rules(data, schema);
      for (int target = 0; target < 4; ++target)
        for (int air = 0; air < 4; ++air) {
          // Vertical centres (11,12)/(11,11.5), horizontal centres
          // (12,11)/(11.5,11). Matching directions are half a cell apart
          // on one axis. Differing directions are offset on both axes.
          const bool fits = reordered ? (target < 2) == (air < 2)
                                       : (target % 2) == (air % 2);
          outcome(run(host(target), ship(10, 10, air), configured, schema),
                  fits, 1, 1, 1, 1, fits ? 1 : 0);
        }
    }
  });
  suite.run("full real control certifies while old distant geometry fails c", [&] {
    require(validate(aligned_visitor().dump(), orthogonal, schema).at("pass") == true,
            "Translated real visitor independently passes structural validation");
    const auto station = orthogonal_station();
    const auto positive = dock_at_port(station, aligned_visitor(), orthogonal, schema);
    require(positive.at("pass") == true && positive.at("data").at("pending") == false,
            "All default F18 checks certify the real aligned fixture");
    const auto negative = dock_at_port(station, visitor(), orthogonal, schema);
    require(negative.at("pass") == false &&
                negative.at("data").at("checks").at("c").at("status") == "FAIL" &&
                negative.at("data").at("checks").at("a").at("status") == "PASS" &&
                negative.at("data").at("checks").at("b").at("status") == "PASS" &&
                negative.at("data").at("checks").at("d").at("status") == "PASS" &&
                has_issue(negative, "dock_no_matching_airlock") &&
                !has_issue(negative, "pending_F21"),
            "The original ten-cell separation is a precise alignment negative");
  });
  suite.run("aligned 52 metre visitor still has no port size envelope", [&] {
    auto visitor = aligned_visitor();
    for (int x = 0; x < 26; ++x)
      visitor["decks"][0]["floors"][std::to_string(x) + ",15"] = true;
    const auto checked = validate(visitor.dump(), orthogonal, schema);
    require(checked.at("pass") == true &&
                checked.at("data").at("definition").at("derived").at("widthM") == 52,
            "The large visitor has real connected material and valid structure");
    const auto report = dock_at_port(orthogonal_station(), visitor, orthogonal, schema);
    require(report.at("pass") == true && !report.at("data").contains("hangar") &&
                report.at("data").at("checks").at("c").at("status") == "PASS",
            "Large aligned ships certify at a port without an invented size cap");
  });
  suite.run("sparse grid preserves the exact one-cell port boundary", [&] {
    constexpr std::uint64_t base = 1ULL << 52;
    auto data = active.data();
    data["grid"] = {{"w", base + 8}, {"h", 1}};
    for (auto &part : data["parts"])
      part["h"] = 1;
    const auto place = [](std::string_view build_type, std::string_view part,
                          std::uint64_t x, unsigned width) {
      auto blueprint = blank(build_type);
      add_module(blueprint, part, 0, 0);
      auto &deck = blueprint["decks"][0];
      deck["modules"][0]["x"] = x;
      for (unsigned column = 0; column < width; ++column)
        deck["floors"][std::to_string(x + column) + ",0"] = true;
      return blueprint;
    };
    const auto station = place("station", "dockingPort", base + 1, 1);

    // The port centre is (base + 1.5, 0.5). A 1x1 airlock at (base, 0)
    // is exactly one cell away; rounding both centres to doubles gives two.
    dimensions(data, 1, 1, 1, 1);
    const auto single = changed_rules(data, schema);
    outcome(run(station, place("ship", "airlock", base, 1), single, schema),
            true);

    // A 2x1 airlock centred at base + 2 is half a cell away. Moving it
    // one cell right makes the gap 1.5, which would round down to one.
    dimensions(data, 1, 1, 2, 1);
    const auto wide = changed_rules(data, schema);
    outcome(run(station, place("ship", "airlock", base + 1, 2), wide, schema),
            true);
    outcome(run(station, place("ship", "airlock", base + 2, 2), wide, schema),
            false, 1, 1, 1, 1, 0);
  });
  return suite.finish();
}
