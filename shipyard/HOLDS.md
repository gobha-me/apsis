# Multi-deck holds

F11–F17 and F19 are resolved by **Luna, under owner delegation 2026-11-23**.

A v4 blueprint may include `holds`, an array of records such as:

```json
{"x": 2, "y": 3, "w": 4, "h": 5, "firstDeck": 1, "lastDeck": 3}
```

Coordinates and positive dimensions are grid cells. New hold integers retain
the interoperable exact-integer bound 2^53−1 before numeric conversions.
Deck indices are zero-based
and inclusive. The rectangle must fit the active grid. `holds.minDecks` is two
in the active rules; older valid v4 snapshots without that optional rules block
use the same settled minimum of two. The largest span is the design's actual
deck count.
Omitting the array or supplying an empty array declares no holds. Nested carried
blueprints use the same schema and checks. Each record is one volume, labelled
for people as `decks 2–4`; actual deck names and numbers do not collapse.

At least one existing ladder or lift chain reaches all spanned decks at or
beside the hold edge. Existing `verticalLink` parts at the same coordinate on
every adjacent deck form that chain. A new lift part or access spacing is not
invented. Ordinary floor support, command routes and reachability still apply.

Actual floor cells bordering the open volume are walkable catwalk edges under
the existing floor/part rules. Empty interior volume alone is no crew route.
There are no railing or fall rules. Mezzanines and partial floors are allowed
when a common grid coordinate stays open through all intervening floor planes:
the first floor is the bottom, and the subsequent decks in the inclusive span
are the interior planes. Normalization never fills those openings. A hold does
not excuse an unrelated module with missing support floors.

A hold may touch a hangar footprint at an edge and must never overlap it on a
shared deck. Its span does not consume a host hangar's independent visitor deck
cap. Existing visitor deck counts still include all of the visitor's normal
decks. Cargo-interface airlocks or docking ports may open directly into a hold,
using existing catalogue footprints and `interface: "cargo"`; there is no
new numeric door-size rule. Clear exterior approach, outward orientation and
crew access remain active. The hold interior is not mistaken for exterior space.

Hold rectangles contribute once to the design width/length envelope, including
open areas outside the material floor bounding box. The same physical bounds
apply to independently checked carried-craft envelopes, including nested holds;
a carried volume cannot bypass the existing carrier width/length limits.
Floor area still counts actual material. Numeric cost, mass and capacity continue to come
only from actual floors and catalogue modules, counted once. Removing interior
floors uses the existing floor-count formulas; ladders, cargo parts and doors use
their existing catalogue effects. There is no new incremental per-volume
capacity, density, price or mass formula. Such an unrecorded statistic is not
silently assigned a zero or inferred by multiplying a cargo module by span.

Validation reports precise `hold_bounds`, `hold_span`, `hold_access`,
`hold_open_column`, or `hold_hangar_overlap` diagnostics. The importer checks
grid/span bounds; the validator checks the real geometry and access graph.
Rendering accepts the same normalized model and records one span-labelled object
in its manifest. Malformed input retains schema diagnostics and the shared
128-issue cap. The old pending-F11–F17 probe is removed.

Docking allows only the four right-angle facings already represented by cardinal
rules rotations. A hangar fits if the visitor's width and length fit either
unrotated or swapped, using exact bounds. Equality passes; an oblique-only fit
fails. F21 alignment and F20 undefined-performance handling are separate from
these resolved hold/rotation decisions.

The exact mutation minimum remains `390/491`. The hold implementation retires
eight obsolete allowances (the removed pending-hold parser and replaced
floor-count geometry guard), shifting only 23 byte-identical source-site
identities. The allowed set tightens from 101 to 93; no new survivor is allowed.
