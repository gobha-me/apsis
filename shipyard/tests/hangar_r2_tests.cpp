#include "dock_test_support.hpp"

#include <cmath>
#include <string>

namespace {
using namespace dock_test;

Json canonical(std::string_view stem, const RulesSnapshot &active,
               const SchemaSet &schema) {
  const auto loaded = import_definition(
      read("fixtures/v3/apsis-" + std::string(stem) + "-definition-v3.json"),
      active, schema);
  require(loaded.ok(), "canonical hangar fixture must import");
  return loaded.value->definition;
}

RulesSnapshot with_hangar_limits(const RulesSnapshot &active,
                                 const SchemaSet &schema, int width,
                                 int length) {
  auto changed = active.data();
  bool found = false;
  for (auto &part : changed["parts"]) {
    if (part.at("id") != "stationHangar")
      continue;
    found = true;
    part["docking"]["maxWidthM"] = width;
    part["docking"]["maxLengthM"] = length;
  }
  require(found, "real rules must expose the station hangar");
  auto loaded = load_rules(changed.dump(), schema);
  require(loaded.ok(), "real rules with a changed hangar envelope must load");
  return std::move(*loaded.value);
}

Json thin_visitor() {
  auto blueprint = blank();
  // Five real 2 m cells make a 2 x 10 m envelope. The public docking
  // options below isolate the hangar measurement from crew/system checks.
  for (int y = 0; y < 5; ++y)
    blueprint["decks"][0]["floors"]["0," + std::to_string(y)] = true;
  return blueprint;
}

const Json &find_issue(const Json &report, std::string_view code) {
  for (const auto &item : report.at("issues"))
    if (item.at("code") == code)
      return item;
  throw std::runtime_error("Missing issue: " + std::string(code) + " in " +
                           report.dump());
}

void check_envelope(const Json &report, int visitor_width, int visitor_length,
                    int hangar_width, int hangar_length,
                    std::string_view severity = "error") {
  require(report.at("operation") == "dock" &&
              report.at("status") == "FAIL" && report.at("pass") == false,
          "exceeding both cardinal hangar envelopes must block docking");
  const auto &data = report.at("data");
  require(data.is_object() && data.at("target").at("kind") == "hangar" &&
              data.at("target").at("deck") == 1 &&
              data.at("target").at("moduleId") == 15,
          "the public result must retain the selected real hangar target");
  for (const auto *name : {"a", "b", "c", "d"}) {
    const auto &check = data.at("checks").at(name);
    require(check.at("enabled") == false && check.at("status") == "DISABLED",
            "the public options must isolate the hangar envelope check");
  }
  const auto &hangar = data.at("hangar");
  require(hangar.at("visitorWidthM") == visitor_width &&
              hangar.at("visitorLengthM") == visitor_length &&
              hangar.at("maxWidthM") == hangar_width &&
              hangar.at("maxLengthM") == hangar_length &&
              hangar.at("visitorDeckCount") == 1 &&
              hangar.at("deckCountFits") == true,
          "reported dimensions must come from the imported hull and active rules");
  require(!has_issue(report, "dock_hangar_deck_limit"),
          "the width/length case must stay within the independent deck cap");
  require(hangar.at("widthLengthFit") == false &&
              data.at("pending") == false && !has_issue(report, "pending_F19"),
          "Luna cardinal-only facings reject every failed orthogonal envelope");
  const auto &item = find_issue(report, "dock_hangar_size_limit");
  require(item.at("severity") == severity &&
              item.at("path") == "/visitor/blueprint/decks" &&
              !item.at("message").get<std::string>().empty(),
          "oversize retains its readable finding and configured severity");
}
} // namespace

