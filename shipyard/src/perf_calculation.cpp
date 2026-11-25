#include "perf_calculation.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace apsis::shipyard::detail {
namespace {
struct FormulaFailure {};

struct Calculator {
    const RulesSnapshot& snapshot;
    const Json& rules;
    const Json& blueprint;
    const Json& totals;
    const Json& policy;
    const Json& loads;
    std::vector<Issue> issues;
    std::map<std::string, const Json*, std::less<>> parts;
    double thrust = 0, burn_rate = 0;
    double supply = 0, idle = 0, cruise = 0, jump = 0;
    std::size_t module_count = 0, engine_count = 0, life_support_count = 0;
    bool blank = true;
    Json drives = Json::array();
    double drive_cost = 0;

    Calculator(const Json& input, const RulesSnapshot& active, const Json& current_loads)
        : snapshot(active), rules(active.data()),
          blueprint(input.at("blueprint")), totals(input.at("derived")),
          policy(rules.at("performance")), loads(current_loads) {
        for (const auto& part : rules.at("parts")) {
            parts.emplace(part.at("id").get<std::string>(), &part);
        }
        for (std::size_t d = 0; d < blueprint.at("decks").size(); ++d) {
            const auto& deck = blueprint.at("decks")[d];
            blank = blank && deck.at("floors").empty() && deck.at("modules").empty();
            for (const auto& module : deck.at("modules")) {
                ++module_count;
                const auto& part = *parts.at(module.at("type").get<std::string>());
                const auto system = part.value("system", std::string());
                engine_count += system == "engine";
                life_support_count += system == "lifeSupport";
                thrust += part.value("thrustKN", 0.0);
                burn_rate += part.value("burnRate", 0.0);
                supply += part.value("generation", 0.0);
                idle += part.at("powerModes").at("idle").get<double>();
                cruise += part.at("powerModes").at("cruise").get<double>();
                jump += part.at("powerModes").at("jump").get<double>();
                if (system == "jumpDrive") {
                    drive_cost += part.at("cost").get<double>();
                    drives.push_back({{"deck", d}, {"moduleId", module.at("id")},
                        {"part", part.at("id")}, {"rating", part.contains("effect") ? part.at("effect").value("rating", 0.0) : 0.0},
                        {"cost", part.at("cost")}});
                }
            }
        }
        blank = blank && number("dryMass") == 0 && number("currentMass") == 0;
    }

    double number(const std::string& key) const { return totals.at(key).get<double>(); }

    std::string severity(const std::string& code) const {
        const auto& checks = rules.at("validation").at("checks");
        return checks.contains(code) ? checks.at(code).at("severity").get<std::string>() : "warning";
    }

    void advisory(const std::string& code, std::string message,
                  std::string path, Json details = Json::object()) {
        details["rule"] = "/validation/checks/" + code;
        add_issue(issues, {code, severity(code), std::move(message), std::move(path), std::move(details)});
    }

    bool near_less(double left, double right) const {
        return left - right < -snapshot.data().at("numeric").at("epsilon").get<double>();
    }

    bool allowed_f20(const Issue& issue, std::string_view name) const {
        const auto& node = rules.at(Json::json_pointer(issue.path));
        const bool engine_less = engine_count == 0 && burn_rate == 0;
        if (issue.code != "formula_error") return false;
        // Missing exhaust velocity is a dependency of the unresolved engine-less
        // evaluation, never permission to substitute a zero exhaust velocity.
        if (name == "deltaV" && engine_less && node.is_string() && node == "exhaustVelocity") return true;
        if (issue.details.value("operator", std::string()) == "ln") {
            return blank && name == "deltaV" && issue.details.value("operand", -1.0) == 0;
        }
        if (issue.details.value("operator", std::string()) != "div" ||
            !node.is_array() || node.size() != 3 || !node[2].is_string()) return false;
        const auto denominator = node[2].get<std::string>();
        if (blank && (name == "acceleration" || name == "range") && denominator == "mass") return true;
        if (blank && name == "deltaV" && denominator == "endMass") return true;
        if (blank && name == "fuelToDryMassRatio" && denominator == "dryMass") return true;
        return engine_less && (name == "burnSeconds" || name == "exhaustVelocity") && denominator == "burnRate";
    }

