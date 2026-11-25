#pragma once

#include <apsis/shipyard/validator.hpp>
#include "internal.hpp"
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

inline bool terminal_structure_issue(const Issue& issue) {
    return issue.code == "issues_truncated" || issue.code == "structure_resource_limit";
}

// Input has already passed import_definition's schema and rule scalar checks.
StructureReport inspect_structure(const Json& blueprint,
                                  const RulesSnapshot& rules,
                                  const std::string& path = "/blueprint",
                                  ValidationOptions options = {});
// Validation and docking share the ordinary schema-checked importer.
Result<ImportedDefinition> import_for_validation(std::string_view bytes,
                                               const RulesSnapshot& rules,
                                               const SchemaSet& schemas,
                                               InputLimits limits = {},
                                               const Json& load_overrides = {},
                                               CarriedTotals* performance_totals = nullptr);
} // namespace apsis::shipyard::detail
