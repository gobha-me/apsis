#pragma once

#include <apsis/shipyard/perf.hpp>

namespace apsis::shipyard {
// Authoring results contain a normalized v4 blueprint, not structural approval.
// Indices are zero-based; generated labels use ordinary one-based deck levels.
// New generated decks carry nameGenerated=true. Missing/false provenance means
// an explicit label, even when its text happens to match a generated name.
Result<Json> insert_deck(std::string_view bytes, std::size_t index,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         std::optional<std::string> name = std::nullopt,
                         InputLimits limits = {});
Result<Json> move_deck(std::string_view bytes, std::size_t from, std::size_t to,
                       const RulesSnapshot& rules, const SchemaSet& schemas,
                       InputLimits limits = {});
Result<Json> remove_deck(std::string_view bytes, std::size_t index,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         InputLimits limits = {});
// A supplied name is explicit (including an empty string); nullopt restores
// generated naming. No operation infers ownership from the label's text.
Result<Json> rename_deck(std::string_view bytes, std::size_t index,
                         std::optional<std::string> name,
                         const RulesSnapshot& rules, const SchemaSet& schemas,
                         InputLimits limits = {});
} // namespace apsis::shipyard
