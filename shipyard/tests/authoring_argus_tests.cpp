#include "test_support.hpp"

#include <apsis/shipyard/authoring.hpp>

namespace {
using namespace test;

Json output(const Result<Json> &result) {
  return {{"ok", result.ok()},
          {"value", result.value ? *result.value : Json(nullptr)},
          {"issues", result.issues}};
}

Json rules_output(const Json &data, const SchemaSet &schema) {
  const auto bytes = canonical_json(data);
  const auto result = load_rules(bytes, schema);
  Json response = {{"input", bytes},
                   {"ok", result.ok()},
                   {"issues", result.issues},
                   {"snapshot", nullptr}};
  if (result.value) {
    const auto &active = *result.value;
    const auto blank = blank_blueprint(active);
    response["snapshot"] = {{"data", active.data()},
                            {"sha256", active.sha256()},
                            {"version", active.version()}};
    response["blank"] = blank;
    response["insert"] =
        output(insert_deck(canonical_json(blank), 1, active, schema));
    response["renameGenerated"] = output(
        rename_deck(canonical_json(blank), 0, std::nullopt, active, schema));
    auto legacy = test::blank();
    legacy["decks"][0]["name"] = data.at("defaults").at("deckName");
    const auto imported =
        import_definition(canonical_json(legacy), active, schema);
    response["legacyImport"] = {{"definition", imported.value
                                                   ? imported.value->definition
                                                   : Json(nullptr)},
                                {"issues", imported.issues}};
  }
  return response;
}

// Observation collection has no expected-result checks: --snapshot emits every
// public value and diagnostic even when a source mutation changes acceptance.
Json observations(const RulesSnapshot &active, const SchemaSet &schema) {
  Json all = Json::object();
  const auto one = blank_blueprint(active);
  const auto bytes = canonical_json(one);
  auto five = one;
  for (int level = 2; level <= 5; ++level) {
    auto deck = one.at("decks")[0];
    deck["name"] = "Main deck " + std::to_string(level);
    five["decks"].push_back(std::move(deck));
  }
  all["insertAtMaximum"] =
      output(insert_deck(canonical_json(five), 5, active, schema));
  all["removeAtMinimum"] = output(remove_deck(bytes, 0, active, schema));
  all["insertIndex"] = output(insert_deck(bytes, 2, active, schema));
  all["moveFromIndex"] = output(move_deck(bytes, 1, 0, active, schema));
  all["moveToIndex"] = output(move_deck(bytes, 0, 1, active, schema));
  all["removeIndex"] = output(remove_deck(bytes, 1, active, schema));
  all["renameIndex"] = output(rename_deck(bytes, 1, "Bridge", active, schema));
  for (const auto &[key, name] :
       std::vector<std::pair<std::string, std::string>>{
           {"DEL", std::string("A") + char(127) + "B"}, {"tab", "A\tB"}}) {
    all["rename" + key] = output(rename_deck(bytes, 0, name, active, schema));
    all["insert" + key] = output(insert_deck(bytes, 1, active, schema, name));
  }
  auto data = active.data();
  data["defaults"]["deckName"] = std::string(47, 'D');
  all["legacy47"] = rules_output(data, schema);
  data["defaults"]["deckName"] = std::string(46, 'D');
  all["legacy46"] = rules_output(data, schema);
  data = active.data();
  std::string stem;
  for (int count = 0; count < 46; ++count)
    stem += "\xc3\xa9";
  data["defaults"]["deckName"] = stem + "\xe4\xb8\xad";
  all["unicode47"] = rules_output(data, schema);
  return all;
}

void exact_error(const Json &result, std::string_view code,
                 std::string_view path, const Json &details,
                 std::string_view message) {
  require(result.at("ok") == false && result.at("value").is_null(),
          "Rejected operation must have no partial public value: " +
              result.dump());
  const Json expected = Json::array({{{"code", code},
                                      {"severity", "error"},
                                      {"path", path},
                                      {"details", details},
                                      {"message", message}}});
  require(result.at("issues") == expected,
          "Exact public diagnostic mismatch: " + result.at("issues").dump());
}
} // namespace

