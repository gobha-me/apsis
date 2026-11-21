#include <apsis/shipyard/perf.hpp>
#include "internal.hpp"
#include "perf_calculation.hpp"
#include "validator_internal.hpp"

#include <algorithm>
#include <cmath>

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

Json blank_blueprint(const RulesSnapshot& rules, BlankType type) {
    const auto& active = rules.data();
    const auto& defaults = active.at("defaults");
    Json decks = Json::array();
    for (std::size_t d = 0; d < active.at("limits").at("minDecks").get<std::size_t>(); ++d) {
        decks.push_back({{"name", defaults.at("deckName")},
                         {"floors", Json::object()}, {"modules", Json::array()}});
    }
    return {{"schema", active.at("schemaIds").at("blueprint")},
        {"name", defaults.at(type == BlankType::station ? "stationName" : "shipName")},
        {"buildType", type == BlankType::station ? "station" : "ship"},
        {"budget", defaults.at("budget")}, {"loads", defaults.at("loads")},
        {"decks", std::move(decks)}, {"nextId", 1}, {"migrationNotes", Json::array()}};
}

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

    auto imported = detail::import_for_validation(bytes, rules, schemas, limits);
    if (!imported.ok()) return envelope(rules, std::move(imported.issues), nullptr);
    auto definition = std::move(imported.value->definition);
    issues = std::move(imported.issues);
    if (!overrides.empty()) {
        for (const auto& [key, value] : overrides.items()) definition["blueprint"]["loads"][key] = value;
        // Recompute every direct total after overrides, including currentMass.
        auto updated = import_definition(canonical_json(definition.at("blueprint")), rules, schemas, limits);
        if (!updated.ok()) return envelope(rules, std::move(updated.issues), nullptr);
        definition = std::move(updated.value->definition);
    }
    std::erase_if(issues, [](const Issue& issue) {
        return issue.code == "pending_validation" || issue.code == "pending_performance" ||
               issue.code == "pending_docking";
    });
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
        calculated = detail::calculate_performance(definition, rules, schemas,
            definition.at("blueprint").at("loads"), limits);
        append(calculated.issues);
    } catch (const detail::IssueLimitReached&) {
        return envelope(rules, std::move(issues), nullptr);
    }
    if (!calculated.ok()) return envelope(rules, std::move(issues), nullptr);
    auto performance = std::move(*calculated.value);
    const bool ready = !has_errors(issues);
    const double jump_margin = performance.at("power").at("marginJump");
    const bool jump_ready = ready && performance.at("rating").get<double>() > 0 &&
        performance.at("jump").at("currentJumps").get<double>() >= 1 &&
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
