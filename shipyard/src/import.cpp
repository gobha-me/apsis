#include "internal.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>

namespace apsis::shipyard {
namespace {
using detail::error;
using detail::pointer_token;

bool includes(const Json& values, const Json& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string at(std::string_view path, std::string_view token) {
    return std::string(path) + "/" + pointer_token(token);
}

bool has_errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.severity == "error";
    });
}

struct FormulaFailure {};

int source_version(std::string_view schema) {
    if (schema == "apsis-shipyard-v4" || schema == "apsis-ship-definition-v4") return 4;
    if (schema == "apsis-shipyard-v3" || schema == "apsis-ship-definition-v3") return 3;
    if (schema == "apsis-shipyard-v2" || schema == "apsis-ship-definition-v2") return 2;
    if (schema == "apsis-shipyard-v1" || schema == "apsis-ship-definition-v1") return 1;
    return 0;
}

struct Importer {
    const RulesSnapshot& snapshot;
    const Json& rules;
    std::vector<Issue> issues;
    MigrationReport report;
    std::map<std::string, const Json*, std::less<>> parts;

    explicit Importer(const RulesSnapshot& active) : snapshot(active), rules(active.data()) {
        for (const auto& part : rules.at("parts")) {
            parts.emplace(part.at("id").get<std::string>(), &part);
        }
    }

    void change(std::string text) {
        if (std::find(report.changes.begin(), report.changes.end(), text) == report.changes.end()) {
            report.changes.push_back(std::move(text));
        }
    }