    std::optional<double> formula(std::string_view name, const Json& variables,
                                  const std::string& output_path, bool allow_pending = false) {
        auto result = evaluate_formula(snapshot, name, variables);
        if (result.ok()) return *result.value;
        Json evaluations = Json::array();
        bool rejected = false;
        const Issue* truncated = nullptr;
        for (auto& issue : result.issues) {
            if (issue.code == "issues_truncated") { truncated = &issue; continue; }
            if (allow_pending && allowed_f20(issue, name)) {
                Json evaluation = {{"formula", name}, {"formulaPath", issue.path}};
                if (issue.details.contains("operator")) evaluation["operator"] = issue.details.at("operator");
                if (issue.details.contains("operand")) evaluation["operand"] = issue.details.at("operand");
                if (issue.details.contains("denominator")) evaluation["denominator"] = issue.details.at("denominator");
                if (rules.at(Json::json_pointer(issue.path)).is_string()) evaluation["dependency"] = "exhaustVelocity";
                evaluations.push_back(std::move(evaluation));
            } else {
                issue.details["outputPath"] = output_path;
                add_issue(issues, std::move(issue));
                rejected = true;
            }
        }
        if (truncated) {
            add_issue(issues, *truncated);
            throw IssueLimitReached{};
        }
        if (rejected || evaluations.empty()) throw FormulaFailure{};
        add_issue(issues, {"pending_F20", "warning", "pending F20: formula output is omitted until Luna defines this case.",
            output_path, {{"finding", "F20"}, {"evaluations", std::move(evaluations)}}});
        return std::nullopt;
    }

    double required(std::string_view name, const Json& variables, const std::string& output_path) {
        return *formula(name, variables, output_path);
    }

    Json power() const {
        return {{"supply", supply}, {"idle", idle}, {"cruise", cruise}, {"jump", jump},
            {"marginIdle", supply - idle}, {"marginCruise", supply - cruise}, {"marginJump", supply - jump}};
    }

    Json scenario(const std::string& name, const Json& fractions, double carried_load, double endurance) {
        const auto path = "/performance/scenarios/" + name;
        const auto cargo_mass = required("cargoMass", {{"cargoUnits", (number("cargo") + number("hidden")) * fractions.at("cargo").get<double>()},
            {"cargoMassPerUnit", policy.at("cargoMassPerUnit")}}, path + "/cargoMass");
        const auto charges = number("fuel") * fractions.at("jumpFuel").get<double>();
        const auto jump_mass = required("jumpFuelMass", {{"jumpFuelCharges", charges},
            {"jumpFuelMassPerCharge", policy.at("jumpFuelMassPerCharge")}}, path + "/jumpFuelMass");
        const auto sublight_mass = number("sublightFuel") * fractions.at("sublightFuel").get<double>();
        const auto mass = round_computed(required("loadedMass", {{"dryMass", totals.at("dryMass")},
            {"cargoMass", cargo_mass}, {"jumpFuelMass", jump_mass}, {"sublightFuelMass", sublight_mass},
            {"carriedLoadMass", carried_load}}, path + "/mass"), snapshot);
        Json result = {{"mass", mass}, {"thrust", thrust}, {"cargoMass", cargo_mass},
            {"jumpFuelMass", jump_mass}, {"sublightFuelMass", sublight_mass}, {"carriedLoadMass", carried_load},
            {"loads", fractions}, {"jumpCharges", charges}, {"endurance", endurance}, {"power", power()}};
        const auto acceleration = formula("acceleration", {{"thrust", thrust}, {"mass", mass}}, path + "/acceleration", true);
        if (acceleration) result["acceleration"] = *acceleration;
        const auto range = formula("range", {{"rating", totals.at("rating")}, {"massReference", policy.at("massReference")},
            {"mass", mass}}, path + "/range", true);
        if (range) result["range"] = *range;
        result["jumpCount"] = required("jumpCount", {{"fuel", charges}, {"fuelPerJump", policy.at("jumpFuelPerJump")}}, path + "/jumpCount");
        return result;
    }

