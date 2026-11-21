#include <apsis/shipyard/validator.hpp>
#include "validator_internal.hpp"

#include <algorithm>
#include <utility>

namespace apsis::shipyard {
namespace {
bool errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.severity == "error";
    });
}

std::string docking_severity(const RulesSnapshot& rules) {
    const auto& validation = rules.data().at("validation");
    return validation.at("checks").at("docking").value(
        "severity", validation.at("defaultSeverity").get<std::string>());
}

Json envelope(const RulesSnapshot& rules, bool pass, std::vector<Issue> issues, Json data) {
    sort_issues(issues);
    return {{"schema", "apsis-shipyard-result-v1"}, {"operation", "dock"},
            {"status", pass ? "PASS" : "FAIL"}, {"pass", pass},
            {"rulesVersion", rules.version()}, {"rulesSha256", rules.sha256()},
            {"issues", issues}, {"data", std::move(data)}};
}

void import_issues(std::vector<Issue>& issues, const std::vector<Issue>& source,
                   const std::string& prefix) {
    for (auto issue : source) {
        // These loader markers are replaced by the actual operations below.
        if (issue.code == "pending_validation" || issue.code == "pending_docking") continue;
        issue.path = prefix + issue.path;
        issues.push_back(std::move(issue));
    }
}

Json check(bool enabled, bool pass, Json details = Json::object()) {
    return {{"enabled", enabled}, {"status", enabled ? (pass ? "PASS" : "FAIL") : "DISABLED"},
            {"details", std::move(details)}};
}
} // namespace

