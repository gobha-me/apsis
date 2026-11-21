#pragma once
#include <apsis/shipyard/loader.hpp>
namespace apsis::shipyard::detail {
Issue error(std::string code, std::string message, std::string path = "");
std::string pointer_token(std::string_view token);
// Checked evaluation visits independent operands and reports every failure.
// The perf operation classifies only DESIGN's exact F20 exclusions as pending.
Result<double> evaluate_formula(const RulesSnapshot& rules, std::string_view name,
                                const Json& variables);
std::size_t character_count(std::string_view utf8);
} // namespace apsis::shipyard::detail
