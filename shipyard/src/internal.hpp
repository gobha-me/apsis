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
// The perf operation distinguishes unavailable design stats from invalid formulas.
Result<double> evaluate_formula(const RulesSnapshot& rules, std::string_view name,
                                const Json& variables);
Result<double> evaluate_formula_operand(const Json& operand, const Json& variables,
                                        const std::string& path = "");
std::size_t character_count(std::string_view utf8);
} // namespace apsis::shipyard::detail
