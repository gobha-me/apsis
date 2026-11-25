#pragma once
#include <apsis/shipyard/loader.hpp>
namespace apsis::shipyard::detail {
inline constexpr std::size_t maxIssues = 128;
// Stop traversal as soon as the cap is reached, including on warning-only paths.
struct IssueLimitReached {};
void add_issue(std::vector<Issue>& issues, Issue issue);
Issue error(std::string code, std::string message, std::string path = "");
std::string pointer_token(std::string_view token);
// Checked evaluation visits independent operands and reports every failure.
// The perf operation classifies only DESIGN's exact F20 exclusions as pending.
Result<double> evaluate_formula(const RulesSnapshot& rules, std::string_view name,
                                const Json& variables);
// Performance reuses child totals by normalized blueprint path and reports its
// own findings instead of the importer's provisional diagnostics.
using CarriedTotals = std::map<std::string, Json, std::less<>>;
// Import bounded parser output, applying load overrides before direct totals.
Result<ImportedDefinition> import_parsed_definition(const Json& input,
    const RulesSnapshot& rules, const SchemaSet& schemas,
    const Json& load_overrides, CarriedTotals* performance_totals = nullptr);
std::size_t character_count(std::string_view utf8);
} // namespace apsis::shipyard::detail
