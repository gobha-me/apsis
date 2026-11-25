#include <apsis/shipyard/validator.hpp>
#include "internal.hpp"
#include "validator_internal.hpp"

#include <algorithm>

namespace apsis::shipyard {
namespace {
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
                                               InputLimits limits,
                                               const Json& load_overrides,
                                               CarriedTotals* performance_totals) {
    auto parsed = parse_json(bytes, limits);
    if (!parsed.ok()) return {std::nullopt, std::move(parsed.issues)};
    return import_parsed_definition(*parsed.value, rules, schemas, load_overrides, performance_totals);
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
    // perf()'s rule-defined performance checks. Never equate range with jumpReady.
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
