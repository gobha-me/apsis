#include "internal.hpp"

#include <openssl/evp.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace apsis::shipyard {
namespace {
using Signature = std::set<std::string>;
const std::map<std::string, Signature, std::less<>> signatures{
    {"structureMass", {"floorCount", "floorMass"}},
    {"dryMass", {"structureMass", "componentMass"}},
    {"cargoMass", {"cargoUnits", "cargoMassPerUnit"}},
    {"jumpFuelMass", {"jumpFuelCharges", "jumpFuelMassPerCharge"}},
    {"loadedMass", {"dryMass", "cargoMass", "jumpFuelMass", "sublightFuelMass", "carriedLoadMass"}},
    {"acceleration", {"thrust", "mass"}}, {"range", {"rating", "massReference", "mass"}},
    {"burnSeconds", {"fuel", "burnRate"}}, {"exhaustVelocity", {"thrust", "burnRate"}},
    {"deltaV", {"exhaustVelocity", "startMass", "endMass"}},
    {"jumpCount", {"fuel", "fuelPerJump"}},
    {"endurance", {"baseDays", "suitDays", "reserves", "reserveDays"}},
    {"weaponDps", {"damage", "cooldown"}},
    {"fuelCapacityMass", {"sublightCapacity", "jumpCapacity", "jumpFuelMassPerCharge"}},
    {"fuelToDryMassRatio", {"fuelMass", "dryMass"}}
};
const std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> arities{
    {"add", {2, 64}}, {"mul", {2, 64}}, {"sub", {2, 2}}, {"div", {2, 2}},
    {"floor", {1, 1}}, {"ln", {1, 1}}, {"min", {1, 64}}, {"max", {1, 64}}
};
void check_formula(const Json& node, const Signature& variables, const std::string& path,
                   std::vector<Issue>& issues, std::size_t depth = 0) {
    auto fail = [&](std::string message) { detail::add_issue(issues, detail::error("invalid_formula", std::move(message), path)); };
    if (depth > 64) { fail("Formula exceeds the operational nesting limit."); return; }
    if (node.is_number()) {
        if (!std::isfinite(node.get<double>())) fail("Formula constant must be finite.");
        return;
    }
    if (node.is_string()) {
        if (!variables.contains(node.get<std::string>())) fail("Unknown variable for this formula.");
        return;
    }
    if (!node.is_array() || node.empty() || !node[0].is_string()) {
        fail("Formula must be a constant, permitted variable, or operator array."); return;
    }
    const auto op = arities.find(node[0].get<std::string>());
    if (op == arities.end()) { fail("Unknown formula operator."); return; }
    const auto operands = node.size() - 1;
    if (operands < op->second.first || operands > op->second.second) {
        fail("Invalid formula operator arity (variadic operational cap is 64 operands)."); return;
    }
    for (std::size_t i = 1; i < node.size(); ++i)
        check_formula(node[i], variables, path + "/" + std::to_string(i), issues, depth + 1);
}
std::optional<double> evaluate(const Json& node, const Json& vars, const std::string& path,
                               std::vector<Issue>& issues) {
    auto fail = [&](std::string message, Json details = Json::object()) -> std::optional<double> {
        auto issue = detail::error("formula_error", std::move(message), path);
        issue.details = std::move(details);
        detail::add_issue(issues, std::move(issue));
        return std::nullopt;
    };
    if (node.is_number()) return node.get<double>();
    if (node.is_string()) {
        const auto name = node.get<std::string>();
        if (!vars.contains(name) || !vars[name].is_number() || !std::isfinite(vars[name].get<double>()))
            return fail("Formula variable is missing or not a finite number.", {{"variable", name}});
        return vars[name].get<double>();
    }
    std::vector<double> values;
    bool complete = true;
    // Visit independent operands even after one fails. In particular an F20
    // exclusion must never conceal an unrelated invalid sibling expression.
    for (std::size_t i = 1; i < node.size(); ++i) {
        const auto value = evaluate(node[i], vars, path + "/" + std::to_string(i), issues);
        if (value) values.push_back(*value);
        else complete = false;
    }
    if (!complete) return std::nullopt;
    const auto op = node[0].get<std::string>();
    double result = 0;
    if (op == "add" || op == "mul") {
        result = op == "mul" ? 1 : 0;
        for (const auto value : values) {
            result = op == "mul" ? result * value : result + value;
            if (!std::isfinite(result)) return fail("Formula intermediate result is not finite.");
        }
    } else if (op == "sub") result = values[0] - values[1];
    else if (op == "div") {
        if (values[1] == 0) return fail("Formula divides by zero.", {{"operator", "div"}, {"denominator", values[1]}});
        result = values[0] / values[1];
    } else if (op == "floor") result = std::floor(values[0]);
    else if (op == "ln") {
        if (values[0] <= 0) return fail("Formula logarithm requires a positive operand.", {{"operator", "ln"}, {"operand", values[0]}});
        result = std::log(values[0]);
    } else if (op == "min") result = *std::min_element(values.begin(), values.end());
    else if (op == "max") result = *std::max_element(values.begin(), values.end());
    else return fail("Unknown formula operator.");
    if (!std::isfinite(result)) return fail("Formula result is not finite.");
    return result;
}
std::string sha256(std::string_view bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr) != 1 || length != 32)
        throw detail::error("rules_hash", "Cannot compute the rules SHA-256.");
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned int i = 0; i < length; ++i) {
        result += hex[digest[i] >> 4];
        result += hex[digest[i] & 15];
    }
    return result;
}
void check_semantics(const Json& r, std::vector<Issue>& issues) {
    auto fail = [&](const std::string& path, std::string message) {
        detail::add_issue(issues, detail::error("invalid_rules", std::move(message), path));
    };
    constexpr double safeInteger = 9007199254740991.0;
    // Bounds on integer storage are representation limits, not game limits.
    for (const auto section : {"grid", "limits", "technicalLimits"})
        for (const auto& [key, value] : r[section].items())
            if (value.get<double>() > safeInteger)
                fail("/" + std::string(section) + "/" + key, "Rule exceeds the exact interoperable integer range.");
    if (r["limits"]["maxModuleId"].get<double>() >= safeInteger)
        fail("/limits/maxModuleId", "Module IDs must leave one exact integer for nextId.");
    if (r["grid"]["w"].get<double>() > safeInteger / r["grid"]["h"].get<double>())
        fail("/grid", "Grid capacity exceeds the exact interoperable integer range.");
    if (r["numeric"]["precision"].get<double>() > 15)
        fail("/numeric/precision", "Binary64 rounding supports precision from 0 through 15.");
    if (r["limits"]["minDecks"] > r["limits"]["maxDecks"])
        fail("/limits/minDecks", "Minimum deck count exceeds maximum deck count.");
    for (const auto key : {"floorMass", "floorCost", "cellSizeM"})
        if (r[key] != r["floor"][std::string_view(key) == "floorMass" ? "mass" :
                                  std::string_view(key) == "floorCost" ? "cost" : "cellSizeM"])
            fail("/" + std::string(key), "Duplicated floor metadata disagrees with the authoritative floor record.");
    if (r["massReference"] != r["performance"]["massReference"])
        fail("/massReference", "Mass-reference metadata disagrees with performance rules.");
    if (r["floor"]["w"] != 1 || r["floor"]["h"] != 1)
        fail("/floor", "Floor records must describe one grid cell in the floor-map model.");
    if (r["cellSizeM"].get<double>() <= 0)
        fail("/cellSizeM", "Cell size must be positive.");
    if (r["massReference"].get<double>() <= 0)
        fail("/massReference", "Mass reference must be positive.");
    if (r["performance"]["jumpFuelPerJump"].get<double>() <= 0)
        fail("/performance/jumpFuelPerJump", "Jump fuel per jump must be positive.");
    auto unique_strings = [&](const Json& array, const std::string& path) {
        std::set<std::string> seen;
        for (std::size_t i = 0; i < array.size(); ++i)
            if (!seen.insert(array[i].get<std::string>()).second)
                fail(path + "/" + std::to_string(i), "Duplicate rules identifier.");
        return seen;
    };
    const auto types = unique_strings(r["buildTypes"], "/buildTypes");
    if (types != std::set<std::string>{"ship", "station"})
        fail("/buildTypes", "This schema supports ship and station build types.");
    const auto interfaces = unique_strings(r["docking"]["interfaces"], "/docking/interfaces");
    unique_strings(r["docking"]["targetKinds"], "/docking/targetKinds");
    if (!interfaces.contains(r["docking"]["defaultInterface"].get<std::string>()))
        fail("/docking/defaultInterface", "Default docking interface is not declared.");
    unique_strings(r["schemaIds"]["acceptedBlueprints"], "/schemaIds/acceptedBlueprints");
    unique_strings(r["schemaIds"]["acceptedDefinitions"], "/schemaIds/acceptedDefinitions");
    for (const auto& [key, expected] : std::map<std::string, Json>{
             {"acceptedBlueprints", Json::array({"apsis-shipyard-v3", "apsis-shipyard-v4"})},
             {"acceptedDefinitions", Json::array({"apsis-ship-definition-v3", "apsis-ship-definition-v4"})}})
        for (const auto& id : expected)
            if (std::find(r["schemaIds"][key].begin(), r["schemaIds"][key].end(), id) == r["schemaIds"][key].end())
                fail("/schemaIds/" + key, "The migration contract requires both v3 and v4 identifiers.");
    if (!types.contains(r["defaults"]["buildType"].get<std::string>()))
        fail("/defaults/buildType", "Default build type is not declared.");
    if (r["defaults"]["budget"] > r["technicalLimits"]["maxBudgetInputCr"])
        fail("/defaults/budget", "Default budget exceeds the technical input sanity limit.");
    for (const auto& [key, value] : r["defaults"]["loads"].items())
        if (value.get<double>() > 1) fail("/defaults/loads/" + key, "Load fractions must be within zero and one.");
    for (const auto& [key, value] : r["performance"]["scenarioFractions"].items())
        if (value.get<double>() > 1) fail("/performance/scenarioFractions/" + key, "Scenario fractions must be within zero and one.");
    for (const auto key : {"shipName", "stationName", "deckName"}) {
        const auto max = key == std::string_view("deckName") ? r["limits"]["maxDeckName"].get<double>()
                            : r["technicalLimits"]["maxBlueprintNameCharacters"].get<double>();
        const auto name = key == std::string_view("deckName")
            ? r["defaults"][key].get<std::string>()
            : detail::trim_design_name(r["defaults"][key].get<std::string>());
        if (detail::character_count(name) > max)
            fail("/defaults/" + std::string(key), "Default name exceeds its rules limit.");
        if (key != std::string_view("deckName") && name.empty())
            fail("/defaults/" + std::string(key), "Default design names cannot be blank after trimming.");
        const auto& text = r["defaults"][key].get_ref<const std::string&>();
        if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            fail("/defaults/" + std::string(key), "Default names cannot contain control characters.");
    }
    // A literal prefix plus decimal level is the entire naming configuration.
    // Older v4 snapshots fall back to their existing deck label and one space.
    const bool has_deck_prefix = r["defaults"].contains("deckNamePrefix");
    const auto deck_prefix = has_deck_prefix
        ? r["defaults"]["deckNamePrefix"].get<std::string>()
        : r["defaults"]["deckName"].get<std::string>() + " ";
    const auto prefix_path = has_deck_prefix ? "/defaults/deckNamePrefix" : "/defaults/deckName";
    const auto longest_level = std::to_string(r["limits"]["maxDecks"].get<std::size_t>());
    if (detail::character_count(deck_prefix) + longest_level.size() > r["limits"]["maxDeckName"].get<std::size_t>())
        fail(prefix_path, "Generated deck names exceed the active deck-name character limit.");
    if (std::any_of(deck_prefix.begin(), deck_prefix.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        fail(prefix_path, "Deck-name prefixes cannot contain control characters.");
    std::set<std::pair<double, double>> directions;
    for (std::size_t i = 0; i < r["rotations"].size(); ++i) {
        const auto& rotation = r["rotations"][i];
        const double x = rotation["dx"], y = rotation["dy"];
        if (std::abs(x) + std::abs(y) != 1 || !directions.emplace(x, y).second)
            fail("/rotations/" + std::to_string(i), "Rotation directions must be distinct cardinal unit vectors.");
    }
    std::set<std::string> ids, systems, carrierIds;
    for (std::size_t i = 0; i < r["parts"].size(); ++i) {
        const auto& part = r["parts"][i];
        const auto path = "/parts/" + std::to_string(i);
        if (!ids.insert(part["id"].get<std::string>()).second) fail(path + "/id", "Duplicate part ID.");
        if (part.contains("system")) systems.insert(part["system"].get<std::string>());
        if (part.contains("carrier")) carrierIds.insert(part["id"].get<std::string>());
        for (const auto axis : {"w", "h"})
            if (part[axis] > r["grid"][axis]) fail(path + "/" + axis, "Part dimensions exceed the authoring grid.");
        if (part.contains("weapon") && part["weapon"]["cooldown"].get<double>() <= 0)
            fail(path + "/weapon/cooldown", "Weapon cooldown must be positive.");
        if (part.contains("docking")) {
            if (!interfaces.contains(part["docking"]["interface"].get<std::string>()))
                fail(path + "/docking/interface", "Part docking interface is not declared.");
            if (part["docking"]["kind"] != "airlock" &&
                std::find(r["docking"]["targetKinds"].begin(), r["docking"]["targetKinds"].end(),
                          part["docking"]["kind"]) == r["docking"]["targetKinds"].end())
                fail(path + "/docking/kind", "Part docking target kind is not declared.");
        }
    }
    if (!carrierIds.contains(r["carrier"]["defaultPart"].get<std::string>()))
        fail("/carrier/defaultPart", "Default carrier part is missing or lacks carrier metadata.");
    auto check_types = [&](const Json& values, const std::string& path) {
        unique_strings(values, path);
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!types.contains(values[i].get<std::string>()))
                fail(path + "/" + std::to_string(i), "Build type reference is not declared.");
    };
    check_types(r["carrier"]["allowedBuildTypes"], "/carrier/allowedBuildTypes");
    for (const auto& [key, value] : r["advisories"].items())
        check_types(value["buildTypes"], "/advisories/" + key + "/buildTypes");
    auto check_systems = [&](const Json& values, const std::string& path) {
        unique_strings(values, path);
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!systems.contains(values[i].get<std::string>()))
                fail(path + "/" + std::to_string(i), "System reference has no catalogue part.");
    };
    for (const auto& [key, values] : r["validation"]["requiredSystems"].items())
        check_systems(values, "/validation/requiredSystems/" + key);
    check_systems(r["validation"]["uniqueSystems"], "/validation/uniqueSystems");
}
} // namespace

