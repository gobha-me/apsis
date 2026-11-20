#pragma once
#include <apsis/shipyard/loader.hpp>
namespace apsis::shipyard::detail {
Issue error(std::string code, std::string message, std::string path = "");
std::string pointer_token(std::string_view token);
// Only direct totals call this internal evaluator in #49. Performance and
// its pending F20 cases are deliberately not exposed by this library yet.
Result<double> evaluate_formula(const RulesSnapshot& rules, std::string_view name,
                                const Json& variables);
std::size_t character_count(std::string_view utf8);
} // namespace apsis::shipyard::detail
