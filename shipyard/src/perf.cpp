#include <apsis/shipyard/perf.hpp>
#include "internal.hpp"
#include "perf_calculation.hpp"
#include "validator_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace apsis::shipyard {
namespace {
bool has_errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.severity == "error";
    });
}

Json envelope(const RulesSnapshot& rules, std::vector<Issue> issues, Json data) {
    sort_issues(issues);
    const bool pass = !has_errors(issues);
    return {{"schema", "apsis-shipyard-result-v1"}, {"operation", "perf"},
        {"status", pass ? "PASS" : "FAIL"}, {"pass", pass},
        {"rulesVersion", rules.version()}, {"rulesSha256", rules.sha256()},
        {"issues", std::move(issues)}, {"data", std::move(data)}};
}
} // namespace

Json perf(std::string_view bytes, const RulesSnapshot& rules,
          const SchemaSet& schemas, LoadFractions loads, InputLimits limits) {
    Json overrides = Json::object();
    std::vector<Issue> issues;
    const auto override_load = [&](const char* key, const std::optional<double>& value) {
        if (!value) return;
        if (!std::isfinite(*value) || *value < 0 || *value > 1) {
            detail::add_issue(issues, detail::error("invalid_load", "Load fractions must be finite and between zero and one.",
                                           std::string("/loads/") + key));
        } else overrides[key] = *value;
    };
    override_load("cargo", loads.cargo);
    override_load("jumpFuel", loads.jumpFuel);
    override_load("sublightFuel", loads.sublightFuel);
    if (has_errors(issues)) return envelope(rules, std::move(issues), nullptr);

    detail::CarriedTotals carried_totals;
    auto imported = detail::import_for_validation(bytes, rules, schemas, limits, overrides, &carried_totals);
    if (!imported.ok()) return envelope(rules, std::move(imported.issues), nullptr);
    auto definition = std::move(imported.value->definition);
    issues = std::move(imported.issues);
    auto structure = detail::inspect_structure(definition.at("blueprint"), rules);
    const bool structural_pass = !has_errors(structure.issues);
    const auto append = [&](const std::vector<Issue>& additions) {
        const Issue* truncated = nullptr;
        for (const auto& issue : additions) {
            if (issue.code == "issues_truncated") truncated = &issue;
            else detail::add_issue(issues, issue);
        }
        if (truncated) {
            detail::add_issue(issues, *truncated);
            throw detail::IssueLimitReached{};
        }
    };
    Result<Json> calculated;
    try {
        append(structure.issues);
        calculated = detail::calculate_performance(definition.at("blueprint"), definition.at("derived"),
            rules, carried_totals);
        append(calculated.issues);
    } catch (const detail::IssueLimitReached&) {
        return envelope(rules, std::move(issues), nullptr);
    }
    if (!calculated.ok()) return envelope(rules, std::move(issues), nullptr);
    auto performance = std::move(*calculated.value);
    const bool ready = !has_errors(issues);
    const double jump_margin = performance.at("power").at("marginJump");
    const auto& jump = performance.at("jump");
    const bool jump_ready = ready && !jump.at("driveCosts").empty() &&
        performance.at("rating").get<double>() > 0 &&
        jump.at("currentJumps").get<double>() >= 1 &&
        detail::has_jump_fuel(jump.at("current"), rules) &&
        (jump_margin >= -rules.data().at("numeric").at("epsilon").get<double>());
    sort_issues(issues);
    definition["derived"]["performance"] = performance;
    definition["validation"] = {{"ready", ready}, {"jumpReady", jump_ready}, {"issues", issues}};
    const auto& migration = imported.value->migration;
    Json data = {{"definition", std::move(definition)}, {"performance", std::move(performance)},
        {"structuralPass", structural_pass}, {"ready", ready}, {"jumpReady", jump_ready},
        {"migration", {{"sourceVersion", migration.sourceVersion}, {"targetVersion", migration.targetVersion},
            {"rulesIdentityChanged", migration.rulesIdentityChanged}, {"changes", migration.changes}}}};
    return envelope(rules, std::move(issues), std::move(data));
}
} // namespace apsis::shipyard
