#include <apsis/shipyard/validator.hpp>
#include "internal.hpp"
#include "validator_internal.hpp"

#include <algorithm>

namespace apsis::shipyard {
namespace {
// The original input has already been schema-checked. Do not walk an invalid
// oversized module list merely to look for an unsupported feature. Only remove
// the field from a disposable copy; never repair geometry or inspect hold data.
bool strip_holds(Json& blueprint, const Json& rules, std::size_t depth,
                 const std::string& path, std::optional<std::string>& first) {
    if (blueprint.is_null()) return true;
    if (!blueprint.is_object() || !blueprint.contains("decks") ||
        !blueprint.at("decks").is_array() ||
        depth > rules.at("limits").at("maxCarryDepth").get<std::size_t>()) return false;
    auto& decks = blueprint.at("decks");
    if (decks.size() > rules.at("limits").at("maxDecks").get<std::size_t>()) return false;
    const auto capacity = rules.at("grid").at("w").get<std::uint64_t>() *
                          rules.at("grid").at("h").get<std::uint64_t>();
    if (blueprint.contains("holds")) {
        if (!first) first = path + "/holds";
        blueprint.erase("holds");
    }
    for (std::size_t d = 0; d < decks.size(); ++d) {
        auto& deck = decks[d];
        if (!deck.is_object() || !deck.contains("modules") || !deck.at("modules").is_array() ||
            deck.at("modules").size() > capacity || !deck.contains("floors") ||
            !deck.at("floors").is_object() || deck.at("floors").size() > capacity) return false;
        auto& modules = deck.at("modules");
        for (std::size_t m = 0; m < modules.size(); ++m) {
            auto& module = modules[m];
            if (module.is_object() && module.contains("craft")) {
                if (!strip_holds(module.at("craft"), rules, depth + 1,
                    path + "/decks/" + std::to_string(d) + "/modules/" +
                    std::to_string(m) + "/craft", first)) return false;
            }
        }
    }
    return true;
}

Json envelope(const RulesSnapshot& rules, bool pass, std::vector<Issue> issues, Json data) {
    sort_issues(issues);
    return {{"schema", "apsis-shipyard-result-v1"}, {"operation", "validate"},
        {"status", pass ? "PASS" : "FAIL"}, {"pass", pass},
        {"rulesVersion", rules.version()}, {"rulesSha256", rules.sha256()},
        {"issues", std::move(issues)}, {"data", std::move(data)}};
}
} // namespace

namespace detail {
Result<ImportedDefinition> import_for_validation(std::string_view bytes,
                                               const RulesSnapshot& rules,
                                               const SchemaSet& schemas,
                                               InputLimits limits) {
    auto imported = import_definition(bytes, rules, schemas, limits);
    if (imported.ok() || std::any_of(imported.issues.begin(), imported.issues.end(),
        [](const Issue& issue) { return issue.code == "issues_truncated"; }) ||
        std::none_of(imported.issues.begin(), imported.issues.end(),
        [](const Issue& issue) { return issue.code == "schema_error"; })) return imported;

    auto parsed = parse_json(bytes, limits);
    if (!parsed.ok()) return imported;
    auto& input = *parsed.value;
    const bool wrapped = input.is_object() && input.contains("blueprint");
    std::optional<std::string> first;
    if (!strip_holds(wrapped ? input.at("blueprint") : input, rules.data(), 0,
                     wrapped ? "/blueprint" : "", first) || !first) return imported;
    // Only a fully importable shape with holds removed earns pending feature
    // findings. Schema/scalar/grid faults take precedence, without assigning
    // any hold representation, totals, or traversal behavior.
    auto checked = import_definition(input.dump(), rules, schemas, limits);
    if (!checked.ok()) return checked;
    std::vector<Issue> issues;
    for (const auto* finding : {"F11", "F12", "F13", "F14", "F15", "F16", "F17"}) {
        add_issue(issues, {"pending_" + std::string(finding), "error",
            "Multi-deck hold validation awaits Luna; no hold behavior or totals were assigned.",
            *first, {{"finding", finding}, {"issue", 60}}});
    }
    sort_issues(issues);
    return {std::nullopt, std::move(issues)};
}
} // namespace detail

Json validate(std::string_view bytes, const RulesSnapshot& rules,
              const SchemaSet& schemas, ValidationOptions options, InputLimits limits) {
    auto imported = detail::import_for_validation(bytes, rules, schemas, limits);
    if (!imported.ok()) return envelope(rules, false, std::move(imported.issues), nullptr);
    auto definition = std::move(imported.value->definition);
    auto issues = std::move(imported.issues);
    std::erase_if(issues, [](const Issue& issue) {
        return issue.code == "pending_validation" || issue.code == "pending_docking";
    });
    auto structure = detail::inspect_structure(definition.at("blueprint"), rules, "/blueprint", options);
    try {
        // Defer an existing terminal marker until its bounded diagnostics have
        // been appended; the shared helper also bounds loader + structure.
        const Issue* truncated = nullptr;
        for (const auto& issue : structure.issues) {
            if (issue.code == "issues_truncated") truncated = &issue;
            else detail::add_issue(issues, issue);
        }
        if (truncated) {
            detail::add_issue(issues, *truncated);
            // A source marker is terminal even if the combined list has room.
            return envelope(rules, false, std::move(issues), nullptr);
        }
    } catch (const detail::IssueLimitReached&) {
        return envelope(rules, false, std::move(issues), nullptr);
    }
    sort_issues(issues);
    const bool pass = std::none_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.severity == "error";
    });
    // Structural approval is explicit below; complete readiness still requires
    // #51's rule-defined performance checks. Never equate range with jumpReady.
    definition["validation"] = {{"ready", pass ? Json(nullptr) : Json(false)},
        {"jumpReady", nullptr}, {"issues", issues}};
    const auto& migration = imported.value->migration;
    Json data = {{"definition", std::move(definition)}, {"structuralPass", pass},
        {"checks", {{"crewRoutes", options.crewRoutes}, {"approachClearance", options.approachClearance}}},
        {"migration", {{"sourceVersion", migration.sourceVersion}, {"targetVersion", migration.targetVersion},
            {"rulesIdentityChanged", migration.rulesIdentityChanged}, {"changes", migration.changes}}}};
    return envelope(rules, pass, std::move(issues), std::move(data));
}
} // namespace apsis::shipyard