    void check_text(const Json& value, const Json& maximum, const std::string& path) {
        const auto& text = value.get_ref<const std::string&>();
        if (detail::character_count(text) > maximum.get<std::size_t>()) {
            detail::add_issue(issues, error("field_limit", "Text exceeds the active rules character limit.", path));
        }
        if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
            detail::add_issue(issues, error("invalid_text", "Control characters are not allowed in labels or notes.", path));
        }
    }

    void add_note(Json& blueprint, const std::string& note, const std::string& path) {
        auto& notes = blueprint.at("migrationNotes");
        if (includes(notes, note)) return;
        if (detail::character_count(note) <= rules.at("limits").at("maxNoteLength").get<std::size_t>()) {
            notes.push_back(note);
            return;
        }
        detail::add_issue(issues, {"migration_note_limit", "warning",
                          "A generated migration note exceeds the active text limit; changes remain in the migration report.",
                          at(path, "migrationNotes"), Json::object()});
    }

    static bool drop_notice(const Json& note) {
        return note == "Older migration notes were dropped." || note == "\xe2\x80\xa6";
    }

    void retain_notes(Json& blueprint, const std::string& path) {
        auto& notes = blueprint.at("migrationNotes");
        const auto maximum = rules.at("limits").at("maxMigrationNotes").get<std::size_t>();
        const auto markers = static_cast<std::size_t>(std::count_if(notes.begin(), notes.end(), drop_notice));
        const auto real_count = notes.size() - markers;
        if (markers != 0 || real_count > maximum) {
            // Rules require a positive cap: reserve one bounded slot for history.
            const auto keep = std::min(real_count, maximum - 1);
            Json retained = Json::array();
            for (auto it = notes.rbegin(); it != notes.rend() && retained.size() < keep; ++it) {
                if (!drop_notice(*it)) retained.push_back(*it);
            }
            std::reverse(retained.begin(), retained.end());
            constexpr std::string_view full_notice = "Older migration notes were dropped.";
            const auto marker = detail::character_count(full_notice) <=
                rules.at("limits").at("maxNoteLength").get<std::size_t>() ? full_notice : "\xe2\x80\xa6";
            retained.insert(retained.begin(), Json(std::string(marker)));
            if (retained != notes) {
                const auto dropped = real_count - keep;
                notes = std::move(retained);
                change(at(path, "migrationNotes") + ": retained newest notes and one dropped-history notice");
                detail::add_issue(issues, {"migration_note_limit", "warning",
                    "Older migration notes were dropped; the newest notes and one notice were retained.",
                    at(path, "migrationNotes"), {{"limit", maximum}, {"dropped", dropped}, {"retained", keep}}});
            }
        }
        for (auto& [deck_index, deck] : blueprint.at("decks").items()) {
            for (auto& [module_index, module] : deck.at("modules").items()) {
                if (module.contains("craft") && !module.at("craft").is_null()) {
                    const auto child = at(at(at(at(at(path, "decks"), deck_index), "modules"), module_index), "craft");
                    retain_notes(module.at("craft"), child);
                }
            }
        }
    }

    // Normalize only schema/default metadata, never geometry, labels or IDs.
    void normalize(Json& blueprint, std::size_t depth, const std::string& path, int version) {
        if (depth > rules.at("limits").at("maxCarryDepth").get<std::size_t>()) {
            detail::add_issue(issues, error("carry_depth", "Carried craft nesting exceeds the active rules limit.", path));
            return;
        }
        if (version == 3) {
            blueprint["schema"] = rules.at("schemaIds").at("blueprint");
            change(path + "/schema: apsis-shipyard-v3 -> apsis-shipyard-v4");
            if (!blueprint.contains("buildType")) {
                blueprint["buildType"] = rules.at("defaults").at("buildType");
                change(path + "/buildType: supplied the active rules default");
            }
            if (!blueprint.contains("loads")) {
                blueprint["loads"] = Json::object();
                change(path + "/loads: supplied the active rules defaults");
            }
            for (auto it = rules.at("defaults").at("loads").begin();
                 it != rules.at("defaults").at("loads").end(); ++it) {
                if (!blueprint.at("loads").contains(it.key())) {
                    blueprint["loads"][it.key()] = it.value();
                    change(path + "/loads/" + it.key() + ": supplied the active rules default");
                }
            }
            if (!blueprint.contains("migrationNotes")) {
                blueprint["migrationNotes"] = Json::array();
                change(path + "/migrationNotes: supplied an empty note list");
            }
        }
        const auto& limits = rules.at("limits");
        check_text(blueprint.at("name"), rules.at("technicalLimits").at("maxBlueprintNameCharacters"),
                   at(path, "name"));
        if (blueprint.at("budget").get<double>() > rules.at("technicalLimits").at("maxBudgetInputCr").get<double>()) {
            detail::add_issue(issues, error("field_limit", "Budget input exceeds the active technical sanity limit.", at(path, "budget")));
        }
        if (!includes(rules.at("buildTypes"), blueprint.at("buildType"))) {
            detail::add_issue(issues, error("invalid_build_type", "Build type is not defined by the active rules.", at(path, "buildType")));
        }
        for (auto it = blueprint.at("loads").begin(); it != blueprint.at("loads").end(); ++it) {
            if (!rules.at("defaults").at("loads").contains(it.key()) || it.value().get<double>() < 0 ||
                it.value().get<double>() > 1) {
                detail::add_issue(issues, error("invalid_load", "Load fractions must be between zero and one.", at(at(path, "loads"), it.key())));
            }
        }
        auto& notes = blueprint.at("migrationNotes");
        // Validate ordinary input notes before retention, including older notes
        // that will be discarded. Reserved notices canonicalize to the active cap.
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (!drop_notice(notes[i]))
                check_text(notes[i], limits.at("maxNoteLength"), at(at(path, "migrationNotes"), std::to_string(i)));
        }
        const auto deck_count = blueprint.at("decks").size();
        if (deck_count < limits.at("minDecks").get<std::size_t>() ||
            deck_count > limits.at("maxDecks").get<std::size_t>()) {
            detail::add_issue(issues, error("deck_limit", "Deck count exceeds the active rules limits.", at(path, "decks")));
        }
        const auto width = rules.at("grid").at("w").get<std::uint64_t>();
        const auto height = rules.at("grid").at("h").get<std::uint64_t>();
        const auto max_id = limits.at("maxModuleId").get<std::uint64_t>();
        std::set<std::uint64_t> ids;
        std::uint64_t greatest_id = 0;
        for (std::size_t d = 0; d < deck_count; ++d) {
            auto& deck = blueprint["decks"][d];
            const auto deck_path = at(at(path, "decks"), std::to_string(d));
            check_text(deck.at("name"), limits.at("maxDeckName"), at(deck_path, "name"));
            if (deck.at("floors").size() > width * height || deck.at("modules").size() > width * height) {
                detail::add_issue(issues, error("grid_capacity", "Floor or module count exceeds the active grid capacity.", deck_path));
            }
            for (auto it = deck.at("floors").begin(); it != deck.at("floors").end(); ++it) {
                const auto comma = it.key().find(',');
                std::uint64_t x = 0, y = 0;
                const auto* first = it.key().data();
                const auto* last = first + it.key().size();
                const auto rx = std::from_chars(first, first + comma, x);
                const auto ry = std::from_chars(first + comma + 1, last, y);
                if (rx.ec != std::errc{} || ry.ec != std::errc{} || rx.ptr != first + comma ||
                    ry.ptr != last || x >= width || y >= height) {
                    detail::add_issue(issues, error("invalid_coordinate", "Floor coordinate is outside the active build grid.",
                                           at(at(deck_path, "floors"), it.key())));
                }
            }
            for (std::size_t m = 0; m < deck.at("modules").size(); ++m) {
                auto& module = deck["modules"][m];
                const auto module_path = at(at(deck_path, "modules"), std::to_string(m));
                const bool id_in_range = module.at("id").get<double>() <= static_cast<double>(max_id);
                const auto id = id_in_range ? module.at("id").get<std::uint64_t>() : 0;
                if (!id_in_range || !ids.insert(id).second) {
                    detail::add_issue(issues, error("invalid_id", "Module ID is duplicate or exceeds the active rules limit.", at(module_path, "id")));
                }
                greatest_id = std::max(greatest_id, id);
                for (const auto* axis : {"x", "y"}) {
                    if (module.at(axis).get<double>() >= static_cast<double>(axis[0] == 'x' ? width : height)) {
                        detail::add_issue(issues, error("invalid_coordinate", "Module coordinate is outside the active build grid.", at(module_path, axis)));
                    }
                }
                if (module.at("r").get<double>() >= static_cast<double>(rules.at("rotations").size())) {
                    detail::add_issue(issues, error("invalid_rotation", "Module rotation is not defined by the active rules.", at(module_path, "r")));
                }
                const auto part = parts.find(module.at("type").get<std::string>());
                if (part == parts.end()) {
                    detail::add_issue(issues, error("invalid_part_type", "Module type is not defined by the active rules.", at(module_path, "type")));
                    continue;
                }
                if (module.contains("interface")) {
                    if (!part->second->contains("docking") ||
                        !includes(rules.at("docking").at("interfaces"), module.at("interface"))) {
                        detail::add_issue(issues, error("invalid_interface", "Module has no compatible interface in the active rules.", at(module_path, "interface")));
                    }
                }
                if (module.contains("craft")) {
                    if (!part->second->contains("carrier")) {
                        detail::add_issue(issues, error("invalid_carrier", "Only a carrier part can contain a craft field.", at(module_path, "craft")));
                    } else if (!module.at("craft").is_null()) {
                        if (depth >= limits.at("maxCarryDepth").get<std::size_t>()) {
                            detail::add_issue(issues, error("carry_depth", "Carried craft nesting exceeds the active rules limit.", at(module_path, "craft")));
                            continue;
                        }
                        normalize(module["craft"], depth + 1, at(module_path, "craft"), version);
                        if (!includes(rules.at("carrier").at("allowedBuildTypes"), module.at("craft").at("buildType"))) {
                            detail::add_issue(issues, error("invalid_carried_build_type", "Carried build type is not allowed by the active rules.", at(at(module_path, "craft"), "buildType")));
                        }
                    }
                }
            }
        }
        const auto next_id = blueprint.at("nextId").get<double>();
        if (next_id > static_cast<double>(max_id + 1) || next_id <= static_cast<double>(greatest_id)) {
            detail::add_issue(issues, error("invalid_next_id", "Next module ID must exceed all existing IDs and stay within the active rules limit.", at(path, "nextId")));
        }
        if (version == 3) {
            add_note(blueprint, "Migrated apsis-shipyard-v3 to apsis-shipyard-v4 with active rules; existing geometry, IDs, names, budget and loads retained.", path);
        }
    }

    double formula(std::string_view name, const Json& variables) {
        auto result = detail::evaluate_formula(snapshot, name, variables);
        for (auto& issue : result.issues) detail::add_issue(issues, std::move(issue));
        if (!result.value) throw FormulaFailure{};
        return *result.value;
    }

    Json totals(const Json& blueprint, const std::string& path = "/blueprint") {
        Json values = Json::object();
        for (const auto* key : {"cost", "mass", "dryMass", "currentMass", "structureMass", "componentMass", "space",
                                "floorCount", "widthM", "lengthM", "areaM2", "cargo", "hidden", "fuel", "sublightFuel",
                                "rating", "shield", "weapon", "crewCapacity", "passengers", "service", "lifeReserve",
                                "berthCount", "shuttleCount", "carriedMass", "carriedCurrentMass", "carriedDryMass",
                                "carriedCost", "powerUse", "powerSupply"}) values[key] = 0;
        values["deckCount"] = blueprint.at("decks").size();
        values["carriedCraft"] = Json::array();
        const auto number = [&values](std::string_view key) { return values.at(std::string(key)).get<double>(); };
        const auto add = [&values, &number](const std::string& key, double amount) { values[key] = number(key) + amount; };
        bool no_modules = true, has_engine = false;
        double burn_rate = 0;
        std::uint64_t min_x = rules.at("grid").at("w"), min_y = rules.at("grid").at("h"), max_x = 0, max_y = 0;
        for (std::size_t d = 0; d < blueprint.at("decks").size(); ++d) {
            const auto& deck = blueprint.at("decks")[d];
            add("floorCount", static_cast<double>(deck.at("floors").size()));
            for (auto it = deck.at("floors").begin(); it != deck.at("floors").end(); ++it) {
                const auto comma = it.key().find(',');
                std::uint64_t x = 0, y = 0;
                const auto* start = it.key().data();
                std::from_chars(start, start + comma, x);
                std::from_chars(start + comma + 1, start + it.key().size(), y);
                min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                max_x = std::max(max_x, x); max_y = std::max(max_y, y);
            }
            for (std::size_t m = 0; m < deck.at("modules").size(); ++m) {
                const auto& module = deck.at("modules")[m];
                const auto& part = *parts.at(module.at("type").get<std::string>());
                no_modules = false;
                has_engine = has_engine || part.value("system", std::string()) == "engine";
                burn_rate += part.value("burnRate", 0.0);
                add("cost", part.at("cost").get<double>());
                add("componentMass", part.at("mass").get<double>());
                add("space", part.at("space").get<double>());
                add("powerSupply", part.value("generation", 0.0));
                add("powerUse", part.at("powerModes").at("cruise").get<double>());
                add("sublightFuel", part.value("sublightCapacity", 0.0));
                if (part.contains("effect")) {
                    for (auto it = part.at("effect").begin(); it != part.at("effect").end(); ++it) add(it.key(), it.value().get<double>());
                }
                if (part.contains("carrier")) {
                    add("berthCount", 1);
                    if (module.contains("craft") && !module.at("craft").is_null()) {
                        const auto child_path = at(at(at(at(at(path, "decks"), std::to_string(d)), "modules"), std::to_string(m)), "craft");
                        const auto child = totals(module.at("craft"), child_path);
                        add("shuttleCount", 1);
                        for (const auto* key : {"carriedMass", "carriedDryMass", "componentMass"}) add(key, child.at("dryMass"));
                        add("carriedCurrentMass", child.at("currentMass"));
                        add("carriedCost", child.at("cost"));
                        add("cost", child.at("cost"));
                        add("passengers", child.at("passengers"));
                        values["carriedCraft"].push_back({{"id", module.at("id")}, {"deck", d}, {"name", module.at("craft").at("name")},
                            {"mass", child.at("dryMass")}, {"dryMass", child.at("dryMass")}, {"currentMass", child.at("currentMass")},
                            {"cost", child.at("cost")}, {"widthM", child.at("widthM")}, {"lengthM", child.at("lengthM")}});
                    }
                }
            }
        }
        add("cost", number("floorCount") * rules.at("floor").at("cost").get<double>());
        values["structureMass"] = round_computed(formula("structureMass", {{"floorCount", values.at("floorCount")}, {"floorMass", rules.at("floor").at("mass")}}), snapshot);
        for (const auto* key : {"componentMass", "carriedMass", "carriedCurrentMass", "carriedDryMass"}) values[key] = round_computed(number(key), snapshot);
        values["dryMass"] = round_computed(formula("dryMass", values), snapshot);
        values["mass"] = values.at("dryMass");
        const auto& loads = blueprint.at("loads");
        const auto& performance = rules.at("performance");
        const auto cargo_mass = formula("cargoMass", {{"cargoUnits", (number("cargo") + number("hidden")) * loads.at("cargo").get<double>()},
                                                        {"cargoMassPerUnit", performance.at("cargoMassPerUnit")}});
        const auto jump_mass = formula("jumpFuelMass", {{"jumpFuelCharges", number("fuel") * loads.at("jumpFuel").get<double>()},
                                                         {"jumpFuelMassPerCharge", performance.at("jumpFuelMassPerCharge")}});
        values["currentMass"] = round_computed(formula("loadedMass", {{"dryMass", values.at("dryMass")}, {"cargoMass", cargo_mass},
            {"jumpFuelMass", jump_mass}, {"sublightFuelMass", number("sublightFuel") * loads.at("sublightFuel").get<double>()},
            {"carriedLoadMass", number("carriedCurrentMass") - number("carriedDryMass")}}), snapshot);
        const auto cell_size = rules.at("cellSizeM").get<double>();
        values["areaM2"] = number("floorCount") * cell_size * cell_size;
        if (number("floorCount") > 0) {
            values["widthM"] = static_cast<double>(max_x - min_x + 1) * cell_size;
            values["lengthM"] = static_cast<double>(max_y - min_y + 1) * cell_size;
        }
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.value().is_number() && !std::isfinite(it.value().get<double>())) {
                detail::add_issue(issues, error("nonfinite_total", "A direct total is not finite.", at("/derived", it.key())));
            }
        }
        Json pending = Json::array();
        if (no_modules && number("floorCount") == 0 && number("dryMass") == 0 && number("currentMass") == 0) {
            for (const auto* formula_name : {"acceleration", "range"}) {
                pending.push_back({{"formula", formula_name}, {"operand", "mass"}});
            }
            pending.push_back({{"formula", "deltaV"}, {"operand", "endMass"}});
            pending.push_back({{"formula", "deltaV"}, {"operand", "ln(0)"}});
            pending.push_back({{"formula", "fuelToDryMassRatio"}, {"operand", "dryMass"}});
        }
        if (!has_engine && burn_rate == 0) {
            for (const auto* scenario : {"full", "current"}) {
                pending.push_back({{"formula", "burnSeconds"}, {"scenario", scenario}, {"operand", "burnRate"}});
            }
            pending.push_back({{"formula", "exhaustVelocity"}, {"operand", "burnRate"}});
        }
        if (!pending.empty()) {
            detail::add_issue(issues, {"pending_F20", "warning", "pending F20: excluded formula results await Luna and remain unset.",
                              path, {{"finding", "F20"}, {"issue", 51}, {"evaluations", std::move(pending)}}});
        }
        return values;
    }
};
} // namespace

