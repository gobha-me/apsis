#include "dock_test_support.hpp"

#include <apsis/shipyard/perf.hpp>

#include <array>

namespace {
using namespace dock_test;

Json fixture(std::string_view name, const RulesSnapshot &active,
             const SchemaSet &schema) {
  const auto imported = import_definition(
      read("fixtures/v3/apsis-" + std::string(name) + "-definition-v3.json"),
      active, schema);
  require(imported.ok(),
          "Historical source must import through the public API");
  return imported.value->definition.at("blueprint");
}

RulesSnapshot hangar_rules(const RulesSnapshot &active, const SchemaSet &schema,
                           int width, int length, int decks = 2) {
  auto data = active.data();
  for (auto &part : data["parts"])
    if (part.at("id") == "stationHangar") {
      part["docking"]["maxWidthM"] = width;
      part["docking"]["maxLengthM"] = length;
      part["docking"]["maxDecks"] = decks;
    }
  auto loaded = load_rules(canonical_json(data), schema);
  require(loaded.ok(), "Authored hangar limits must load as real rules");
  return std::move(*loaded.value);
}

void only_alignment_pending(const Json &report) {
  const auto &checks = report.at("data").at("checks");
  for (const auto *check : {"a", "b", "d"})
    require(
        checks.at(check).at("status") == "PASS",
        "The control must reach docking with valid structure and clearance");
  require(checks.at("c").at("status") == "PENDING" &&
              has_issue(report, "pending_F21") && report.at("pass") == false,
          "An eligible size must not silently certify unresolved alignment");
}
} // namespace

