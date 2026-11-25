#include <apsis/shipyard/authoring.hpp>
#include "internal.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace apsis::shipyard {
namespace {
struct GeneratedDeckName {
    std::string_view stem;
    std::string level;

    std::string text() const {
        if (stem.empty()) return level;
        return std::string(stem) + ' ' + level;
    }

    std::size_t encoded_size() const {
        // The rules loader excludes ASCII controls. Canonical JSON retains
        // UTF-8 and escapes only quotes and backslashes in the name content.
        const auto escapes = std::count_if(stem.begin(), stem.end(),
            [](char c) { return c == '"' || c == '\\'; });
        return stem.size() + static_cast<std::size_t>(escapes) + !stem.empty() + level.size();
    }
};

std::optional<GeneratedDeckName> generated_name(std::string_view stem, std::size_t maximum,
                                                std::size_t index) {
    auto level = std::to_string(index + 1);
    if (level.size() > maximum) return std::nullopt;
    const auto space_and_stem = maximum - level.size();
    if (space_and_stem == 0) return GeneratedDeckName{{}, std::move(level)};
    // Keep the entire level and cut the validated UTF-8 stem between scalars.
    std::size_t characters = 0;
    std::size_t offset = 0;
    for (const unsigned char byte : stem) {
        if ((byte & 0xc0) != 0x80 && ++characters == space_and_stem) {
            return GeneratedDeckName{stem.substr(0, offset), std::move(level)};
        }
        ++offset;
    }
    return GeneratedDeckName{stem, std::move(level)};
}

Json empty_deck(std::string name, bool generated) {
    Json deck = {{"name", std::move(name)},
                 {"floors", Json::object()}, {"modules", Json::array()}};
    if (generated) deck["nameGenerated"] = true;
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
    const auto& stem = rules.data().at("defaults").at("deckName").get_ref<const std::string&>();
    const auto maximum = rules.data().at("limits").at("maxDeckName").get<std::size_t>();
    auto& decks = blueprint.at("decks");
    for (std::size_t index = 0; index < decks.size(); ++index) {
        if (!decks[index].value("nameGenerated", false)) continue;
        const auto name = generated_name(stem, maximum, index);
        if (!name) {
            return {std::nullopt, {detail::error("field_limit", "Generated deck level exceeds the active name character limit.",
                "/blueprint/decks/" + std::to_string(index) + "/name")}};
        }
        decks[index]["name"] = name->text();
    }
    // Recompute direct totals and all import diagnostics after editing. Existing
    // geometry/IDs and explicit labels are unchanged; validation remains public.
    return read_blueprint(canonical_json(blueprint), rules, schemas, limits);
}
} // namespace

Json blank_blueprint(const RulesSnapshot& rules, BlankType type) {
    const auto& active = rules.data();
    const auto& defaults = active.at("defaults");
    const auto& limits = active.at("limits");
    constexpr std::size_t max_generated_decks = 4096;
    std::size_t remaining_name_bytes = 8 * 1024 * 1024;
    const auto deck_count = limits.at("minDecks").get<std::size_t>();
    if (deck_count > max_generated_decks)
        throw std::length_error("Blank blueprint exceeds the operational deck allocation limit.");
    const auto& stem = defaults.at("deckName").get_ref<const std::string&>();
    const auto maximum = limits.at("maxDeckName").get<std::size_t>();
    Json decks = Json::array();
    for (std::size_t index = 0; index < deck_count; ++index) {
        const auto name = generated_name(stem, maximum, index);
        if (!name)
            throw std::length_error("Blank blueprint deck level exceeds the active name character limit.");
        const auto bytes = name->encoded_size();
        if (bytes > remaining_name_bytes)
            throw std::length_error("Blank blueprint exceeds the operational deck name byte limit.");
        remaining_name_bytes -= bytes;
        decks.push_back(empty_deck(name->text(), true));
    }
    const auto& name = defaults.at(type == BlankType::station ? "stationName" : "shipName")
                           .get_ref<const std::string&>();
    return {{"schema", active.at("schemaIds").at("blueprint")},
        {"name", detail::normalize_design_name(name)},
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
    decks.insert(decks.begin() + static_cast<Json::difference_type>(index), empty_deck(name.value_or(""), !name));
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
