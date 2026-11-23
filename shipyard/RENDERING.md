# Headless renderer (#53)

Include `<apsis/shipyard/render.hpp>` and call:

~~~cpp
auto preview = apsis::shipyard::render(inputBytes, rules, schemas);
if (preview.envelope.at("pass").get<bool>()) {
    for (const auto& artifact : preview.artifacts) {
        // artifact.bytes belongs to the caller. File/transport handling is separate.
    }
}
~~~

The library returns an `apsis-shipyard-result-v1` envelope with operation
`render` and a vector of owned binary artifacts. It performs no filesystem
writes, outbound requests, UI work, or rendering through a DOM/GPU. The renderer
uses the ordinary production `validate` operation with its default checks after
normalization with the active rules; imported derived/validation/rules caches
cannot govern geometry. A structural, schema, pending-feature, or resource
failure returns FAIL, null data, and no artifacts. Active validation warnings
remain in the successful envelope. No performance metric or unresolved game rule is synthesized.

Successful artifact order is one PNG per blueprint deck, `stack.png`,
`manifest.json`, then `stack.zip`. The ZIP contains exactly those deck PNGs,
`stack.png`, and `manifest.json`, in that order. It contains no copy of itself.
ZIP entries use the stored method, fixed DOS timestamp 1980-01-01 00:00:00, no
comments, and no caller-selected paths. Entry names are
`deck-NN-<slug>.png`, with a one-based, two-digit index. Slugs contain only
lowercase ASCII letters, digits, and collapsed hyphens, are at most 40 bytes,
and fall back to `deck`. The index makes duplicate or entirely non-ASCII deck
names safe and unique. Original Unicode names remain intact in the manifest.

The manifest schema is `apsis-shipyard-render-manifest-v1`. It records renderer
version `apsis-cpu-1`, rules version/hash, the SHA-256 of
`canonical_json(validate(...).data.definition)`, original blueprint/deck names,
the effective options, the fixed camera, animation time zero, and the version
and hash of the bundled font. Its `decks` array records zero-based indices,
dimensions, filenames, byte counts, and PNG hashes. Its `artifacts` array
identifies all PNGs, including the stack. The manifest cannot hash itself or the
ZIP that contains it; their byte counts and hashes appear in the outer
envelope's `data.artifacts` metadata alongside every other returned artifact.
Hash inputs are exact bytes; `manifest.json` is canonical JSON plus one LF.
Non-image typed artifacts have width and height zero and omit these fields
from JSON metadata. No manifest field contains the current time or a local path.

## Pixel and geometry contract

`RenderOptions.width` and `.height` are each deck panel's explicit dimensions
(default 800 by 640). `.labels` defaults to true. Every panel uses the same
full-grid top-down camera; it never crops to occupied cells or changes fit for
an individual deck. Positive grid x points right and positive y points down.
For grid width `gw`, height `gh`, panel width `w`, and panel height `h`:

~~~text
cellPixels = min((w - 32) / gw, (h - 48) / gh)  # integer division
originX = (w - gw * cellPixels) / 2
originY = 32 + (h - 32 - gh * cellPixels) / 2
~~~

A panel that cannot fit at least one pixel per grid cell fails
`render_resolution`. The manifest records these exact integer camera values.
The stack concatenates the complete panels vertically, in increasing blueprint
index, so stack dimensions are `w` by `h * deckCount`; its panel pixels are
identical to their separate PNGs. Original deck indices are retained. Holds keep those normal deck numbers and
carry their inclusive one-based human span labels.

PNG pixels are opaque RGBA8. Background is (12,22,30), the full-grid rectangle
is (18,31,40), module borders are (43,64,77), title ink is (230,240,245), and
module text/facing ink is (8,16,24). Floors use active `floor.color`; modules
use active catalogue colors and footprints. A `#RRGGBB` color is interpreted
exactly; other color strings use neutral (128,128,128) as a presentation fallback,
without changing rules or validation.

Floor rectangles begin at their grid origin, with one pixel inset on the top
and left when the cell size is at least three. Module rectangles use the
catalogue's w/h when the active rotation's dx is zero, and h/w otherwise,
matching the validator's cardinal footprints. They have a one-pixel border
when cells are at least three pixels. Modules paint after floors in their
blueprint order. A small integer arrow depicts the active rule direction when
cells are at least four pixels. Labels use the bundled original 5-by-7 bitmap
font in `src/render_font.hpp`; ASCII is uppercased, unsupported Unicode scalar
values display as question marks, and text is clipped to its available width.
Labels can be disabled without changing geometry. No system fonts or external
visual assets are consulted.

