#include <apsis/shipyard/validator.hpp>
#include "validator_internal.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cmath>
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
            {"issues", std::move(issues)}, {"data", std::move(data)}};
}

bool pending_hold(const Issue& issue) {
    return issue.code.size() == 11 && issue.code.starts_with("pending_F1") &&
           issue.code.back() >= '1' && issue.code.back() <= '7';
}

bool truncated(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.code == "issues_truncated";
    });
}

void import_issues(std::vector<Issue>& issues, const std::vector<Issue>& source,
                   const std::string& prefix, bool hold_request = false) {
    const Issue* marker = nullptr;
    for (const auto& original : source) {
        // A source limit is terminal even if this operation filters some of
        // its diagnostics. Its library-wide marker always retains path "".
        if (original.code == "issues_truncated") { marker = &original; continue; }
        if (original.code == "pending_validation" || original.code == "pending_docking") continue;
        // Hold requests have no definition data. Keep their seven findings
        // once per field kind, without unrelated successful-import warnings.
        if (hold_request && original.severity != "error" && !pending_hold(original)) continue;
        if (pending_hold(original) && std::any_of(issues.begin(), issues.end(),
            [&](const Issue& existing) { return existing.code == original.code; })) continue;
        auto issue = original;
        issue.path = prefix + issue.path;
        detail::add_issue(issues, std::move(issue));
    }
    if (marker) {
        detail::add_issue(issues, *marker);
        throw detail::IssueLimitReached{};
    }
}

Json check(bool enabled, bool pass, Json details = Json::object()) {
    return {{"enabled", enabled}, {"status", enabled ? (pass ? "PASS" : "FAIL") : "DISABLED"},
            {"details", std::move(details)}};
}

// The optional v4 policy requires this bound when present.
constexpr std::int64_t maximum_distance_cells = 1;

bool within_port_alignment(const detail::ModuleState& airlock, const detail::ModuleState& port) {
    const auto dx = (2 * airlock.x + airlock.width) - (2 * port.x + port.width);
    const auto dy = (2 * airlock.y + airlock.height) - (2 * port.y + port.height);
    return (dx == 0 || dy == 0) && std::abs(dx) + std::abs(dy) <= 2 * maximum_distance_cells;
}
} // namespace