    void check_finite(const Json& value, const std::string& path) {
        if (value.is_number() && !std::isfinite(value.get<double>())) {
            add_issue(issues, error("nonfinite_performance", "A performance result is not finite.", path));
            throw FormulaFailure{};
        }
        if (value.is_structured()) {
            for (const auto& [key, child] : value.items()) check_finite(child, path + "/" + pointer_token(key));
        }
    }

    Json calculate() {
        const auto endurance = required("endurance", {{"baseDays", life_support_count ? policy.at("baseLifeSupportDays") : Json(0)},
            {"suitDays", life_support_count ? policy.at("suitReserveDays") : Json(0)},
            {"reserves", life_support_count ? totals.at("lifeReserve") : Json(0)},
            {"reserveDays", policy.at("reserveLifeSupportDays")}}, "/performance/endurance");
        const auto carried_load = number("carriedCurrentMass") - number("carriedDryMass");
        Json scenarios = Json::object();
        scenarios["current"] = scenario("current", loads, carried_load, endurance);
        for (const auto* name : {"empty", "half", "full"}) {
            const auto fraction = policy.at("scenarioFractions").at(name).get<double>();
            scenarios[name] = scenario(name, {{"cargo", fraction}, {"jumpFuel", fraction}, {"sublightFuel", fraction}},
                carried_load * fraction, endurance);
        }
        const auto current = scenarios.at("current");
        Json sublight = {{"capacity", totals.at("sublightFuel")}, {"current", current.at("sublightFuelMass")}, {"burnRate", burn_rate}};
        const auto full_burn = formula("burnSeconds", {{"fuel", totals.at("sublightFuel")}, {"burnRate", burn_rate}}, "/performance/sublight/fullBurnSeconds", true);
        if (full_burn) sublight["fullBurnSeconds"] = *full_burn;
        const auto current_burn = formula("burnSeconds", {{"fuel", current.at("sublightFuelMass")}, {"burnRate", burn_rate}}, "/performance/sublight/currentBurnSeconds", true);
        if (current_burn) sublight["currentBurnSeconds"] = *current_burn;
        const auto exhaust = formula("exhaustVelocity", {{"thrust", thrust}, {"burnRate", burn_rate}}, "/performance/sublight/exhaustVelocity", true);
        if (exhaust) sublight["exhaustVelocity"] = *exhaust;
        Json delta_variables = {{"startMass", current.at("mass")},
            {"endMass", current.at("mass").get<double>() - current.at("sublightFuelMass").get<double>()}};
        if (exhaust) delta_variables["exhaustVelocity"] = *exhaust;
        const auto delta_v = formula("deltaV", delta_variables, "/performance/sublight/deltaV", true);
        if (delta_v) sublight["deltaV"] = *delta_v;
        const auto fuel_mass = required("fuelCapacityMass", {{"sublightCapacity", totals.at("sublightFuel")},
            {"jumpCapacity", totals.at("fuel")}, {"jumpFuelMassPerCharge", policy.at("jumpFuelMassPerCharge")}}, "/performance/fuelCapacityMass");
        const auto fuel_ratio = formula("fuelToDryMassRatio", {{"fuelMass", fuel_mass}, {"dryMass", totals.at("dryMass")}}, "/performance/fuelToDryMassRatio", true);
        Json result = {{"dryMass", totals.at("dryMass")}, {"currentMass", current.at("mass")}, {"thrust", thrust},
            {"rating", totals.at("rating")}, {"scenarios", std::move(scenarios)}, {"sublight", std::move(sublight)},
            {"endurance", endurance}, {"fuelCapacityMass", fuel_mass}, {"power", power()}, {"loads", loads},
            {"jump", {{"capacity", totals.at("fuel")}, {"current", current.at("jumpCharges")},
                {"massPerCharge", policy.at("jumpFuelMassPerCharge")}, {"fuelPerJump", policy.at("jumpFuelPerJump")},
                {"maxRangeJumps", required("jumpCount", {{"fuel", totals.at("fuel")}, {"fuelPerJump", policy.at("jumpFuelPerJump")}}, "/performance/jump/maxRangeJumps")},
                {"currentJumps", current.at("jumpCount")}, {"driveCosts", drives}, {"driveCost", drive_cost}}}};
        if (fuel_ratio) result["fuelToDryMassRatio"] = *fuel_ratio;
        for (const auto* key : {"acceleration", "range", "cargoMass", "jumpFuelMass", "sublightFuelMass", "carriedLoadMass"}) {
            if (current.contains(key)) result[key] = current.at(key);
        }
        if (current.contains("acceleration")) {
            double best = -std::numeric_limits<double>::infinity();
            for (const auto& band : policy.at("accelerationBands")) {
                const auto minimum = band.at("min").get<double>();
                if (minimum <= current.at("acceleration").get<double>() && minimum >= best) {
                    result["accelerationRating"] = band.at("label"); best = minimum;
                }
            }
        }
        const bool ship = blueprint.at("buildType") == "ship";
        const bool weak = current.contains("acceleration") && near_less(current.at("acceleration"), policy.at("underpoweredAcceleration"));
        if (current.contains("acceleration")) {
            result["underpowered"] = ship && (weak || near_less(supply, idle) || near_less(supply, cruise) || near_less(supply, jump));
        }
        for (const auto& [phase, demand] : std::map<std::string, double>{{"idle", idle}, {"cruise", cruise}, {"jump", jump}}) {
            if (near_less(supply, demand)) advisory("power_shortfall", "Demand exceeds the active rules' available power supply.",
                "/performance/power/" + phase, {{"phase", phase}, {"supply", supply}, {"demand", demand}});
        }
        if (ship && weak) advisory("weak_acceleration", "Current acceleration is below the active rules' advisory threshold.",
            "/performance/acceleration", {{"acceleration", current.at("acceleration")}, {"threshold", policy.at("underpoweredAcceleration")}});
        if (engine_count && current.at("sublightFuelMass").get<double>() <= 0) advisory("no_sublight_fuel", "Engines are fitted without loaded sublight propellant.", "/performance/sublight/current");
        if (number("rating") > 0 && near_less(current.at("jumpCharges"), policy.at("jumpFuelPerJump"))) advisory("no_jump_fuel", "A jump drive is fitted with fewer than one jump's fuel currently loaded.", "/performance/jump/current");
        if (number("shield") > policy.at("advisoryShieldCount").get<double>() || number("weapon") > policy.at("advisoryWeaponCount").get<double>()) advisory("excess_defence", "Defence equipment exceeds the active rules' advisory counts.", "/performance");
        for (const auto& [code, rule] : rules.at("advisories").items()) {
            const auto& types = rule.at("buildTypes");
            if (std::find(types.begin(), types.end(), blueprint.at("buildType")) == types.end()) continue;
            const bool applies = (code == "many_engines" && engine_count >= rule.at("minEngineCount").get<double>()) ||
                (code == "large_fuel_capacity" && fuel_ratio && !near_less(*fuel_ratio, rule.at("minFuelToDryMassRatio"))) ||
                (code == "no_cargo" && module_count >= rule.at("minModuleCount").get<double>() &&
                    number("cargo") + number("hidden") <= rule.at("maxCargoCapacity").get<double>());
            if (applies) advisory(code, rule.at("text"), "/performance");
        }
        check_finite(result, "/performance");
        return result;
    }
};
} // namespace

