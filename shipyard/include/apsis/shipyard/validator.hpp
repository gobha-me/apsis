#pragma once

#include <apsis/shipyard/loader.hpp>
#include <cstdint>

namespace apsis::shipyard {
// Disabling checks is explicit and reflected in the result. CLI and HTTP always
// use defaults and never map request fields onto these switches. These public
// types remain a seam for existing independent tests of the real validator.
struct ValidationOptions {
    bool crewRoutes = true;
    bool approachClearance = true;
};

struct DockOptions {
    bool structural = true;         // F18(a): both designs, including visitor access/clearance
    bool portReachability = true;  // F18(b): station crew access to the selected target
    bool matchingAirlock = true;   // F18(c): matching airlock, with port alignment decided and hangar alignment pending F21
    bool approachClearance = true; // F18(d): selected target's approach
};
// With defaults (a) includes every structural fault. Disabling (b) or (d)
// also disables that selected target's corresponding finding inside (a), so
// duplicate reports cannot mask independent checks. Other modules are checked.

// Both operations return the DESIGN.md apsis-shipyard-result-v1 JSON envelope.
// They always normalize with active rules, never trust imported cache fields.
Json validate(std::string_view bytes, const RulesSnapshot& rules,
              const SchemaSet& schemas, ValidationOptions options = {},
              InputLimits limits = {});

Json dock(std::string_view hostBytes, std::size_t targetDeck,
          std::uint64_t targetModuleId, std::string_view visitorBytes,
          const RulesSnapshot& rules, const SchemaSet& schemas,
          DockOptions options = {}, InputLimits limits = {});
} // namespace apsis::shipyard
