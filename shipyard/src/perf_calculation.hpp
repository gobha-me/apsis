#pragma once

#include "internal.hpp"

namespace apsis::shipyard::detail {
bool has_jump_fuel(double charges, const RulesSnapshot& rules);
// The blueprint and direct totals must come from the same import.
Result<Json> calculate_performance(const Json& blueprint, const Json& totals,
                                   const RulesSnapshot& rules,
                                   const CarriedTotals& carried_totals,
                                   const std::string& blueprint_path = "/blueprint");
} // namespace apsis::shipyard::detail