Result<Json> calculate_performance(const Json& definition, const RulesSnapshot& rules,
                                   const SchemaSet& schemas, const Json& loads, InputLimits limits) {
    Calculator calculator(definition, rules, loads);
    try {
        auto performance = calculator.calculate();
        Json carried = Json::array();
        bool rejected_child = false;
        const auto& decks = definition.at("blueprint").at("decks");
        for (std::size_t d = 0; d < decks.size(); ++d) {
            const auto& modules = decks[d].at("modules");
            for (std::size_t m = 0; m < modules.size(); ++m) {
                const auto& module = modules[m];
                if (!module.contains("craft") || module.at("craft").is_null()) continue;
                const auto child_path = "/blueprint/decks/" + std::to_string(d) + "/modules/" + std::to_string(m) + "/craft";
                const auto output_path = "/performance/carriedCraft/" + std::to_string(carried.size()) + "/performance";
                auto imported = import_definition(canonical_json(module.at("craft")), rules, schemas, limits);
                const auto append = [&](std::vector<Issue> child_issues) {
                    const Issue* truncated = nullptr;
                    for (auto& issue : child_issues) {
                        if (issue.code == "issues_truncated") { truncated = &issue; continue; }
                        if (issue.code == "pending_validation" || issue.code == "pending_performance" ||
                            issue.code == "pending_docking") continue;
                        if (issue.path.starts_with("/performance")) {
                            issue.path = output_path + issue.path.substr(std::string("/performance").size());
                        } else if (issue.path.starts_with("/blueprint")) {
                            issue.path = child_path + issue.path.substr(std::string("/blueprint").size());
                        }
                        if (issue.details.contains("outputPath")) {
                            auto path = issue.details.at("outputPath").get<std::string>();
                            if (path.starts_with("/performance")) issue.details["outputPath"] = output_path + path.substr(std::string("/performance").size());
                        }
                        if (issue.details.contains("childBlueprintPath")) {
                            const auto nested = issue.details.at("childBlueprintPath").get<std::string>();
                            issue.details["childBlueprintPath"] = child_path + nested.substr(std::string("/blueprint").size());
                        } else issue.details["childBlueprintPath"] = child_path;
                        add_issue(calculator.issues, std::move(issue));
                    }
                    if (truncated) {
                        add_issue(calculator.issues, *truncated);
                        throw IssueLimitReached{};
                    }
                };
                append(std::move(imported.issues));
                if (!imported.ok()) { rejected_child = true; continue; }
                const auto& child = imported.value->definition;
                auto calculated = calculate_performance(child, rules, schemas, child.at("blueprint").at("loads"), limits);
                append(std::move(calculated.issues));
                if (!calculated.ok()) { rejected_child = true; continue; }
                carried.push_back({{"deck", d}, {"moduleId", module.at("id")},
                    {"name", module.at("craft").at("name")}, {"performance", std::move(*calculated.value)}});
            }
        }
        performance["carriedCraft"] = std::move(carried);
        sort_issues(calculator.issues);
        if (rejected_child) return {std::nullopt, std::move(calculator.issues)};
        return {std::move(performance), std::move(calculator.issues)};
    } catch (const FormulaFailure&) {
        sort_issues(calculator.issues);
        return {std::nullopt, std::move(calculator.issues)};
    } catch (const IssueLimitReached&) {
        // Stop scenarios and carried-craft traversal, including warning-only paths.
    } catch (const std::invalid_argument&) {
        try {
            add_issue(calculator.issues, error("nonfinite_performance", "A performance mass cannot be rounded to a finite value.", "/performance"));
        } catch (const IssueLimitReached&) {}
    } catch (const std::overflow_error&) {
        try {
            add_issue(calculator.issues, error("nonfinite_performance", "A performance mass exceeds the numeric rounding range.", "/performance"));
        } catch (const IssueLimitReached&) {}
    }
    sort_issues(calculator.issues);
    return {std::nullopt, std::move(calculator.issues)};
}
} // namespace apsis::shipyard::detail
