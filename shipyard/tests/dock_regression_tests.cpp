#include "dock_test_support.hpp"

#include <string>

namespace {
using namespace dock_test;

Json canonical(std::string_view stem, const RulesSnapshot &active,
               const SchemaSet &schema) {
  const auto loaded = import_definition(
      read("fixtures/v3/apsis-" + std::string(stem) + "-definition-v3.json"),
      active, schema);
  require(loaded.ok(), "canonical docking fixture must import");
  return loaded.value->definition;
}

void rejected_early(const Json &report, std::string_view code,
                    std::string_view path) {
  require(report.at("status") == "FAIL" && report.at("pass") == false,
          "invalid docking request must fail the public result envelope");
  require(report.at("data").is_null(),
          "invalid docking request must return before constructing check data");
  require(has_issue(report, code, path),
          "invalid docking request must identify its specific public fault");
}

RulesSnapshot with_hangar_limits(const RulesSnapshot &active,
                                 const SchemaSet &schema, int width,
                                 int length) {
  auto changed = active.data();
  for (auto &part : changed["parts"]) {
    if (part.at("id") != "stationHangar")
      continue;
    part["docking"]["maxWidthM"] = width;
    part["docking"]["maxLengthM"] = length;
  }
  auto loaded = load_rules(changed.dump(), schema);
  require(loaded.ok(), "rule-defined hangar envelope must load");
  return std::move(*loaded.value);
}
} // namespace