int main() {
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto anchorage = fixture("wayfarer-anchorage", active, schema);
  const auto skiff = fixture("skiff", active, schema);
  const auto wren = fixture("wren", active, schema);
  Suite suite;

  suite.run(
      "F01 every owner-picked drive price reaches calculated direct costs",
      [&] {
        struct Price {
          const char *part;
          double rating;
          int cost;
        };
        constexpr std::array prices{
            Price{"drive05", 0.5, 1500}, Price{"drive10", 1.0, 2500},
            Price{"drive15", 1.5, 3750}, Price{"drive20", 2.0, 5000},
            Price{"drive30", 3.0, 7500}};
        const auto bare =
            perf(canonical_json(blank_blueprint(active)), active, schema);
        require(bare.at("data").is_object(),
                "Blank direct totals must remain inspectable");
        const auto bare_cost = bare.at("data")
                                   .at("definition")
                                   .at("derived")
                                   .at("cost")
                                   .get<double>();
        for (const auto &price : prices) {
          auto ship = blank_blueprint(active);
          add_module(ship, price.part, 3, 3);
          const auto report = perf(canonical_json(ship), active, schema);
          require(report.at("data").is_object(),
                  "Fitted-drive totals must calculate");
          const auto &data = report.at("data");
          const auto &drive =
              data.at("performance").at("jump").at("driveCosts");
          require(drive.size() == 1 && drive[0].at("rating") == price.rating &&
                      drive[0].at("cost") == price.cost &&
                      drive[0].at("part") == price.part,
                  "Each explicit owner price must be reported for the fitted "
                  "drive");
          require(data.at("definition").at("derived").at("cost").get<double>() -
                          bare_cost ==
                      price.cost,
                  "Reported drive prices must affect actual blueprint direct "
                  "totals");
        }
      });

  suite.run(
      "F02 ports have no size envelope for a real 20 by 34 metre ship", [&] {
        for (const auto &part : active.data().at("parts"))
          if (part.contains("docking") &&
              part.at("docking").at("kind") == "port")
            for (const auto *field : {"maxWidthM", "maxLengthM", "maxDecks"})
              require(!part.at("docking").contains(field),
                      "Ports must remove size caps rather than merely increase "
                      "them");
        const auto report = dock_at_port(station(), wren, active, schema);
        only_alignment_pending(report);
        require(
            !report.at("data").contains("hangar") &&
                std::none_of(
                    report.at("issues").begin(), report.at("issues").end(),
                    [](const Json &item) {
                      const auto code = item.at("code").get<std::string>();
                      return code.find("size_limit") != std::string::npos ||
                             code.find("deck_limit") != std::string::npos;
                    }),
            "A port must not apply a hangar envelope to the canonical Wren");
      });

  suite.run(
      "F02 hangar inclusive dimensions and rotation-proof overbounds",
      [&] {
        struct Envelope {
          int width;
          int length;
          bool fits;
        };
        // The 6 by 7 hangar has a diagonal shorter than the 10 metre visitor.
        // Narrowing only to 6 by 9 does not prove an oversize under open F19.
        for (const auto bounds :
             {Envelope{6, 10, true}, Envelope{6, 11, true}, Envelope{7, 10, true},
              Envelope{5, 10, false}, Envelope{6, 7, false}}) {
          const auto limited =
              hangar_rules(active, schema, bounds.width, bounds.length);
          const auto report = dock(canonical_json(anchorage), 1, 15,
                                   canonical_json(skiff), limited, schema);
          const auto &hangar = report.at("data").at("hangar");
          require(hangar.at("visitorWidthM") == 6 &&
                      hangar.at("visitorLengthM") == 10 &&
                      hangar.at("maxWidthM") == bounds.width &&
                      hangar.at("maxLengthM") == bounds.length &&
                      hangar.at("widthLengthFit") == bounds.fits,
                  "Inclusive unrotated fits and rotation-proof failures must "
                  "retain measured dimensions and active limits");
          require(
              has_issue(report, "dock_hangar_size_limit") == !bounds.fits &&
                  !has_issue(report, "pending_F19"),
              "Definite fits and oversize cases do not guess a rotation rule");
          if (bounds.fits)
            only_alignment_pending(report);
          else {
            require(report.at("status") == "FAIL" && report.at("pass") == false,
                    "A rotation-proof overbound must block docking");
            require(std::any_of(
                        report.at("issues").begin(), report.at("issues").end(),
                        [](const Json &item) {
                          return item.at("code") == "dock_hangar_size_limit" &&
                                 item.at("severity") == "error";
                        }),
                    "Definite oversize must retain the active docking error");
          }
        }
      });

  suite.run(
      "F19 failed axis alignments remain uncertified without a rotation-proof "
      "bound",
      [&] {
        // Keep the original 6 by 9 case; 6 by 8 also checks strictness at the
        // diagonal bound (hypot(6, 8) == 10). Neither chooses a rotation rule.
        for (const auto length : {9, 8}) {
          const auto limited = hangar_rules(active, schema, 6, length);
          for (const bool alignment : {true, false}) {
            DockOptions options;
            options.matchingAirlock = alignment;
            const auto report = dock(canonical_json(anchorage), 1, 15,
                                     canonical_json(skiff), limited, schema,
                                     options);
            const auto &data = report.at("data");
            const auto &hangar = data.at("hangar");
            require(hangar.at("visitorWidthM") == 6 &&
                        hangar.at("visitorLengthM") == 10 &&
                        hangar.at("maxWidthM") == 6 &&
                        hangar.at("maxLengthM") == length &&
                        hangar.at("widthLengthFit").is_null() &&
                        hangar.at("deckCountFits") == true,
                    "Two failed axis alignments must not certify or reject an "
                    "uncertain hangar fit");
            require(has_issue(report, "pending_F19") &&
                        !has_issue(report, "dock_hangar_size_limit") &&
                        report.at("status") == "FAIL" &&
                        report.at("pass") == false && data.at("pending") == true,
                    "Unresolved F19 must block certification even when F21 is "
                    "disabled");
            for (const auto &item : report.at("issues")) {
              require(item.at("severity") != "error",
                      "An uncertain fit must not acquire a guessed size error");
              if (item.at("code") == "pending_F19")
                require(item.at("severity") == "warning" &&
                            item.at("details").at("finding") == "F19",
                        "The unresolved rotation must have structured F19 "
                        "details");
            }
            for (const auto *check : {"a", "b", "d"})
              require(data.at("checks").at(check).at("status") == "PASS",
                      "Other docking checks must pass so F19 is the blocker");
            if (alignment)
              only_alignment_pending(report);
            else
              require(data.at("checks").at("c").at("enabled") == false &&
                          data.at("checks").at("c").at("status") == "DISABLED" &&
                          !has_issue(report, "pending_F21"),
                      "The public test seam must isolate F19 from alignment");
          }
        }
      });

  suite.run(
      "F02 hangar deck count endpoint and one-over use structurally valid "
      "craft",
      [&] {
        const auto limited = hangar_rules(active, schema, 6, 10);
        auto ship = skiff;
        const auto first = add_module(ship, "ladder", 13, 10);
        for (std::size_t count = 2; count <= 3; ++count) {
          const auto id = first + count - 1;
          ship["decks"].push_back({{"name", "Linked level"},
                                   {"floors", {{"13,10", true}}},
                                   {"modules", Json::array({{{"id", id},
                                                             {"type", "ladder"},
                                                             {"x", 13},
                                                             {"y", 10},
                                                             {"r", 0}}})}});
          ship["nextId"] = id + 1;
          require(
              validate(canonical_json(ship), limited, schema).at("pass") ==
                  true,
              "The deck-boundary craft must have genuine connected structure");
          const auto report = dock(canonical_json(anchorage), 1, 15,
                                   canonical_json(ship), limited, schema);
          const auto &hangar = report.at("data").at("hangar");
          require(hangar.at("maxDecks") == 2 &&
                      hangar.at("visitorDeckCount") == count &&
                      hangar.at("deckCountFits") == (count == 2) &&
                      has_issue(report, "dock_hangar_deck_limit") ==
                          (count == 3),
                  "Only the visitor exceeding the active hangar deck cap must "
                  "fail size");
          if (count == 2)
            only_alignment_pending(report);
        }
      });

  return suite.finish();
}
