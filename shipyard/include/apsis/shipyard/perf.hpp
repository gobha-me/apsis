#pragma once

#include <apsis/shipyard/loader.hpp>

namespace apsis::shipyard {
// Unspecified fractions retain the normalized blueprint's current loads.
// Overrides affect this design; carried craft retain their own current loads.
struct LoadFractions {
    std::optional<double> cargo = std::nullopt;
    std::optional<double> jumpFuel = std::nullopt;
    std::optional<double> sublightFuel = std::nullopt;
};

enum class BlankType { ship, station };
// An empty blueprint using active defaults and the minimum deck count. This
// authoring source does not imply structural approval or assign F20 values.
// Throws std::length_error if generation would exceed 4096 decks or 8 MiB of
// deck names in total.
Json blank_blueprint(const RulesSnapshot& rules, BlankType type = BlankType::ship);

// DESIGN.md result envelope, including a freshly normalized definition and
// current/empty/half/full performance. Imported caches are never authoritative.
Json perf(std::string_view bytes, const RulesSnapshot& rules,
          const SchemaSet& schemas, LoadFractions loads = {},
          InputLimits limits = {});
} // namespace apsis::shipyard