Json dock(std::string_view host_bytes, std::size_t target_deck,
          std::uint64_t target_module_id, std::string_view visitor_bytes,
          const RulesSnapshot& rules, const SchemaSet& schemas,
          DockOptions options, InputLimits limits) {
    std::vector<Issue> issues;
    // Holds have a settled representation but no approved access/traversal
    // rules. Report their pending contract before unsupported shape fields.
    const auto host_input = parse_json(host_bytes, limits);
    const auto visitor_input = parse_json(visitor_bytes, limits);
    import_issues(issues, host_input.issues, "/host");
    import_issues(issues, visitor_input.issues, "/visitor");
    if (!host_input.ok() || !visitor_input.ok()) return envelope(rules, false, std::move(issues), nullptr);
    auto host_holds = detail::pending_holds(*host_input.value, "/host");
    auto visitor_holds = detail::pending_holds(*visitor_input.value, "/visitor");
    issues.insert(issues.end(), host_holds.begin(), host_holds.end());
    issues.insert(issues.end(), visitor_holds.begin(), visitor_holds.end());
    if (!host_holds.empty() || !visitor_holds.empty()) return envelope(rules, false, std::move(issues), nullptr);
    auto host = import_definition(host_bytes, rules, schemas, limits);
    auto visitor = import_definition(visitor_bytes, rules, schemas, limits);
    import_issues(issues, host.issues, "/host");
    import_issues(issues, visitor.issues, "/visitor");
    if (!host.ok() || !visitor.ok()) return envelope(rules, false, std::move(issues), nullptr);

    const auto& host_blueprint = host.value->definition.at("blueprint");
    const auto& visitor_blueprint = visitor.value->definition.at("blueprint");
    const auto severity = docking_severity(rules);
    const auto invalid = [&](std::string code, std::string message, std::string path) {
        issues.push_back({std::move(code), severity, std::move(message), std::move(path), Json::object()});
    };
    if (host_blueprint.at("buildType") != "station") {
        invalid("invalid_docking_host", "Docking requires a station host.", "/host/blueprint/buildType");
    }
    if (visitor_blueprint.at("buildType") != "ship") {
        invalid("invalid_docking_visitor", "Docking requires a visiting ship.", "/visitor/blueprint/buildType");
    }
    if (host_blueprint.at("buildType") != "station" || visitor_blueprint.at("buildType") != "ship") {
        return envelope(rules, false, std::move(issues), nullptr);
    }

    auto host_structure = detail::inspect_structure(host_blueprint, rules, "/host/blueprint");
    auto visitor_structure = detail::inspect_structure(visitor_blueprint, rules, "/visitor/blueprint");
    const auto target = std::find_if(host_structure.modules.begin(), host_structure.modules.end(),
        [&](const detail::ModuleState& state) {
            return state.deck == target_deck && state.id == target_module_id;
        });
    if (target == host_structure.modules.end() || !target->part->contains("docking")) {
        invalid("invalid_docking_target", "Target deck/module does not identify a station docking target.", "/target");
        return envelope(rules, false, std::move(issues), nullptr);
    }
    const auto& docking = target->part->at("docking");
    const auto kind = docking.at("kind").get<std::string>();
    const auto& target_kinds = rules.data().at("docking").at("targetKinds");
    if (std::find(target_kinds.begin(), target_kinds.end(), kind) == target_kinds.end()) {
        invalid("invalid_docking_target", "Target part is not a supported station port or hangar.", target->path);
        return envelope(rules, false, std::move(issues), nullptr);
    }

    Json checks = Json::object();
    std::vector<Issue> structural_issues;
    for (const auto& issue : host_structure.issues) {
        // Defaults retain every host structural fault in (a). Disabling (b)
        // or (d) also disables that selected target's underlying rule family
        // in (a), allowing each check to be mutated independently.
        if (issue.path == target->path &&
            ((!options.portReachability && issue.code == "unreachable_component") ||
             (!options.approachClearance && issue.code == "blocked_approach"))) continue;
        structural_issues.push_back(issue);
    }
    structural_issues.insert(structural_issues.end(), visitor_structure.issues.begin(), visitor_structure.issues.end());
    checks["a"] = check(options.structural, !errors(structural_issues),
                        {{"structuralIssues", structural_issues}});
    if (options.structural) issues.insert(issues.end(), structural_issues.begin(), structural_issues.end());

    checks["b"] = check(options.portReachability, target->reachable,
                        {{"crewReachable", target->reachable}});
    if (options.portReachability && !target->reachable) {
        issues.push_back({"dock_port_unreachable", severity, "Station crew cannot reach the docking target.",
                          target->path, {{"check", "b"}, {"deck", target_deck}, {"moduleId", target_module_id}}});
    }

    std::size_t airlock_count = 0, interface_count = 0, exterior_count = 0;
    for (const auto& airlock : visitor_structure.modules) {
        if (!airlock.part->contains("docking") || airlock.part->at("docking").at("kind") != "airlock") continue;
        ++airlock_count;
        if (airlock.interface != target->interface) continue;
        ++interface_count;
        // Reachability and clear approach remain structural (a) requirements.
        // Here exterior includes the airlock's settled outward-facing condition.
        if (airlock.exterior) ++exterior_count;
    }
    checks["c"] = check(options.matchingAirlock, exterior_count > 0,
                        {{"airlockCount", airlock_count}, {"compatibleInterfaceCount", interface_count},
                         {"exteriorOutwardCount", exterior_count}});
    bool pending = false;
    if (options.matchingAirlock) {
        if (exterior_count == 0) {
            issues.push_back({"dock_no_matching_airlock", severity,
                "No compatible visitor airlock is on the exterior hull facing outward.",
                "/visitor/blueprint/decks", {{"check", "c"}, {"interface", target->interface}}});
        } else {
            pending = true;
            checks["c"]["status"] = "PENDING";
            issues.push_back({"pending_F21", "warning",
                "pending F21: same-deck mapping and the 1-cell measurement convention await Luna; alignment is not certified.",
                "/visitor/blueprint/decks", {{"finding", "F21"}, {"check", "c"}}});
        }
    }

    checks["d"] = check(options.approachClearance, target->clear,
                        {{"approachClear", target->clear}, {"exteriorOutward", target->exterior}});
    if (options.approachClearance && !target->clear) {
        issues.push_back({"dock_blocked_approach", severity, "The docking target's exterior approach is blocked.",
                          target->path, {{"check", "d"}, {"deck", target_deck}, {"moduleId", target_module_id}}});
    }

    Json data = {{"target", {{"deck", target_deck}, {"moduleId", target_module_id},
                            {"kind", kind}, {"interface", target->interface}}},
                 {"checks", std::move(checks)}};
    if (kind == "hangar") {
        const auto& dimensions = visitor.value->definition.at("derived");
        const auto deck_count = visitor_blueprint.at("decks").size();
        const auto max_decks = docking.at("maxDecks").get<std::size_t>();
        data["hangar"] = {{"visitorWidthM", dimensions.at("widthM")},
                           {"visitorLengthM", dimensions.at("lengthM")}, {"visitorDeckCount", deck_count},
                           {"maxWidthM", docking.at("maxWidthM")}, {"maxLengthM", docking.at("maxLengthM")},
                           {"maxDecks", max_decks}, {"deckCountFits", deck_count <= max_decks},
                           {"widthLengthFit", nullptr}};
        if (deck_count > max_decks) {
            issues.push_back({"dock_hangar_deck_limit", severity, "Visitor exceeds the hangar's rule-defined deck limit.",
                              "/visitor/blueprint/decks", {{"actual", deck_count}, {"maximum", max_decks}}});
        }
        pending = true;
        issues.push_back({"pending_F19", "warning",
            "pending F19: allowed visitor docking rotations await Luna; hangar width/length fit is not certified.",
            "/visitor/blueprint/decks", {{"finding", "F19"}}});
    }
    data["pending"] = pending;
    const auto& measured_checks = data.at("checks");
    const bool failed_check = std::any_of(measured_checks.begin(), measured_checks.end(), [](const Json& value) {
        return value.at("enabled").get<bool>() && value.at("status") == "FAIL";
    });
    const bool pass = !errors(issues) && !failed_check && !pending;
    return envelope(rules, pass, std::move(issues), std::move(data));
}
} // namespace apsis::shipyard
