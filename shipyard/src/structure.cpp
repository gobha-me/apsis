#include "validator_internal.hpp"

#include <algorithm>
#include <charconv>
#include <deque>
#include <map>
#include <set>
#include <tuple>

namespace apsis::shipyard::detail {
namespace {
using Coordinate = std::int64_t;
using Cell = std::pair<Coordinate, Coordinate>;
using Node = std::tuple<std::size_t, Coordinate, Coordinate>;
using Cells = std::set<Cell>;

constexpr Cell neighbors[] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};

struct Rectangle {
    Coordinate x, y, w, h;
    bool contains(Cell c) const {
        return c.first >= x && c.first < x + w &&
               c.second >= y && c.second < y + h;
    }
};

Rectangle footprint(const ModuleState& module) {
    return {module.x, module.y, module.width, module.height};
}

bool intersects(const Rectangle& a, const Rectangle& b) {
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
           a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

bool adjacent(const Rectangle& rectangle, Cell cell) {
    return ((cell.first == rectangle.x - 1 || cell.first == rectangle.x + rectangle.w) &&
            cell.second >= rectangle.y && cell.second < rectangle.y + rectangle.h) ||
           ((cell.second == rectangle.y - 1 || cell.second == rectangle.y + rectangle.h) &&
            cell.first >= rectangle.x && cell.first < rectangle.x + rectangle.w);
}

Cell parse_cell(std::string_view key) {
    Cell result;
    const auto comma = key.find(',');
    std::from_chars(key.data(), key.data() + comma, result.first);
    std::from_chars(key.data() + comma + 1, key.data() + key.size(), result.second);
    return result;
}

std::string severity(const Json& rules, const std::string& code) {
    const auto& validation = rules.at("validation");
    const auto& checks = validation.at("checks");
    return checks.contains(code) ? checks.at(code).at("severity").get<std::string>()
                                 : validation.at("defaultSeverity").get<std::string>();
}

std::string severity_rule(const std::string& code) {
    // Occupancy/shape faults share the rules' invalid_blueprint severity while
    // retaining precise, machine-readable issue codes for clients.
    if (code == "module_out_of_bounds" || code == "missing_floor" ||
        code == "module_overlap" || code == "unique_system" || code == "carried_fit") {
        return "invalid_blueprint";
    }
    return code;
}

Json module_details(const ModuleState& module) {
    return {{"deck", module.deck}, {"moduleId", module.id}};
}

bool any_in(const Cells& cells, const Rectangle& rectangle) {
    if (rectangle.w <= 0 || rectangle.h <= 0) return false;
    for (auto it = cells.lower_bound({rectangle.x, rectangle.y});
         it != cells.end() && it->first < rectangle.x + rectangle.w; ++it) {
        if (rectangle.contains(*it)) return true;
    }
    return false;
}

Rectangle outward_face(const ModuleState& module) {
    if (module.dx < 0) return {module.x - 1, module.y, 1, module.height};
    if (module.dx > 0) return {module.x + module.width, module.y, 1, module.height};
    if (module.dy < 0) return {module.x, module.y - 1, module.width, 1};
    return {module.x, module.y + module.height, module.width, 1};
}

Rectangle approach(const ModuleState& module, Coordinate width, Coordinate height) {
    if (module.dx < 0) return {0, module.y, module.x, module.height};
    if (module.dx > 0) return {module.x + module.width, module.y,
                             std::max(Coordinate{0}, width - module.x - module.width), module.height};
    if (module.dy < 0) return {module.x, 0, module.width, module.y};
    return {module.x, module.y + module.height, module.width,
            std::max(Coordinate{0}, height - module.y - module.height)};
}

bool has_errors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(), [](const Issue& issue) {
        return issue.severity == "error";
    });
}
} // namespace

