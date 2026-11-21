#include "dock_test_support.hpp"

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
                    int hangar_width, int hangar_length, bool definite) {
  require(report.at("operation") == "dock" &&
              report.at("status") == "FAIL" && report.at("pass") == false,
          "a definite size failure or unresolved rotation must block docking");
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
  if (definite) {
    require(hangar.at("widthLengthFit") == false &&
                data.at("pending") == false &&
                !has_issue(report, "pending_F19"),
            "rotation-proof oversize must be definite without pending F19");
    const auto &item = find_issue(report, "dock_hangar_size_limit");
    require(item.at("severity") == "error" &&
                item.at("path") == "/visitor/blueprint/decks" &&
                !item.at("message").get<std::string>().empty(),
            "definite oversize must expose a readable structured size error");
  } else {
    require(hangar.at("widthLengthFit").is_null() &&
                data.at("pending") == true &&
                !has_issue(report, "dock_hangar_size_limit"),
            "an envelope without a rotation-proof rejection must stay unknown");
    const auto &item = find_issue(report, "pending_F19");
    require(item.at("severity") == "warning" &&
                item.at("path") == "/visitor/blueprint/decks" &&
                item.at("details").at("finding") == "F19" &&
                !item.at("message").get<std::string>().empty(),
            "unresolved rotation must retain the documented pending finding");
    require(std::none_of(report.at("issues").begin(),
                         report.at("issues").end(), [](const Json &item) {
                           return item.at("severity") == "error";
                         }),
            "the isolated pending case must not acquire a definite rejection");
  }
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
                         int visitor_length, int width, int length,
                         bool definite) {
    const auto configured = with_hangar_limits(active, schema, width, length);
    const auto report = dock(anchorage.dump(), 1, 15, visitor.dump(), configured,
                             schema, options);
    check_envelope(report, visitor_width, visitor_length, width, length,
                   definite);
  };

  suite.run("N14 Wren minimum side exceeds a long narrow hangar", [&] {
    // The historical swapped comparisons are false / true (34 <= 10,
    // 20 <= 40); the current unrotated comparisons are also false / true.
    // Either && -> || fault would incorrectly avoid this definite rejection.
    check(wren, 20, 34, 10, 40, true);
  });

  suite.run("canonical Wren exceeds the real 10 x 18 m hangar", [&] {
    check(wren, 20, 34, 10, 18, true);
  });

  suite.run("Skiff 6 x 10 in 8 x 8 has no rotation-proof rejection", [&] {
    check(skiff, 6, 10, 8, 8, false);
  });

  suite.run("minimum-side equality leaves Skiff rotation pending", [&] {
    check(skiff, 6, 10, 6, 9, false);
  });

  suite.run("minimum-side excess rejects even below the hangar diagonal", [&] {
    check(skiff, 6, 10, 5, 9, true);
  });

  suite.run("2 x 10 in 9 x 9 may fit diagonally and remains pending", [&] {
    check(thin, 2, 10, 9, 9, false);
  });

  suite.run("hangar diagonal equality cannot prove a size rejection", [&] {
    // hypot(6, 8) is exactly 10: the documented comparison is strict.
    check(thin, 2, 10, 6, 8, false);
  });

  suite.run("a visitor longer than the hangar diagonal is definitely too large",
            [&] { check(thin, 2, 10, 6, 7, true); });

  return suite.finish();
}
