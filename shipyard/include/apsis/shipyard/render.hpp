#pragma once

#include <apsis/shipyard/loader.hpp>
#include <cstdint>

namespace apsis::shipyard {
inline constexpr std::string_view renderer_version = "apsis-cpu-1";

// Width/height describe each deck panel. stack.png concatenates the panels
// vertically in blueprint order. All options are explicit in the manifest.
struct RenderOptions {
    std::uint32_t width = 800;
    std::uint32_t height = 640;
    bool labels = true;
};

// Operational limits, independent of the active game rules. Callers may lower
// these defaults, never raise them beyond the hard limits listed in RENDERING.md.
struct RenderLimits {
    std::size_t maxDecks = 16;
    std::uint32_t maxDimension = 8192;
    std::uint64_t maxTotalPixels = 16 * 1024 * 1024;
    std::size_t maxOutputBytes = 64 * 1024 * 1024;
};

struct RenderArtifact {
    std::string filename;
    std::string mediaType;
    std::vector<std::uint8_t> bytes;
    std::string sha256;
    std::uint32_t width = 0;  // Zero for non-image artifacts.
    std::uint32_t height = 0;
};

struct RenderResult {
    Json envelope;  // DESIGN.md's apsis-shipyard-result-v1, operation render.
    std::vector<RenderArtifact> artifacts; // Empty on failure; no files are written.
};

// Normalize and validate with the existing production defaults. A structural,
// schema, pending-feature, or operational-limit failure returns null data and
// no artifacts. Successful output order is decks, stack.png, manifest.json,
// stack.zip. Imported caches and embedded rules never govern pixels.
RenderResult render(std::string_view bytes, const RulesSnapshot& rules,
                    const SchemaSet& schemas, RenderOptions options = {},
                    RenderLimits renderLimits = {}, InputLimits inputLimits = {});
} // namespace apsis::shipyard
