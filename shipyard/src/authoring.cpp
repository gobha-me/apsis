#include <apsis/shipyard/authoring.hpp>
#include "internal.hpp"

#include <algorithm>
#include <utility>

namespace apsis::shipyard {
namespace {
std::string generated_name(const RulesSnapshot& rules, std::size_t index) {
    const auto& defaults = rules.data().at("defaults");
    const auto prefix = defaults.contains("deckNamePrefix")
        ? defaults.at("deckNamePrefix").get<std::string>()
        : defaults.at("deckName").get<std::string>() + " ";
    return prefix + std::to_string(index + 1);
}

Json empty_deck(const RulesSnapshot& rules, std::size_t index,
                const std::optional<std::string>& name = std::nullopt) {
    Json deck = {{"name", name ? *name : generated_name(rules, index)},
                 {"floors", Json::object()}, {"modules", Json::array()}};
    if (!name) deck["nameGenerated"] = true;
    return deck;
}

Result<Json> read_blueprint(std::string_view bytes, const RulesSnapshot& rules,
                            const SchemaSet& schemas, InputLimits limits) {
    auto imported = import_definition(bytes, rules, schemas, limits);
    if (!imported.ok()) return {std::nullopt, std::move(imported.issues)};
    return {std::move(imported.value->definition.at("blueprint")),
            std::move(imported.issues)};
}

Result<Json> invalid_index(std::size_t index, std::size_t count) {
    auto issue = detail::error("invalid_deck_index", "Deck index is outside the authoring operation's bounds.",
                               "/blueprint/decks");
    issue.details = {{"index", index}, {"deckCount", count}};
    return {std::nullopt, {std::move(issue)}};
}

Result<Json> deck_limit(std::size_t requested, std::size_t bound) {
    auto issue = detail::error("deck_limit", "Deck count is outside the active rules limits.", "/blueprint/decks");
    issue.details = {{"requested", requested}, {"limit", bound}};
    return {std::nullopt, {std::move(issue)}};
}

std::optional<Issue> check_name(const std::optional<std::string>& name,
                                std::size_t index, const RulesSnapshot& rules) {
    if (!name) return std::nullopt;
    const auto path = "/blueprint/decks/" + std::to_string(index) + "/name";
    if (detail::character_count(*name) > rules.data().at("limits").at("maxDeckName").get<std::size_t>())
        return detail::error("field_limit", "Text exceeds the active rules character limit.", path);
    if (std::any_of(name->begin(), name->end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return detail::error("invalid_text", "Control characters are not allowed in labels or notes.", path);
    try {
        // Typed C++ arguments have not passed the JSON parser's UTF-8 check.
        // The serializer validates them without repairing or echoing input.
        static_cast<void>(Json(*name).dump());
    } catch (const Json::exception&) {
        return detail::error("invalid_text", "Deck names must be valid UTF-8.", path);
    }
    return std::nullopt;
}

Result<Json> finish(Json blueprint, const RulesSnapshot& rules,
                    const SchemaSet& schemas, InputLimits limits) {
    auto& decks = blueprint.at("decks");
    for (std::size_t index = 0; index < decks.size(); ++index) {
        if (decks[index].value("nameGenerated", false))
            decks[index]["name"] = generated_name(rules, index);
    }
    // Recompute direct totals and all import diagnostics after editing. Existing
    // geometry/IDs and explicit labels are unchanged; validation remains public.
    return read_blueprint(canonical_json(blueprint), rules, schemas, limits);
}
} // namespace

Json blank_blueprint(const RulesSnapshot& rules, BlankType type) {
    const auto& active = rules.data();
    const auto& defaults = active.at("defaults");
    Json decks = Json::array();
    for (std::size_t index = 0; index < active.at("limits").at("minDecks").get<std::size_t>(); ++index)
        decks.push_back(empty_deck(rules, index));
    return {{"schema", active.at("schemaIds").at("blueprint")},
        {"name", detail::trim_design_name(defaults.at(type == BlankType::station ? "stationName" : "shipName").get<std::string>())},
        {"buildType", type == BlankType::station ? "station" : "ship"},
        {"budget", defaults.at("budget")}, {"loads", defaults.at("loads")},
        {"decks", std::move(decks)}, {"nextId", 1}, {"migrationNotes", Json::array()}};
}

Result<Json> insert_deck(std::string_view bytes, std::size_t index,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         std::optional<std::string> name, InputLimits limits) {
    auto input = read_blueprint(bytes, rules, schemas, limits);
    if (!input.ok()) return input;
    auto& decks = input.value->at("decks");
    const auto count = decks.size();
    if (index > count) return invalid_index(index, count);
    const auto maximum = rules.data().at("limits").at("maxDecks").get<std::size_t>();
    if (count >= maximum) return deck_limit(count + 1, maximum);
    if (auto issue = check_name(name, index, rules)) return {std::nullopt, {std::move(*issue)}};
    decks.insert(decks.begin() + static_cast<Json::difference_type>(index), empty_deck(rules, index, name));
    return finish(std::move(*input.value), rules, schemas, limits);
}

Result<Json> move_deck(std::string_view bytes, std::size_t from, std::size_t to,
                       const RulesSnapshot& rules, const SchemaSet& schemas,
                       InputLimits limits) {
    auto input = read_blueprint(bytes, rules, schemas, limits);
    if (!input.ok()) return input;
    auto& decks = input.value->at("decks");
    if (from >= decks.size()) return invalid_index(from, decks.size());
    if (to >= decks.size()) return invalid_index(to, decks.size());
    Json moved = std::move(decks[from]);
    decks.erase(decks.begin() + static_cast<Json::difference_type>(from));
    decks.insert(decks.begin() + static_cast<Json::difference_type>(to), std::move(moved));
    return finish(std::move(*input.value), rules, schemas, limits);
}

Result<Json> remove_deck(std::string_view bytes, std::size_t index,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         InputLimits limits) {
    auto input = read_blueprint(bytes, rules, schemas, limits);
    if (!input.ok()) return input;
    auto& decks = input.value->at("decks");
    const auto count = decks.size();
    if (index >= count) return invalid_index(index, count);
    const auto minimum = rules.data().at("limits").at("minDecks").get<std::size_t>();
    if (count <= minimum) return deck_limit(count - 1, minimum);
    decks.erase(decks.begin() + static_cast<Json::difference_type>(index));
    return finish(std::move(*input.value), rules, schemas, limits);
}

Result<Json> rename_deck(std::string_view bytes, std::size_t index,
                         std::optional<std::string> name,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         InputLimits limits) {
    auto input = read_blueprint(bytes, rules, schemas, limits);
    if (!input.ok()) return input;
    auto& decks = input.value->at("decks");
    if (index >= decks.size()) return invalid_index(index, decks.size());
    if (auto issue = check_name(name, index, rules)) return {std::nullopt, {std::move(*issue)}};
    if (name) {
        decks[index]["name"] = *name;
        decks[index].erase("nameGenerated");
    } else {
        decks[index]["nameGenerated"] = true;
    }
    return finish(std::move(*input.value), rules, schemas, limits);
}
} // namespace apsis::shipyard
