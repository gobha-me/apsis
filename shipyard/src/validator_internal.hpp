#pragma once

#include <apsis/shipyard/validator.hpp>
#include <cstdint>

namespace apsis::shipyard::detail {
// References point into the normalized blueprint and active rules supplied to
// inspect_structure; callers must keep both alive while using the report.
struct ModuleState {
    std::size_t deck = 0;
    std::size_t index = 0;
    std::uint64_t id = 0;
    const Json* module = nullptr;
    const Json* part = nullptr;
    std::string path;
    std::int64_t x = 0, y = 0, width = 0, height = 0;
    int dx = 0, dy = 0;
    bool reachable = false;
    bool exterior = false;
    bool clear = false;
    std::string interface;
};

struct StructureReport {
    std::vector<Issue> issues;
    std::vector<ModuleState> modules;
};

// Input has already passed import_definition's schema and rule scalar checks.
StructureReport inspect_structure(const Json& blueprint,
                                  const RulesSnapshot& rules,
                                  const std::string& path = "/blueprint",
                                  ValidationOptions options = {});
std::vector<Issue> pending_holds(const Json& input, const std::string& path = "");
} // namespace apsis::shipyard::detail
