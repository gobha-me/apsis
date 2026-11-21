#pragma once

#include <apsis/shipyard/loader.hpp>

namespace apsis::shipyard::detail {
// The definition must come from import_definition with the active snapshot.
// Load overrides have already passed the same normalization/input checks.
Result<Json> calculate_performance(const Json& definition,
                                   const RulesSnapshot& rules,
                                   const SchemaSet& schemas,
                                   const Json& loads,
                                   InputLimits limits = {});
} // namespace apsis::shipyard::detail