Result<ImportedDefinition> import_definition(std::string_view bytes, const RulesSnapshot& rules,
                                             const SchemaSet& schemas, InputLimits limits) {
    auto parsed = parse_json(bytes, limits);
    if (!parsed.ok()) return {std::nullopt, std::move(parsed.issues)};
    const auto& input = *parsed.value;
    if (!input.is_object() || !input.contains("schema") || !input.at("schema").is_string()) {
        return {std::nullopt, {error("unsupported_schema", "An explicit supported blueprint or definition schema is required.", "/schema")}};
    }
    const auto schema = input.at("schema").get<std::string>();
    const auto version = source_version(schema);
    if (version == 1 || version == 2) {
        return {std::nullopt, {error("unsupported_version", "Versions 1 and 2 are not supported by this adapter; provide a v3 or v4 export.", "/schema")}};
    }
    if (version == 0) return {std::nullopt, {error("unsupported_schema", "Expected an apsis-shipyard-v3/v4 blueprint or apsis-ship-definition-v3/v4 wrapper.", "/schema")}};
    auto shape_issues = schemas.validate(input, schema);
    if (!shape_issues.empty()) return {std::nullopt, std::move(shape_issues)};
    const bool wrapped = schema == "apsis-ship-definition-v3" || schema == "apsis-ship-definition-v4";
    Importer importer(rules);
    try {
        importer.report.sourceVersion = version;
        Json blueprint = wrapped ? input.at("blueprint") : input;
        const auto blueprint_path = wrapped ? std::string("/blueprint") : std::string();
        importer.normalize(blueprint, 0, blueprint_path, version);
        importer.report.rulesIdentityChanged = version == 3 || (wrapped &&
            (input.at("rulesVersion") != rules.version() || input.at("rulesSha256") != rules.sha256() || input.at("rules") != rules.data()));
        if (importer.report.rulesIdentityChanged) {
            importer.change("Applied active v4 rules " + rules.sha256() + "; imported caches and rules were not used as authority.");
            if (!has_errors(importer.issues)) {
                importer.add_note(blueprint, "Imported rule identity differed; active v4 rules were applied and direct totals recomputed.", blueprint_path);
            }
        }
        if (has_errors(importer.issues)) {
            sort_issues(importer.issues);
            return {std::nullopt, std::move(importer.issues)};
        }
        // New automatic notes are present now; retain each craft independently.
        importer.retain_notes(blueprint, blueprint_path);
        // Validate the normalized target before any rule-dependent arithmetic.
        auto target_issues = schemas.validate(blueprint, "apsis-shipyard-v4");
        if (!target_issues.empty()) return {std::nullopt, std::move(target_issues)};
        Json derived;
        try {
            derived = importer.totals(blueprint);
        } catch (const FormulaFailure&) {
            sort_issues(importer.issues);
            return {std::nullopt, std::move(importer.issues)};
        } catch (const std::invalid_argument&) {
            detail::add_issue(importer.issues, error("nonfinite_total", "A direct total cannot be rounded to a finite value.", "/derived"));
            sort_issues(importer.issues);
            return {std::nullopt, std::move(importer.issues)};
        } catch (const std::overflow_error&) {
            detail::add_issue(importer.issues, error("nonfinite_total", "A direct total exceeds the numeric rounding range.", "/derived"));
            sort_issues(importer.issues);
            return {std::nullopt, std::move(importer.issues)};
        }
        if (has_errors(importer.issues)) {
            sort_issues(importer.issues);
            return {std::nullopt, std::move(importer.issues)};
        }
        detail::add_issue(importer.issues, {"pending_validation", "warning", "Imported cached results are discarded; structural validation and readiness await issue #50.", "/validation", {{"issue", 50}}});
        detail::add_issue(importer.issues, {"pending_performance", "warning", "Performance is not evaluated by import or structural validation; call perf() for performance results.", "/derived", {{"issue", 51}, {"finding", "F20"}}});
        detail::add_issue(importer.issues, {"pending_docking", "warning", "Docking outcomes await issue #50 and unresolved Luna conventions.", "/derived", {{"issue", 50}, {"findings", {"F17", "F19", "F21"}}}});
        sort_issues(importer.issues);
        Json definition = {{"schema", rules.data().at("schemaIds").at("definition")}, {"blueprint", std::move(blueprint)},
            {"derived", std::move(derived)}, {"validation", {{"ready", nullptr}, {"jumpReady", nullptr}, {"issues", importer.issues}}},
            {"rulesVersion", rules.version()}, {"rulesSha256", rules.sha256()}, {"rules", rules.data()}};
        auto output_issues = schemas.validate(definition, "apsis-ship-definition-v4");
        if (!output_issues.empty()) return {std::nullopt, std::move(output_issues)};
        return {ImportedDefinition{std::move(definition), std::move(importer.report)}, std::move(importer.issues)};
    } catch (const detail::IssueLimitReached&) {
        sort_issues(importer.issues);
        return {std::nullopt, std::move(importer.issues)};
    }
}
} // namespace apsis::shipyard