Json dock(std::string_view host_bytes, std::size_t target_deck,
          std::uint64_t target_module_id, std::string_view visitor_bytes,
          const RulesSnapshot& rules, const SchemaSet& schemas,
          DockOptions options, InputLimits limits) {
    std::vector<Issue> issues;
    try {
        // The shared import path checks schema and scalar/grid bounds before
        // recognizing unsupported holds, once per field kind.
        auto host = detail::import_for_validation(host_bytes, rules, schemas, limits);
        if (truncated(host.issues)) import_issues(issues, host.issues, "/host");
        auto visitor = detail::import_for_validation(visitor_bytes, rules, schemas, limits);
        const bool hold_request = std::any_of(host.issues.begin(), host.issues.end(), pending_hold) ||
                                  std::any_of(visitor.issues.begin(), visitor.issues.end(), pending_hold);
        import_issues(issues, host.issues, "/host", hold_request);
        import_issues(issues, visitor.issues, "/visitor", hold_request);
        if (!host.ok() || !visitor.ok()) return envelope(rules, false, std::move(issues), nullptr);

        const auto& host_blueprint = host.value->definition.at("blueprint");
        const auto& visitor_blueprint = visitor.value->definition.at("blueprint");
        const auto severity = docking_severity(rules);
        const auto invalid = [&](std::string code, std::string message, std::string path) {
            detail::add_issue(issues, {std::move(code), severity, std::move(message), std::move(path), Json::object()});
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
        if (truncated(host_structure.issues)) import_issues(issues, host_structure.issues, "");
        auto visitor_structure = detail::inspect_structure(visitor_blueprint, rules, "/visitor/blueprint");
        if (truncated(visitor_structure.issues)) import_issues(issues, visitor_structure.issues, "");
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
        try {
            for (const auto& issue : host_structure.issues) {
                // Defaults retain every host structural fault in (a). Disabling (b)
                // or (d) also disables that selected target's underlying rule family
                // in (a), allowing each check to be mutated independently.
                if (issue.path == target->path &&
                    ((!options.portReachability && issue.code == "unreachable_component") ||
                     (!options.approachClearance && issue.code == "blocked_approach"))) continue;
                detail::add_issue(structural_issues, issue);
            }
            for (const auto& issue : visitor_structure.issues) detail::add_issue(structural_issues, issue);
        } catch (const detail::IssueLimitReached&) {
            if (options.structural) import_issues(issues, structural_issues, "");
            // Resource limits remain blocking when the diagnostic check is disabled.
            const auto marker = std::find_if(structural_issues.begin(), structural_issues.end(),
                [](const Issue& issue) { return issue.code == "issues_truncated"; });
            detail::add_issue(issues, *marker);
            throw;
        }
        checks["a"] = check(options.structural, !errors(structural_issues),
                            {{"structuralIssues", structural_issues}});
        if (options.structural) import_issues(issues, structural_issues, "");

        checks["b"] = check(options.portReachability, target->reachable,
                            {{"crewReachable", target->reachable}});
        if (options.portReachability && !target->reachable) {
            detail::add_issue(issues, {"dock_port_unreachable", severity, "Station crew cannot reach the docking target.",
                              target->path, {{"check", "b"}, {"deck", target_deck}, {"moduleId", target_module_id}}});
        }

        const bool port = kind == "port";
        std::size_t airlock_count = 0, interface_count = 0, exterior_count = 0;
        std::size_t same_deck_count = 0, aligned_count = 0;
        for (const auto& airlock : visitor_structure.modules) {
            if (!airlock.part->contains("docking") || airlock.part->at("docking").at("kind") != "airlock") continue;
            ++airlock_count;
            if (airlock.interface != target->interface) continue;
            ++interface_count;
            // Reachability and clear approach remain structural (a) requirements.
            if (!airlock.exterior) continue;
            ++exterior_count;
            if (!port || airlock.deck != target->deck) continue;
            ++same_deck_count;
            if (within_port_alignment(airlock, *target)) ++aligned_count;
        }
        Json airlock_details = {{"airlockCount", airlock_count}, {"compatibleInterfaceCount", interface_count},
                                {"exteriorOutwardCount", exterior_count}};
        bool pending = false;
        if (port) {
            airlock_details.update({{"sameDeckExteriorCount", same_deck_count}, {"alignedAirlockCount", aligned_count},
                                    {"distanceMetric", "manhattan"}, {"referencePoint", "centres"},
                                    {"maximumDistanceCells", maximum_distance_cells}});
            checks["c"] = check(options.matchingAirlock, aligned_count > 0, std::move(airlock_details));
            if (options.matchingAirlock && aligned_count == 0) {
                detail::add_issue(issues, {"dock_no_matching_airlock", severity,
                    "No compatible exterior outward-facing visitor airlock is on the port deck within the orthogonal centre-distance limit.",
                    "/visitor/blueprint/decks", {{"check", "c"}, {"interface", target->interface},
                        {"distanceMetric", "manhattan"}, {"referencePoint", "centres"},
                        {"maximumDistanceCells", maximum_distance_cells}}});
            }
        } else {
            checks["c"] = check(options.matchingAirlock, false, std::move(airlock_details));
            if (options.matchingAirlock) {
                if (exterior_count == 0) {
                    detail::add_issue(issues, {"dock_no_matching_airlock", severity,
                        "No compatible visitor airlock is on the exterior hull facing outward.",
                        "/visitor/blueprint/decks", {{"check", "c"}, {"interface", target->interface}}});
                } else {
                    pending = true;
                    checks["c"]["status"] = "PENDING";
                    detail::add_issue(issues, {"pending_F21", "warning",
                        "pending F21: same-deck mapping and the 1-cell measurement convention await Luna; alignment is not certified.",
                        "/visitor/blueprint/decks", {{"finding", "F21"}, {"check", "c"}}});
                }
            }
        }

        checks["d"] = check(options.approachClearance, target->clear,
                            {{"approachClear", target->clear}, {"exteriorOutward", target->exterior}});
        if (options.approachClearance && !target->clear) {
            detail::add_issue(issues, {"dock_blocked_approach", severity, "The docking target's exterior approach is blocked.",
                              target->path, {{"check", "d"}, {"deck", target_deck}, {"moduleId", target_module_id}}});
        }

        Json data = {{"target", {{"deck", target_deck}, {"moduleId", target_module_id},
                                {"kind", kind}, {"interface", target->interface}}},
                     {"checks", std::move(checks)}};
        if (kind == "hangar") {
            const auto& dimensions = visitor.value->definition.at("derived");
            const auto width = dimensions.at("widthM").get<double>();
            const auto length = dimensions.at("lengthM").get<double>();
            const auto max_width = docking.at("maxWidthM").get<double>();
            const auto max_length = docking.at("maxLengthM").get<double>();
            const auto epsilon = rules.data().at("numeric").at("epsilon").get<double>();
            const auto deck_count = visitor_blueprint.at("decks").size();
            const auto max_decks = docking.at("maxDecks").get<std::size_t>();
            data["hangar"] = {{"visitorWidthM", dimensions.at("widthM")},
                               {"visitorLengthM", dimensions.at("lengthM")}, {"visitorDeckCount", deck_count},
                               {"maxWidthM", docking.at("maxWidthM")}, {"maxLengthM", docking.at("maxLengthM")},
                               {"maxDecks", max_decks}, {"deckCountFits", deck_count <= max_decks},
                               {"widthLengthFit", nullptr}};
            if (deck_count > max_decks) {
                detail::add_issue(issues, {"dock_hangar_deck_limit", severity, "Visitor exceeds the hangar's rule-defined deck limit.",
                                  "/visitor/blueprint/decks", {{"actual", deck_count}, {"maximum", max_decks}}});
            }
            if (width <= max_width && length <= max_length) {
                // An unrotated fit does not require an F19 rotation decision.
                data["hangar"]["widthLengthFit"] = true;
            } else if (std::min(width, length) > std::min(max_width, max_length) ||
                       std::max(width, length) > std::hypot(max_width, max_length) + epsilon) {
                // Minimum span and diagonal bounds rule out every rotation.
                // Failure of both axis-aligned fits alone is inconclusive.
                data["hangar"]["widthLengthFit"] = false;
                detail::add_issue(issues, {"dock_hangar_size_limit", severity,
                    "Visitor exceeds a rotation-proof bound of the hangar's width/length envelope.",
                    "/visitor/blueprint/decks", {{"visitorWidthM", width}, {"visitorLengthM", length},
                                              {"maxWidthM", max_width}, {"maxLengthM", max_length}}});
            } else {
                pending = true;
                detail::add_issue(issues, {"pending_F19", "warning",
                    "pending F19: allowed visitor docking rotations await Luna; hangar width/length fit is not certified.",
                    "/visitor/blueprint/decks", {{"finding", "F19"}}});
            }
        }
        data["pending"] = pending;
        const auto& measured_checks = data.at("checks");
        const bool failed_check = std::any_of(measured_checks.begin(), measured_checks.end(), [](const Json& value) {
            return value.at("enabled").get<bool>() && value.at("status") == "FAIL";
        });
        const bool pass = !errors(issues) && !failed_check && !pending;
        return envelope(rules, pass, std::move(issues), std::move(data));
    } catch (const detail::IssueLimitReached&) {
        return envelope(rules, false, std::move(issues), nullptr);
    }
}
} // namespace apsis::shipyard