int main(int argc, char **argv) {
  using namespace test;
  const auto schema = schemas();
  const auto active = rules(schema);
  const auto all = observations(active, schema);
  if (argc == 2 && std::string_view(argv[1]) == "--snapshot") {
    std::cout << canonical_json(all) << '\n';
    return 0;
  }
  Suite suite;
  suite.run("Argus authoring count boundaries preserve exact paths and details",
            [&] {
              exact_error(all.at("insertAtMaximum"), "deck_limit",
                          "/blueprint/decks", {{"limit", 5}, {"requested", 6}},
                          "Deck count is outside the active rules limits.");
              exact_error(all.at("removeAtMinimum"), "deck_limit",
                          "/blueprint/decks", {{"limit", 1}, {"requested", 0}},
                          "Deck count is outside the active rules limits.");
            });
  suite.run(
      "Argus invalid indices retain deckCount for every public edit", [&] {
        for (const auto *key : {"insertIndex", "moveFromIndex", "moveToIndex",
                                "removeIndex", "renameIndex"})
          exact_error(
              all.at(key), "invalid_deck_index", "/blueprint/decks",
              {{"index", std::string_view(key) == "insertIndex" ? 2 : 1},
               {"deckCount", 1}},
              "Deck index is outside the authoring operation's bounds.");
      });
  suite.run("Argus DEL and tab fail at the authoring name path", [&] {
    for (const auto *key : {"renameDEL", "renametab", "insertDEL", "inserttab"})
      exact_error(all.at(key), "invalid_text",
                  std::string_view(key).starts_with("insert")
                      ? "/blueprint/decks/1/name"
                      : "/blueprint/decks/0/name",
                  Json::object(),
                  "Control characters are not allowed in labels or notes.");
  });
  suite.run(
      "Argus legacy v4 defaults load and import at the name boundary", [&] {
        for (const auto count : {46, 47}) {
          const auto &accepted = all.at("legacy" + std::to_string(count));
          const std::string original(count, 'D');
          require(accepted.at("ok") == true && accepted.at("issues").empty() &&
                      accepted.at("snapshot")
                              .at("data")
                              .at("defaults")
                              .at("deckName") == original,
                  "Legacy v4 rules must retain their exact default label");
          const auto &imported = accepted.at("legacyImport").at("definition");
          const auto &deck = imported.at("blueprint").at("decks").at(0);
          require(deck.at("name") == original &&
                      !deck.contains("nameGenerated"),
                  "Ordinary import must retain the unmarked legacy label");
          const std::string stem(46, 'D');
          require(
              accepted.at("blank").at("decks")[0].at("name") == stem + " 1" &&
                  accepted.at("insert").at("ok") == true &&
                  accepted.at("insert").at("value").at("decks")[1].at("name") ==
                      stem + " 2",
              "Generated labels keep a separator and the level within the cap");
        }
      });
  suite.run(
      "Argus generated labels shorten a Unicode stem at a scalar boundary",
      [&] {
        std::string stem;
        for (int count = 0; count < 46; ++count)
          stem += "\xc3\xa9";
        const auto &accepted = all.at("unicode47");
        require(accepted.at("ok") == true && accepted.at("issues").empty() &&
                    accepted.at("snapshot").at("version") == 4 &&
                    accepted.at("snapshot")
                            .at("data")
                            .at("defaults")
                            .at("deckName") == stem + "\xe4\xb8\xad",
                "Rules loading preserves the 47-character default stem");
        require(accepted.at("blank").at("decks")[0].at("name") == stem + " 1" &&
                    accepted.at("blank").at("decks")[0].at("nameGenerated") ==
                        true &&
                    accepted.at("insert").at("ok") == true &&
                    accepted.at("insert").at("value").at("decks")[1].at(
                        "name") == stem + " 2" &&
                    accepted.at("renameGenerated").at("ok") == true &&
                    accepted.at("renameGenerated")
                            .at("value")
                            .at("decks")[0]
                            .at("name") == stem + " 1",
                "Accepted Unicode rules must remain usable through public "
                "authoring");
      });
  return suite.finish();
}
