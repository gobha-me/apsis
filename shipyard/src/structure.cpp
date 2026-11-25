#include "validator_internal.hpp"
#include "internal.hpp"

#include <algorithm>
#include <charconv>
#include <compare>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace apsis::shipyard::detail {
namespace {
using Coordinate = std::int64_t;
using Cell = std::pair<Coordinate, Coordinate>;
using Node = std::tuple<std::size_t, Coordinate, Coordinate>;
using Cells = std::set<Cell>;
using ComponentLabels = std::map<Cell, std::size_t>;

constexpr std::size_t max_hold_work = 1'000'000;
struct HoldWorkLimitReached {
    std::string path;
};

struct HoldWork {
    std::size_t remaining = max_hold_work;

    void charge(std::size_t units, const std::string& path) {
        if (std::min(units, remaining) != units) throw HoldWorkLimitReached{path};
        remaining -= units;
    }
};

constexpr Cell neighbors[] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};

void enqueue_neighbors(Cell cell, std::deque<Cell>& queue, auto can_enqueue) {
    for (const auto& [dx, dy] : neighbors) {
        const Cell next{cell.first + dx, cell.second + dy};
        if (can_enqueue(next)) queue.push_back(next);
    }
}

struct Rectangle {
    Coordinate x, y, w, h;
    auto operator<=>(const Rectangle&) const = default;

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

ComponentLabels label_walkways(const Cells& walkable) {
    ComponentLabels labels;
    for (const auto& cell : walkable) {
        const auto component = labels.size();
        if (!labels.emplace(cell, component).second) continue;
        std::deque<Cell> queue{cell};
        while (!queue.empty()) {
            const auto current = queue.front();
            queue.pop_front();
            enqueue_neighbors(current, queue, [&](Cell next) {
                return walkable.contains(next) && labels.emplace(next, component).second;
            });
        }
    }
    return labels;
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
        code == "module_overlap" || code == "unique_system" || code == "carried_fit" ||
        code == "hold_open_column" || code == "hold_access" || code == "hold_hangar_overlap") {
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
StructureReport inspect_structure(const Json& blueprint, const RulesSnapshot& snapshot,
                                  const std::string& path, ValidationOptions options,
                                  HoldWork& hold_work) {
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
        add_issue(result.issues, {std::move(code), level, std::move(message),
                                  std::move(location), std::move(details)});
    };
    try {
        std::map<std::string, const Json*, std::less<>> catalogue;
        for (const auto& part : rules.at("parts")) catalogue.emplace(part.at("id").get<std::string>(), &part);
        std::vector<Cells> floors(decks.size()), walkable(decks.size());
        std::vector<std::size_t> components(decks.size());
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
            while (!remainder.empty()) {
                ++components[d];
                std::deque<Cell> queue{*remainder.begin()};
                remainder.erase(remainder.begin());
                while (!queue.empty()) {
                    const auto cell = queue.front(); queue.pop_front();
                    enqueue_neighbors(cell, queue, [&](Cell next) { return remainder.erase(next) != 0; });
                }
            }
            // The explicit crew-route test seam retains local hull checks. The
            // default path below checks the whole design from its command module.
            if (!options.crewRoutes && components[d] > 1) {
                issue("disconnected_hull", "The deck contains disconnected hull floor regions.",
                      deck_path + "/floors", {{"deck", d}, {"components", components[d]}});
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

        struct HoldChecks {
            bool closed_column = false;
            bool access = false;
            std::vector<std::size_t> overlaps;
        };
        std::map<std::tuple<Rectangle, std::size_t, std::size_t>, HoldChecks> holds;
        std::vector<std::vector<const Rectangle*>> deck_holds;
        if (const auto records = blueprint.find("holds"); records != blueprint.end() && !records->empty()) {
            const auto holds_path = path + "/holds";
            hold_work.charge(records->size(), holds_path);
            deck_holds.resize(decks.size());
            std::vector<std::optional<ComponentLabels>> components_by_deck(decks.size());
            for (std::size_t i = 0; i < records->size(); ++i) {
                const auto& record = records->at(i);
                const Rectangle rectangle{record.at("x").get<Coordinate>(), record.at("y").get<Coordinate>(),
                                          record.at("w").get<Coordinate>(), record.at("h").get<Coordinate>()};
                const auto first = record.at("firstDeck").get<std::size_t>();
                const auto last = record.at("lastDeck").get<std::size_t>();
                const auto [position, inserted] = holds.try_emplace({rectangle, first, last});
                auto& checks = position->second;
                if (inserted) {
                    Cells covered;
                    std::vector<std::set<std::size_t>> routes;
                    for (auto d = first; d <= last; ++d) {
                        // Charge sparse floor scans and all module comparisons, including
                        // the two later face and clearance queries for each module.
                        hold_work.charge(1, holds_path);
                        hold_work.charge(floors[d].size(), holds_path);
                        hold_work.charge(3 * deck_modules[d].size(), holds_path);
                        deck_holds[d].push_back(&std::get<0>(position->first));
                        for (const auto index : deck_modules[d]) {
                            const auto& module = result.modules[index];
                            if (module.part->contains("docking") &&
                                module.part->at("docking").at("kind") == "hangar" &&
                                intersects(rectangle, footprint(module))) {
                                checks.overlaps.push_back(index);
                            }
                        }
                        // The bottom floor is excluded. One coordinate absent from
                        // every intervening floor plane keeps the column open.
                        if (d != first) {
                            hold_work.charge(floors[d].size(), holds_path);
                            for (const auto& cell : floors[d])
                                if (rectangle.contains(cell)) covered.insert(cell);
                        }
                        auto& labels = components_by_deck[d];
                        if (!labels) {
                            hold_work.charge(floors[d].size(), holds_path);
                            labels = label_walkways(walkable[d]);
                        }
                        auto& touched = routes.emplace_back();
                        for (const auto& [cell, component] : *labels)
                            if (rectangle.contains(cell) || adjacent(rectangle, cell)) touched.insert(component);
                    }
                    checks.closed_column = covered.size() == static_cast<std::uint64_t>(rectangle.w) *
                                                              static_cast<std::uint64_t>(rectangle.h);
                    // The same aligned chain must connect to the hold on each deck.
                    const auto first_ladder = ladders.lower_bound({first, 0, 0});
                    const auto last_ladder = ladders.lower_bound({first + 1, 0, 0});
                    checks.access = std::any_of(first_ladder, last_ladder, [&](const auto& entry) {
                        const auto& [deck, x, y] = entry.first;
                        for (auto d = deck; d <= last; ++d) {
                            hold_work.charge(1, holds_path);
                            const auto& labels = *components_by_deck[d];
                            const auto label = labels.find({x, y});
                            if (label == labels.end() || !routes[d - first].contains(label->second) ||
                                !ladders.contains({d, x, y})) return false;
                        }
                        return true;
                    });
                }
                // Duplicate geometry still has independent diagnostic paths.
                if (checks.closed_column || !checks.access || !checks.overlaps.empty()) {
                    const auto hold_path = holds_path + "/" + std::to_string(i);
                    if (checks.closed_column) {
                        issue("hold_open_column", "The hold needs one open column through every intervening floor plane.",
                              hold_path, {{"firstDeck", first}, {"lastDeck", last}});
                    }
                    if (!checks.access) {
                        issue("hold_access", "The hold needs a ladder or lift chain with a walkable route on every spanned deck.",
                              hold_path, {{"firstDeck", first}, {"lastDeck", last}});
                    }
                    for (const auto index : checks.overlaps) {
                        issue("hold_hangar_overlap", "A hold may touch a hangar but must not overlap it.",
                              hold_path, module_details(result.modules[index]));
                    }
                }
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

        const auto command = std::find_if(result.modules.begin(), result.modules.end(), [](const ModuleState& module) {
            return module.part->value("system", std::string()) == "bridge";
        });
        const auto flood = [&](const std::vector<Cells>& cells, std::optional<Node> start) {
            std::set<Node> reached;
            std::deque<Node> queue;
            const auto enqueue = [&](Node node) {
                if (reached.insert(node).second) queue.push_back(node);
            };
            if (start) enqueue(*start);
            while (!queue.empty()) {
                const auto [deck, x, y] = queue.front(); queue.pop_front();
                for (const auto& [dx, dy] : neighbors) {
                    if (cells[deck].contains({x + dx, y + dy})) enqueue({deck, x + dx, y + dy});
                }
                if (!ladders.contains({deck, x, y})) continue;
                for (const int delta : {-1, 1}) {
                    if ((delta < 0 && deck == 0) || (delta > 0 && deck + 1 == decks.size())) continue;
                    const auto next_deck = delta < 0 ? deck - 1 : deck + 1;
                    if (ladders.contains({next_deck, x, y}) && cells[next_deck].contains({x, y})) {
                        enqueue({next_deck, x, y});
                    }
                }
            }
            return reached;
        };

        // Match v3 bridgeStart: use only the first bridge's first walkable service
        // cell, bottom edge first, then alternating left/right edges, then top.
        // Occupied command cells and other adjacent regions never become shortcuts.
        std::optional<Node> crew_start;
        if (command != result.modules.end()) {
            const auto rectangle = footprint(*command);
            std::optional<std::tuple<int, Coordinate, int, Cell>> first;
            for (const auto& cell : walkable[command->deck]) {
                std::optional<std::tuple<int, Coordinate, int, Cell>> candidate;
                if (cell.second == rectangle.y + rectangle.h &&
                    cell.first >= rectangle.x && cell.first < rectangle.x + rectangle.w) {
                    candidate = std::tuple{0, cell.first, 0, cell};
                } else if (cell.second >= rectangle.y && cell.second < rectangle.y + rectangle.h &&
                           (cell.first == rectangle.x - 1 || cell.first == rectangle.x + rectangle.w)) {
                    candidate = std::tuple{1, cell.second, cell.first == rectangle.x - 1 ? 0 : 1, cell};
                } else if (cell.second == rectangle.y - 1 &&
                           cell.first >= rectangle.x && cell.first < rectangle.x + rectangle.w) {
                    candidate = std::tuple{2, cell.first, 0, cell};
                }
                if (candidate && (!first || *candidate < *first)) first = candidate;
            }
            if (first) {
                const auto& [x, y] = std::get<3>(*first);
                crew_start = Node{command->deck, x, y};
            } else if (options.crewRoutes) {
                issue("command_access", "The command station has no adjacent walkable crew access.",
                      command->path, module_details(*command));
            }
            if (options.crewRoutes) {
                const auto connected = flood(floors, Node{command->deck, command->x, command->y});
                for (std::size_t d = 0; d < floors.size(); ++d) {
                    std::size_t isolated = 0;
                    for (const auto& [x, y] : floors[d]) {
                        if (!connected.contains({d, x, y})) ++isolated;
                    }
                    if (isolated != 0) {
                        issue("disconnected_hull", "Hull floor cells are disconnected from the command station; join floors or align deck ladders.",
                              path + "/decks/" + std::to_string(d) + "/floors",
                              {{"deck", d}, {"components", components[d]}, {"isolatedCells", isolated}});
                    }
                }
            }
        }
        const auto reached = flood(walkable, crew_start);

        bool usable_airlock = false;
        for (auto& module : result.modules) {
            for (const auto& cell : walkable[module.deck]) {
                if ((adjacent(footprint(module), cell) || footprint(module).contains(cell)) &&
                    reached.contains({module.deck, cell.first, cell.second})) {
                    module.reachable = true;
                    break;
                }
            }
            const auto inside_hold = [&](const Rectangle& rectangle) {
                if (deck_holds.empty()) return false;
                const auto& on_deck = deck_holds[module.deck];
                return std::any_of(on_deck.begin(), on_deck.end(), [&](const Rectangle* hold) {
                    return intersects(*hold, rectangle);
                });
            };
            const auto face = outward_face(module);
            module.exterior = !any_in(floors[module.deck], face) && !inside_hold(face);
            const auto clearance = approach(module, grid_width, grid_height);
            module.clear = !any_in(floors[module.deck], clearance) && !inside_hold(clearance);
            for (const auto other_index : deck_modules[module.deck]) {
                const auto& other = result.modules[other_index];
                if (other.id != module.id && intersects(clearance, footprint(other))) module.clear = false;
            }
            if (!module.reachable && module.part->value("system", std::string()) != "bridge" &&
                command != result.modules.end() && options.crewRoutes) {
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
                auto child = inspect_structure(craft, snapshot, child_path, options, hold_work);
                const auto truncated = std::find_if(child.issues.begin(), child.issues.end(), [](const Issue& value) {
                    return value.code == "issues_truncated";
                });
                if (truncated != child.issues.end()) {
                    // Resource failure cannot be downgraded to the carrier's
                    // configurable carried_not_ready warning severity. Retain
                    // its diagnostics through the same shared cap before the
                    // root marker, rather than returning a marker-only report.
                    for (const auto& child_issue : child.issues) {
                        if (child_issue.code != "issues_truncated") add_issue(result.issues, child_issue);
                    }
                    add_issue(result.issues, *truncated);
                    throw IssueLimitReached{};
                }
                if (has_errors(child.issues)) {
                    auto details = module_details(module);
                    details["issues"] = Json::array();
                    for (const auto& child_issue : child.issues) {
                        // Every child report is capped. Retain its direct codes
                        // without recursively copying descendants' issue trees.
                        Json summary = {{"code", child_issue.code}, {"severity", child_issue.severity},
                            {"message", child_issue.message}, {"path", child_issue.path},
                            {"details", Json::object()}};
                        for (const auto& [key, value] : child_issue.details.items()) {
                            if (key != "issues") summary["details"][key] = value;
                        }
                        details["issues"].push_back(std::move(summary));
                    }
                    issue("carried_not_ready", "The carried craft has structural faults.", child_path, std::move(details));
                }
                // A child's structural errors use carried_not_ready's rule severity;
                // its independent warnings still deserve their own precise paths.
                for (const auto& child_issue : child.issues) {
                    if (child_issue.severity != "error") add_issue(result.issues, child_issue);
                }
                Coordinate min_x = grid_width, min_y = grid_height, max_x = -1, max_y = -1;
                for (const auto& deck : craft.at("decks")) {
                    for (auto it = deck.at("floors").begin(); it != deck.at("floors").end(); ++it) {
                        const auto [x, y] = parse_cell(it.key());
                        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                        max_x = std::max(max_x, x); max_y = std::max(max_y, y);
                    }
                }
                if (const auto records = craft.find("holds"); records != craft.end()) {
                    for (const auto& hold : *records) {
                        const auto x = hold.at("x").get<Coordinate>();
                        const auto y = hold.at("y").get<Coordinate>();
                        min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                        max_x = std::max(max_x, x + hold.at("w").get<Coordinate>() - 1);
                        max_y = std::max(max_y, y + hold.at("h").get<Coordinate>() - 1);
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
    } catch (const IssueLimitReached&) {
        // add_issue already recorded the shared truncation marker; stop all
        // remaining geometry and carried-craft traversal immediately.
    }
    sort_issues(result.issues);
    return result;
}
} // namespace

StructureReport inspect_structure(const Json& blueprint, const RulesSnapshot& snapshot,
                                  const std::string& path, ValidationOptions options) {
    HoldWork hold_work;
    try {
        return inspect_structure(blueprint, snapshot, path, options, hold_work);
    } catch (const HoldWorkLimitReached& exceeded) {
        return {{{"structure_resource_limit", "error", "Hold checks exceed the operational structural work limit.",
                  exceeded.path, {{"limit", max_hold_work}}}}, {}};
    }
}
} // namespace apsis::shipyard::detail