int main() {
  using namespace dock_test;
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto anchorage = canonical("wayfarer-anchorage", active, schema);
  const auto skiff = canonical("skiff", active, schema);
  const auto wren = canonical("wren", active, schema);
  const auto thin = thin_visitor();
  DockOptions options;
  options.structural = false;
  options.portReachability = false;
  options.matchingAirlock = false;
  options.approachClearance = false;
  Suite suite;

  const auto check = [&](const Json &visitor, int visitor_width,
                         int visitor_length, int width, int length) {
    const auto configured = with_hangar_limits(active, schema, width, length);
    const auto report = dock(anchorage.dump(), 1, 15, visitor.dump(), configured,
                             schema, options);
    check_envelope(report, visitor_width, visitor_length, width, length);
  };

  const auto check_diagonal = [&](double length, double epsilon,
                                  bool definite) {
    auto changed = with_hangar_limits(active, schema, 6, 8).data();
    changed["numeric"]["epsilon"] = epsilon;
    for (auto &part : changed["parts"])
      if (part.at("id") == "stationHangar")
        part["docking"]["maxLengthM"] = length;
    const auto configured = load_rules(changed.dump(), schema);
    require(configured.ok(), "fractional hangar limits and epsilon must load");
    const double excess = 10.0 - std::hypot(6.0, length);
    require(excess >= 0.0 && (definite ? excess > epsilon : excess <= epsilon),
            "the diagonal fixture must straddle the active epsilon boundary");
    const auto report = dock(anchorage.dump(), 1, 15, thin.dump(),
                             *configured.value, schema, options);
    const auto &data = report.at("data");
    const auto &hangar = data.at("hangar");
    require(hangar.at("visitorWidthM") == 2 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 6 &&
                hangar.at("maxLengthM") == length &&
                hangar.at("deckCountFits") == true,
            "diagonal checks must use the real visitor and fractional rules");
    require(hangar.at("widthLengthFit") == false,
            "A 2 x 10 visitor exceeds both cardinal facings of 6 x under-10");
    require(has_issue(report, "dock_hangar_size_limit") &&
                !has_issue(report, "pending_F19") && data.at("pending") == false,
            "Diagonal equality and epsilon no longer change cardinal rejection");
    const auto &item = find_issue(report, "dock_hangar_size_limit");
    require(item.at("severity") == "error" &&
                item.at("path") == "/visitor/blueprint/decks",
            "Cardinal outcomes retain their structured public finding");
  };

  suite.run("D09 exact unrotated Skiff equality fits the 6 x 10 hangar", [&] {
    const auto configured = with_hangar_limits(active, schema, 6, 10);
    const auto report = dock(anchorage.dump(), 1, 15, skiff.dump(), configured,
                             schema, options);
    const auto &hangar = report.at("data").at("hangar");
    require(hangar.at("visitorWidthM") == 6 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 6 &&
                hangar.at("maxLengthM") == 10,
            "canonical Skiff must exactly equal both active hangar limits");
    require(hangar.at("widthLengthFit") == true &&
                !has_issue(report, "pending_F19") &&
                !has_issue(report, "dock_hangar_size_limit"),
            "exact unrotated equality must fit without pending F19 or a size error");
  });

  suite.run("Luna exact swapped equality is a permitted cardinal fit", [&] {
    const auto configured = with_hangar_limits(active, schema, 10, 6);
    const auto report = dock(anchorage.dump(), 1, 15, skiff.dump(), configured, schema, options);
    const auto &hangar = report.at("data").at("hangar");
    require(report.at("pass") == true && hangar.at("widthLengthFit") == true &&
                hangar.at("visitorWidthM") == 6 && hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 10 && hangar.at("maxLengthM") == 6 &&
                report.at("data").at("pending") == false && !has_issue(report, "pending_F19"),
            "Luna, under owner delegation 2026-11-23: inclusive swapped extent equality passes");
  });

  const double epsilon = active.data().at("numeric").at("epsilon").get<double>();
  suite.run("diagonal exact equality rejects under cardinal facings with active epsilon", [&] {
    check_diagonal(8.0, epsilon, false);
  });

  suite.run("diagonal excess within active epsilon rejects under cardinal facings", [&] {
    check_diagonal(7.9999999, epsilon, false);
  });

  suite.run("the same diagonal excess rejects beyond a smaller active epsilon",
            [&] { check_diagonal(7.9999999, 1e-8, true); });

  suite.run("E03 diagonal excess equal to epsilon rejects under cardinal facings", [&] {
    const double diagonal = std::hypot(6.0, 7.9999999);
    const double boundary_epsilon = 10.0 - diagonal;
    require(boundary_epsilon > 0.0 && diagonal + boundary_epsilon == 10.0,
            "the diagonal plus epsilon must exactly equal the visitor length");
    check_diagonal(7.9999999, boundary_epsilon, false);
  });

  suite.run("E04 diagonal excess between one and two epsilons rejects", [&] {
    const double boundary_epsilon = 5e-8;
    const double excess = 10.0 - std::hypot(6.0, 7.9999999);
    require(excess > boundary_epsilon && excess < 2.0 * boundary_epsilon,
            "the diagonal excess must distinguish one epsilon from two");
    check_diagonal(7.9999999, boundary_epsilon, true);
  });

  suite.run("E06 E07 Skiff width within epsilon still exceeds the exact limit",
            [&] {
    const double width = 5.9999995;
    require(6.0 > width && 6.0 - width <= epsilon,
            "the canonical Skiff must exceed the width by less than epsilon");
    auto changed = with_hangar_limits(active, schema, 6, 10).data();
    for (auto &part : changed["parts"])
      if (part.at("id") == "stationHangar")
        part["docking"]["maxWidthM"] = width;
    const auto configured = load_rules(changed.dump(), schema);
    require(configured.ok(), "the fractional hangar width must load");
    const auto report = dock(anchorage.dump(), 1, 15, skiff.dump(),
                             *configured.value, schema, options);
    const auto &data = report.at("data");
    const auto &hangar = data.at("hangar");
    require(report.at("operation") == "dock" &&
                report.at("status") == "FAIL" && report.at("pass") == false,
            "the exact minimum-side excess must block public docking");
    require(hangar.at("visitorWidthM") == 6 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == width &&
                hangar.at("maxLengthM") == 10 &&
                hangar.at("visitorDeckCount") == 1 &&
                hangar.at("deckCountFits") == true,
            "the exact-bound check must use canonical Skiff and fractional rules");
    require(hangar.at("widthLengthFit") == false &&
                data.at("pending") == false &&
                !has_issue(report, "pending_F19") &&
                !has_issue(report, "dock_hangar_deck_limit"),
            "epsilon must not allow an unrotated fit or defer minimum-side excess");
    const auto &item = find_issue(report, "dock_hangar_size_limit");
    require(item.at("severity") == "error" &&
                item.at("path") == "/visitor/blueprint/decks" &&
                !item.at("message").get<std::string>().empty(),
            "minimum-side excess must expose a readable structured size error");
  });

  suite.run("N14 Wren minimum side exceeds a long narrow hangar", [&] {
    // The historical swapped comparisons are false / true (34 <= 10,
    // 20 <= 40); the current unrotated comparisons are also false / true.
    // Either && -> || fault would incorrectly avoid this definite rejection.
    check(wren, 20, 34, 10, 40);
  });

  suite.run("canonical Wren exceeds the real 10 x 18 m hangar", [&] {
    check(wren, 20, 34, 10, 18);
  });

  suite.run("Skiff 6 x 10 in 8 x 8 exceeds both cardinal facings", [&] {
    check(skiff, 6, 10, 8, 8);
  });

  suite.run("minimum-side equality rejects Skiff cardinal fit", [&] {
    check(skiff, 6, 10, 6, 9);
  });

  suite.run("minimum-side excess rejects even below the hangar diagonal", [&] {
    check(skiff, 6, 10, 5, 9);
  });

  suite.run("2 x 10 in 9 x 9 only fits obliquely and rejects under cardinal facings", [&] {
    check(thin, 2, 10, 9, 9);
  });

  suite.run("warning severity still requires a cardinal fit for docking", [&] {
    const auto dock_with_warning = [&](int width, int length) {
      auto changed = with_hangar_limits(active, schema, width, length).data();
      changed["validation"]["checks"]["docking"]["severity"] = "warning";
      const auto configured = load_rules(changed.dump(), schema);
      require(configured.ok(), "warning docking severity must load");
      return dock(anchorage.dump(), 1, 15, thin.dump(), *configured.value,
                  schema, options);
    };

    const auto exact_fit = dock_with_warning(10, 2);
    const auto &data = exact_fit.at("data");
    const auto &hangar = data.at("hangar");
    require(hangar.at("visitorWidthM") == 2 &&
                hangar.at("visitorLengthM") == 10 &&
                hangar.at("maxWidthM") == 10 && hangar.at("maxLengthM") == 2,
            "the control must exactly equal the swapped cardinal limits");
    require(exact_fit.at("status") == "PASS" && exact_fit.at("pass") == true &&
                hangar.at("widthLengthFit") == true &&
                hangar.at("deckCountFits") == true &&
                data.at("pending") == false &&
                !has_issue(exact_fit, "dock_hangar_size_limit"),
            "an exact cardinal fit must still certify under warning severity");

    check_envelope(dock_with_warning(9, 9), 2, 10, 9, 9, "warning");
  });

  suite.run("hangar diagonal equality still rejects cardinal docking", [&] {
    // The diagonal is exactly 10 m, but both cardinal facings exceed a limit.
    check(thin, 2, 10, 6, 8);
  });

  suite.run("a visitor longer than the hangar diagonal is definitely too large",
            [&] { check(thin, 2, 10, 6, 7); });

  return suite.finish();
}