Result<RulesSnapshot> load_rules(std::string_view bytes, const SchemaSet& schemas, InputLimits limits) {
    auto parsed = parse_json(bytes, limits);
    if (!parsed.ok()) return {std::nullopt, std::move(parsed.issues)};
    std::vector<Issue> issues;
    try {
        const auto& r = *parsed.value;
        // Enforce formula bounds before schema traversal, regardless of the parser's
        // nesting allowance. Missing names and invalid container shapes stay with
        // the schema; check_formula safely validates each present expression.
        if (r.is_object() && r.contains("formulas") && r["formulas"].is_object())
            for (const auto& [name, vars] : signatures)
                if (r["formulas"].contains(name))
                    check_formula(r["formulas"][name], vars, "/formulas/" + name, issues);
        sort_issues(issues);
        if (!issues.empty()) return {std::nullopt, std::move(issues)};
        issues = schemas.validate(r, "apsis-ship-rules-v4");
        if (!issues.empty()) return {std::nullopt, std::move(issues)};
        check_semantics(*parsed.value, issues);
        sort_issues(issues);
        if (!issues.empty()) return {std::nullopt, std::move(issues)};
        try {
            RulesSnapshot snapshot;
            snapshot.sha256_ = sha256(bytes);
            snapshot.data_ = std::make_shared<const Json>(std::move(*parsed.value));
            return {std::move(snapshot), {}};
        } catch (const Issue& issue) { return {std::nullopt, {issue}}; }
    } catch (const detail::IssueLimitReached&) {
        sort_issues(issues);
        return {std::nullopt, std::move(issues)};
    }
}
namespace detail {
Result<double> evaluate_formula(const RulesSnapshot& rules, std::string_view name, const Json& variables) {
    const auto path = "/formulas/" + std::string(name);
    if (!rules.data().at("formulas").contains(name))
        return {std::nullopt, {error("invalid_formula", "Undefined formula.", path)}};
    std::vector<Issue> issues;
    std::optional<double> value;
    try {
        value = evaluate(rules.data().at("formulas").at(std::string(name)), variables, path, issues);
    } catch (const IssueLimitReached&) {
        // The shared marker already records the omitted formula diagnostics.
    }
    sort_issues(issues);
    return {value, std::move(issues)};
}
} // namespace detail
bool numerically_equal(double left, double right, const RulesSnapshot& rules) {
    return std::isfinite(left) && std::isfinite(right) &&
           std::abs(left - right) <= rules.data()["numeric"]["epsilon"].get<double>();
}
double round_computed(double value, const RulesSnapshot& rules) {
    if (!std::isfinite(value)) throw std::invalid_argument("Computed number must be finite.");
    const double factor = std::pow(10.0, rules.data()["numeric"]["precision"].get<int>());
    const double scaled = value * factor;
    if (!std::isfinite(scaled)) throw std::overflow_error("Computed number exceeds the rounding range.");
    // Same tie direction as the reference's Math.round; avoid adding 0.5
    // at large magnitudes, where it could alter an already integral value.
    const double lower = std::floor(scaled);
    const double rounded = (scaled - lower >= 0.5 ? lower + 1 : lower) / factor;
    return rounded == 0 ? 0.0 : rounded;
}
} // namespace apsis::shipyard