The rasterizer uses integer coordinates, solid fills, and integer lines:
there is no antialiasing, floating camera, lighting, animation, random seed,
thread scheduling, or SIMD-dependent pixel calculation. **Allowed pixel variance
is zero.** Repeated calls with identical bytes, rules, and options must produce
identical PNG, manifest, and ZIP bytes in the supported g++-13/C++23 runtime
with the committed miniz version. Semantic geometry and manifest identity
remain exact on other platforms; cross-toolchain compressed-byte parity is not
a separate compatibility promise. Decoder/library upgrades require explicit
version and comparison review.

## Operational bounds

These limits control work and allocation; they are independent of ship/station
rules and do not grant a build any additional decks, parts, or grid area.
`RenderLimits` can lower its defaults but cannot raise the hard limits:

| Limit | Default / hard maximum |
| --- | ---: |
| Top-level rendered decks | 16 |
| Each requested panel dimension | 64–2048 pixels |
| Any output image dimension, including the stack | 8192 pixels |
| Total decoded pixels, all deck PNGs plus the stack | 16,777,216 |
| Sum of all returned artifact bytes, including the ZIP | 67,108,864 |
| Input module plus hold entries across the design and carried craft | 4096 |
| Input floor entries across the design and carried craft | 262,144 |
| Active grid cells and individual part dimensions | 262,144 |

The normal parser's finite 8 MiB / 64-container defaults still apply;
`InputLimits` passes consistently through parsing and validation. Counts and
pixel products are checked before raster allocation; input work limits are
checked before structural validation. Output sizes are checked as artifacts
are encoded, and the ZIP's write callback refuses growth past the remaining
byte budget. A request cannot return partial artifacts after a limit failure.
The combined pixel cap bounds the live stack plus one deck raster to at most
64 MiB, before encoded artifacts, dependency workspace, and the normalized
model. There is no library wall-clock budget; transport execution deadlines
remain #52 work. These bounds do not set #57's deployment/resource policy.

Measured with the Release g++-13/C++23 build, pinned miniz, the real public API,
and unmodified canonical inputs:

| Input / options | Combined decoded pixels | Returned bytes, including ZIP | Render duration | Peak process RSS |
| --- | ---: | ---: | ---: | ---: |
| Wren, 800×640, one deck | 1,024,000 | 51,016 | 18 ms | 12,928 KiB |
| Wayfarer Anchorage, 2048×1365, three decks | 16,773,120 | 444,276 | 198 ms | 52,936 KiB |

The second measurement exercises 99.98% of the hard pixel budget and includes
the 2048×4095 stack. Duration measures the render call; RSS includes process and
rules/schema loading. These measurements support a finite allocation policy;
they do not promise that every valid scene has the same compressed size or
runtime. The byte cap is separately enforced during encoding, and lower caller
limits can bound accepted output further. Operational deployment limits still
need #57's review.

## Prototype reference

The preserved prototype previews describe an edited five-deck `Kestrel`; the
canonical `Kestrel Tender` fixture has three decks. Those images are reference
material, not goldens. The prototype uses shaded textured orthographic views
and an exploded stack. This version intentionally uses a top-down schematic
and a vertical contact sheet to make every panel's geometry exact and
inspectable. The scene mismatch prevents approving a canonical pixel comparison
against that five-deck preview. F25 records that boundary; no prototype image,
archive, or HTML is imported, and no unresolved game decision is inferred.

## Multi-deck holds

Luna, under owner delegation 2026-11-23 resolved F11–F17 and F19. Each accepted
hold is outlined in the existing border color after floors and before modules
on each deck it spans. With labels enabled, the compact ASCII bitmap label `HOLD 02-04` identifies that
same volume; normal deck titles and filenames retain their original indices.
The manifest has one `holds` entry per object, carrying its original rectangle,
inclusive zero-based deck span and one-based `label`. This array is omitted
when no hold is present. No-hold PNG pixels, compression and font are unchanged.
A hold is included in the existing input-object work cap; no new game limit or
material value follows from rendering.