int main() {
  using namespace dock_test;
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto anchorage = canonical("wayfarer-anchorage", active, schema);
  const auto skiff = canonical("skiff", active, schema);
  const auto wren = canonical("wren", active, schema);
  Suite suite;

  suite.run("K05 matching airlock must face exterior rather than floor", [&] {
    const auto baseline = dock_at_port(station(), visitor(), active, schema);
    require(baseline.at("data").at("checks").at("c").at("status") ==
                "PENDING" &&
                baseline.at("data")
                        .at("checks")
                        .at("c")
                        .at("details")
                        .at("exteriorOutwardCount") == 1,
            "baseline visitor must have one compatible exterior airlock");
    auto inward = visitor();
    inward["decks"][0]["floors"]["1,5"] = true;
    const auto report = dock_at_port(station(), inward, active, schema);
    const auto &check = report.at("data").at("checks").at("c");
    require(check.at("status") == "FAIL" &&
                check.at("details").at("airlockCount") == 1 &&
                check.at("details").at("compatibleInterfaceCount") == 1 &&
                check.at("details").at("exteriorOutwardCount") == 0,
            "floor in front must disqualify a matching visitor airlock");
    require(has_issue(report, "dock_no_matching_airlock",
                      "/visitor/blueprint/decks") &&
                !has_issue(report, "pending_F21"),
            "an inward-facing airlock must fail instead of becoming a candidate");
  });

  suite.run("K08 a non-station host returns null data before docking", [&] {
    auto host = station();
    host["buildType"] = "ship";
    rejected_early(dock_at_port(host, visitor(), active, schema),
                   "invalid_docking_host", "/host/blueprint/buildType");
  });

  suite.run("K09 a non-ship visitor returns null data before docking", [&] {
    auto ship = visitor();
    ship["buildType"] = "station";
    rejected_early(dock_at_port(station(), ship, active, schema),
                   "invalid_docking_visitor", "/visitor/blueprint/buildType");
  });

  suite.run("K10 a real non-docking module is rejected without an exception",
            [&] {
              // The reactor exists, but has no docking record. Do not catch an
              // exception here: an escaping Json::out_of_range fails this case.
              const auto report = dock(station().dump(), 0, 2, visitor().dump(),
                                       active, schema);
              rejected_early(report, "invalid_docking_target", "/target");
            });

  suite.run("K18 a station airlock is excluded by active targetKinds", [&] {
    require(active.data().at("docking").at("targetKinds") ==
                Json::array({"port", "hangar"}),
            "real rules admit ports and hangars as station targets");
    // Unlike K10, this real target has a docking record with kind airlock.
    rejected_early(dock(station().dump(), 0, 4, visitor().dump(), active, schema),
                   "invalid_docking_target",
                   "/host/blueprint/decks/0/modules/3");
  });

  suite.run("K12 exactly two visitor decks fit the hangar deck cap", [&] {
    auto ship = skiff.at("blueprint");
    const auto ladder_id = add_module(ship, "ladder", 13, 10);
    ship["decks"].push_back(
        {{"name", "Linked upper deck"},
         {"floors", {{"13,10", true}}},
         {"modules", Json::array({{{"id", ladder_id + 1},
                                   {"type", "ladder"},
                                   {"x", 13},
                                   {"y", 10},
                                   {"r", 0}}})}});
    ship["nextId"] = ladder_id + 2;
    require(validate(ship.dump(), active, schema).at("pass") == true,
            "the boundary visitor must have valid paired ladders and structure");
    const auto report =
        dock(anchorage.dump(), 1, 15, ship.dump(), active, schema);
    const auto &hangar = report.at("data").at("hangar");
    require(hangar.at("maxDecks") == 2 &&
                hangar.at("visitorDeckCount") == 2 &&
                hangar.at("deckCountFits") == true &&
                !has_issue(report, "dock_hangar_deck_limit"),
            "the documented maximum deck count is inclusive");
    require(report.at("data").at("checks").at("a").at("status") ==
                "PASS",
            "the deck endpoint must not be masked by a structural fault");
  });

  suite.run("K14 failed docking checks block even at warning severity", [&] {
    auto changed = active.data();
    changed["validation"]["checks"]["docking"]["severity"] = "warning";
    auto loaded = load_rules(changed.dump(), schema);
    require(loaded.ok(), "warning docking severity must load as real rules");
    auto ship = visitor();
    ship["decks"][0]["modules"][3]["interface"] = "cargo";
    const auto report =
        dock_at_port(station(), ship, *loaded.value, schema);
    require(has_issue(report, "dock_no_matching_airlock"),
            "the compatible-interface check must detect the authored fault");
    require(std::all_of(report.at("issues").begin(), report.at("issues").end(),
                        [](const Json &item) {
                          return item.at("severity") != "error";
                        }) &&
                report.at("data").at("pending") == false,
            "only failed-check state may block this warning-only request");
    const auto &checks = report.at("data").at("checks");
    require(checks.at("c").at("status") == "FAIL" &&
                checks.at("a").at("status") == "PASS" &&
                checks.at("b").at("status") == "PASS" &&
                checks.at("d").at("status") == "PASS",
            "only the actual interface check must fail");
    require(report.at("status") == "FAIL" && report.at("pass") == false,
            "warning-only failed check must not produce a docking PASS");
  });

  suite.run("S2 unrotated Skiff fit is definite without pending_F19", [&] {
    const auto report =
        dock(anchorage.dump(), 1, 15, skiff.dump(), active, schema);
    const auto &hangar = report.at("data").at("hangar");
    require(hangar.at("visitorWidthM") == 6 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 10 &&
                hangar.at("maxLengthM") == 18,
            "canonical Skiff must fit the installed envelope without rotation");
    require(hangar.at("widthLengthFit") == true &&
                !has_issue(report, "pending_F19") &&
                has_issue(report, "pending_F21"),
            "definite unrotated fit must leave only alignment uncertified");
  });

  suite.run("S2 only a fit requiring rotation remains pending_F19", [&] {
    const auto narrow = with_hangar_limits(active, schema, 12, 8);
    const auto report =
        dock(anchorage.dump(), 1, 15, skiff.dump(), narrow, schema);
    const auto &hangar = report.at("data").at("hangar");
    require(hangar.at("visitorWidthM") == 6 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 12 &&
                hangar.at("maxLengthM") == 8,
            "Skiff must fit only after exchanging width and length");
    require(hangar.at("widthLengthFit").is_null() &&
                has_issue(report, "pending_F19") &&
                report.at("data").at("pending") == true &&
                report.at("pass") == false,
            "a rotation-only fit must remain uncertified until F19 is settled");
  });

  suite.run("S2 Wren exceeds both hangar orientations with a size error", [&] {
    const auto report =
        dock(anchorage.dump(), 1, 15, wren.dump(), active, schema);
    const auto &hangar = report.at("data").at("hangar");
    require(hangar.at("visitorWidthM") == 20 &&
                hangar.at("visitorLengthM") == 34 &&
                hangar.at("maxWidthM") == 10 &&
                hangar.at("maxLengthM") == 18,
            "Wren must exceed the real hangar in both orientations");
    require(hangar.at("widthLengthFit") == false,
            "definite oversize must return false rather than an unknown fit");
    require(!has_issue(report, "pending_F19"),
            "definite oversize must not await an irrelevant rotation decision");
    require(std::any_of(report.at("issues").begin(), report.at("issues").end(),
                        [](const Json &item) {
                          const auto code = item.at("code").get<std::string>();
                          return item.at("severity") == "error" &&
                                 code.starts_with("dock_hangar_") &&
                                 code != "dock_hangar_deck_limit";
                        }),
            "definite hangar oversize must expose a structured size error");
  });
  return suite.finish();
}
