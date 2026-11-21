#include <apsis/shipyard/validator.hpp>
#include "internal.hpp"
#include "validator_internal.hpp"

#include <algorithm>

namespace apsis::shipyard {
namespace {
// Hold-specific rules and even the final accepted input shape await Luna.
// Recognize the requested feature without fabricating a hold schema, totals,
// floor repairs, or traversal through its required opening.
void find_holds(const Json& blueprint, const std::string& path,
                std::vector<Issue>& issues) {
    if (!blueprint.is_object()) return;
    if (blueprint.contains("holds")) {
        for (const auto* finding : {"F11", "F12", "F13", "F14", "F15", "F16", "F17"}) {
            issues.push_back({"pending_" + std::string(finding), "error",
                "Multi-deck hold validation awaits Luna; no hold behavior or totals were assigned.",
                path + "/holds", {{"finding", finding}, {"issue", 60}}});
        }
    }
    if (!blueprint.contains("decks") || !blueprint.at("decks").is_array()) return;
    const auto& decks = blueprint.at("decks");
    for (std::size_t d = 0; d < decks.size(); ++d) {
        const auto& deck = decks[d];
        if (!deck.is_object() || !deck.contains("modules") || !deck.at("modules").is_array()) continue;
        for (std::size_t m = 0; m < deck.at("modules").size(); ++m) {
            const auto& module = deck.at("modules")[m];
            if (module.is_object() && module.contains("craft")) {
                find_holds(module.at("craft"), path + "/decks/" + std::to_string(d) +
                           "/modules/" + std::to_string(m) + "/craft", issues);
            }
        }
    }
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
std::vector<Issue> pending_holds(const Json& input, const std::string& path) {
    std::vector<Issue> issues;
    const bool wrapped = input.is_object() && input.contains("blueprint");
    find_holds(wrapped ? input.at("blueprint") : input,
               wrapped ? path + "/blueprint" : path, issues);
    sort_issues(issues);
    return issues;
}
} // namespace detail

Json validate(std::string_view bytes, const RulesSnapshot& rules,
              const SchemaSet& schemas, ValidationOptions options, InputLimits limits) {
    // Parse before probing unimplemented features so the same resource and JSON
    // guards apply to every path, including requests that contain hold records.
    auto parsed = parse_json(bytes, limits);
    if (!parsed.ok()) return envelope(rules, false, std::move(parsed.issues), nullptr);
    auto pending = detail::pending_holds(*parsed.value);
    if (!pending.empty()) return envelope(rules, false, std::move(pending), nullptr);

    auto imported = import_definition(bytes, rules, schemas, limits);
    if (!imported.ok()) return envelope(rules, false, std::move(imported.issues), nullptr);
    auto definition = std::move(imported.value->definition);
    auto issues = std::move(imported.issues);
    std::erase_if(issues, [](const Issue& issue) {
        return issue.code == "pending_validation" || issue.code == "pending_docking";
    });
    auto structure = detail::inspect_structure(definition.at("blueprint"), rules, "/blueprint", options);
    issues.insert(issues.end(), structure.issues.begin(), structure.issues.end());
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