StructureReport inspect_structure(const Json& blueprint, const RulesSnapshot& snapshot,
                                  const std::string& path, ValidationOptions options) {
    const auto& rules = snapshot.data();
    const auto& decks = blueprint.at("decks");
    const auto grid_width = rules.at("grid").at("w").get<Coordinate>();
    const auto grid_height = rules.at("grid").at("h").get<Coordinate>();
    StructureReport result;
    const auto issue = [&](std::string code, std::string message,
                           std::string location, Json details = Json::object()) {
        const auto rule = severity_rule(code);
        const auto level = severity(rules, rule);
        details["rule"] = rules.at("validation").at("checks").contains(rule)
            ? "/validation/checks/" + rule : "/validation/defaultSeverity";
        result.issues.push_back({std::move(code), level, std::move(message),
                                 std::move(location), std::move(details)});
    };
    std::map<std::string, const Json*, std::less<>> catalogue;
    for (const auto& part : rules.at("parts")) catalogue.emplace(part.at("id").get<std::string>(), &part);
    std::vector<Cells> floors(decks.size()), walkable(decks.size());
    std::vector<std::vector<std::size_t>> deck_modules(decks.size());
    std::map<std::string, std::size_t, std::less<>> systems;
    std::map<Node, std::vector<std::size_t>> ladders;

    for (std::size_t d = 0; d < decks.size(); ++d) {
        const auto deck_path = path + "/decks/" + std::to_string(d);
        for (auto it = decks[d].at("floors").begin(); it != decks[d].at("floors").end(); ++it) {
            floors[d].insert(parse_cell(it.key()));
        }
        const auto& modules = decks[d].at("modules");
        for (std::size_t m = 0; m < modules.size(); ++m) {
            const auto& module = modules[m];
            const auto& part = *catalogue.at(module.at("type").get<std::string>());
            const auto& direction = rules.at("rotations").at(module.at("r").get<std::size_t>());
            ModuleState state;
            state.deck = d;
            state.index = m;
            state.id = module.at("id").get<std::uint64_t>();
            state.module = &module;
            state.part = &part;
            state.path = deck_path + "/modules/" + std::to_string(m);
            state.x = module.at("x").get<Coordinate>();
            state.y = module.at("y").get<Coordinate>();
            state.dx = direction.at("dx").get<int>();
            state.dy = direction.at("dy").get<int>();
            state.width = part.at(state.dx == 0 ? "w" : "h").get<Coordinate>();
            state.height = part.at(state.dx == 0 ? "h" : "w").get<Coordinate>();
            if (part.contains("docking")) {
                state.interface = module.value("interface", part.at("docking").at("interface").get<std::string>());
            }
            if (part.contains("system")) ++systems[part.at("system").get<std::string>()];
            deck_modules[d].push_back(result.modules.size());
            if (part.value("verticalLink", false)) ladders[{d, state.x, state.y}].push_back(result.modules.size());
            result.modules.push_back(std::move(state));
        }

        // A missing floor is a hull fault, not an invitation to synthesize one.
        for (const auto index : deck_modules[d]) {
            const auto& module = result.modules[index];
            const auto rectangle = footprint(module);
            if (rectangle.x + rectangle.w > grid_width || rectangle.y + rectangle.h > grid_height) {
                issue("module_out_of_bounds", "The rotated part footprint extends beyond the active grid.",
                      module.path, module_details(module));
            }
            std::size_t supported = 0;
            for (const auto& cell : floors[d]) if (rectangle.contains(cell)) ++supported;
            // Rule grids can exceed 32-bit dimensions. Do not enumerate absent
            // footprint cells or allocate a dense grid based on their bounds.
            if (static_cast<Coordinate>(supported) / rectangle.w != rectangle.h ||
                static_cast<Coordinate>(supported) % rectangle.w != 0) {
                issue("missing_floor", "Every part footprint cell requires an existing hull floor.",
                      module.path, module_details(module));
            }
        }
        for (std::size_t i = 0; i < deck_modules[d].size(); ++i) {
            const auto& a = result.modules[deck_modules[d][i]];
            for (std::size_t j = i + 1; j < deck_modules[d].size(); ++j) {
                const auto& b = result.modules[deck_modules[d][j]];
                if (intersects(footprint(a), footprint(b))) {
                    auto details = module_details(b);
                    details["otherModuleId"] = a.id;
                    issue("module_overlap", "Part footprints overlap.", b.path, std::move(details));
                }
            }
        }

        Cells remainder = floors[d];
        std::size_t components = 0;
        while (!remainder.empty()) {
            ++components;
            std::deque<Cell> queue{*remainder.begin()};
            remainder.erase(remainder.begin());
            while (!queue.empty()) {
                const auto cell = queue.front(); queue.pop_front();
                for (const auto& [dx, dy] : neighbors) {
                    const Cell next{cell.first + dx, cell.second + dy};
                    if (remainder.erase(next) != 0) queue.push_back(next);
                }
            }
        }
        if (components > 1) {
            issue("disconnected_hull", "The deck contains disconnected hull floor regions.",
                  deck_path + "/floors", {{"deck", d}, {"components", components}});
        }
        for (const auto& cell : floors[d]) {
            bool can_walk = rules.at("floor").at("walkable").get<bool>();
            for (const auto index : deck_modules[d]) {
                const auto& module = result.modules[index];
                if (footprint(module).contains(cell) && !module.part->value("walkable", false)) {
                    can_walk = false;
                    break;
                }
            }
            if (can_walk) walkable[d].insert(cell);
        }
    }

    const auto build_type = blueprint.at("buildType").get<std::string>();
    for (const auto& required : rules.at("validation").at("requiredSystems").at(build_type)) {
        const auto system = required.get<std::string>();
        if (systems[system] == 0) {
            issue("required_system", "A required system is absent.", path + "/decks", {{"system", system}});
        }
    }
    for (const auto& unique : rules.at("validation").at("uniqueSystems")) {
        const auto system = unique.get<std::string>();
        if (systems[system] > 1) {
            issue("unique_system", "The design has more than one instance of a unique system.",
                  path + "/decks", {{"system", system}, {"count", systems[system]}});
        }
    }

    // Crew starts on walkable floor next to a command module. Occupied,
    // nonwalkable command cells never become shortcuts through machinery.
    std::set<Node> reached;
    std::deque<Node> queue;
    const auto enqueue = [&](Node node) {
        if (reached.insert(node).second) queue.push_back(node);
    };
    for (const auto& module : result.modules) {
        if (module.part->value("system", std::string()) != "bridge") continue;
        bool access = false;
        for (const auto& cell : walkable[module.deck]) {
            if (adjacent(footprint(module), cell) || footprint(module).contains(cell)) {
                enqueue({module.deck, cell.first, cell.second});
                access = true;
            }
        }
        if (!access && options.crewRoutes) {
            issue("command_access", "The command station has no adjacent walkable crew access.",
                  module.path, module_details(module));
        }
    }
    while (!queue.empty()) {
        const auto [deck, x, y] = queue.front(); queue.pop_front();
        for (const auto& [dx, dy] : neighbors) {
            if (walkable[deck].contains({x + dx, y + dy})) enqueue({deck, x + dx, y + dy});
        }
        if (!ladders.contains({deck, x, y})) continue;
        for (const int delta : {-1, 1}) {
            if ((delta < 0 && deck == 0) || (delta > 0 && deck + 1 == decks.size())) continue;
            const auto next_deck = delta < 0 ? deck - 1 : deck + 1;
            if (ladders.contains({next_deck, x, y}) && walkable[next_deck].contains({x, y})) {
                enqueue({next_deck, x, y});
            }
        }
    }

    bool usable_airlock = false;
    for (auto& module : result.modules) {
        for (const auto& cell : walkable[module.deck]) {
            if ((adjacent(footprint(module), cell) || footprint(module).contains(cell)) &&
                reached.contains({module.deck, cell.first, cell.second})) {
                module.reachable = true;
                break;
            }
        }
        module.exterior = !any_in(floors[module.deck], outward_face(module));
        const auto clearance = approach(module, grid_width, grid_height);
        module.clear = !any_in(floors[module.deck], clearance);
        for (const auto other_index : deck_modules[module.deck]) {
            const auto& other = result.modules[other_index];
            if (other.id != module.id && intersects(clearance, footprint(other))) module.clear = false;
        }
        if (!module.reachable && module.part->value("system", std::string()) != "bridge" && options.crewRoutes) {
            issue("unreachable_component", "The component has no walkable crew route from a command station.",
                  module.path, module_details(module));
        }
        if (module.part->value("clearApproach", false) && !module.clear && options.approachClearance) {
            issue("blocked_approach", "The outward approach is obstructed by hull or another component.",
                  module.path, module_details(module));
        }
        if (module.part->contains("docking") && module.part->at("docking").at("kind") == "airlock" &&
            (!options.crewRoutes || module.reachable) &&
            (!options.approachClearance || (module.exterior && module.clear))) usable_airlock = true;

        if (module.part->value("verticalLink", false)) {
            const bool lower = module.deck > 0 && ladders.contains({module.deck - 1, module.x, module.y});
            const bool upper = module.deck + 1 < decks.size() && ladders.contains({module.deck + 1, module.x, module.y});
            if (!lower && !upper) {
                issue("unpaired_ladder", "The ladder has no matching ladder at the same coordinate on an adjacent deck.",
                      module.path, module_details(module));
            }
        }
        if (module.part->contains("carrier") && module.module->contains("craft") && !module.module->at("craft").is_null()) {
            const auto& craft = module.module->at("craft");
            const auto child_path = module.path + "/craft";
            auto child = inspect_structure(craft, snapshot, child_path, options);
            if (has_errors(child.issues)) {
                auto details = module_details(module);
                details["issues"] = child.issues;
                issue("carried_not_ready", "The carried craft has structural faults.", child_path, std::move(details));
            }
            // A child's structural errors use carried_not_ready's rule severity;
            // its independent warnings still deserve their own precise paths.
            for (const auto& child_issue : child.issues) {
                if (child_issue.severity != "error") result.issues.push_back(child_issue);
            }
            Coordinate min_x = grid_width, min_y = grid_height, max_x = -1, max_y = -1;
            for (const auto& deck : craft.at("decks")) {
                for (auto it = deck.at("floors").begin(); it != deck.at("floors").end(); ++it) {
                    const auto [x, y] = parse_cell(it.key());
                    min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x); max_y = std::max(max_y, y);
                }
            }
            const auto cell_size = rules.at("cellSizeM").get<double>();
            const auto width = max_x < 0 ? 0.0 : static_cast<double>(max_x - min_x + 1) * cell_size;
            const auto length = max_y < 0 ? 0.0 : static_cast<double>(max_y - min_y + 1) * cell_size;
            const auto& carrier = module.part->at("carrier");
            const auto epsilon = rules.at("numeric").at("epsilon").get<double>();
            if (width > carrier.at("maxWidthM").get<double>() + epsilon ||
                length > carrier.at("maxLengthM").get<double>() + epsilon ||
                craft.at("decks").size() > carrier.at("maxDecks").get<std::size_t>()) {
                issue("carried_fit", "The carried craft exceeds the carrier's rule-defined envelope.", child_path,
                      {{"deck", module.deck}, {"moduleId", module.id}, {"widthM", width}, {"lengthM", length},
                       {"deckCount", craft.at("decks").size()}, {"limits", carrier}});
            }
        }
    }
    if (rules.at("validation").at("requiredAirlock").at(build_type).get<bool>() && !usable_airlock) {
        issue("missing_airlock", "A reachable airlock facing clear exterior space is required.", path + "/decks");
    }
    if (build_type == "station" && (systems["engine"] != 0 || systems["jumpDrive"] != 0)) {
        issue("station_propulsion", "The station contains propulsion systems.", path + "/decks");
    }
    sort_issues(result.issues);
    return result;
}
} // namespace apsis::shipyard::detail
