# Loader contract

## Input, formulas, and schemas

Strict UTF-8 JSON rejects duplicate keys after escape decoding, nonfinite
numbers, unknown versions/authoritative fields, invalid shapes, and excessive
nesting before totals. Defaults are operational parser bounds of 8 MiB per
document and 64 nested containers; callers can set `InputLimits`. These are
library resource guards, not game limits or deployment sizing. A checked SAX
DOM builder avoids rescanning arrays after each object while retaining the
duplicate-key, nesting, finite-number, and strict JSON checks. Schema checking
has a recursion-depth cap of 512 (`maxSchemaSteps`), not a total-work budget.
HTTP request threads need at least 1 MB of stack for these bounded recursive
checks (#52). The library has no wall-clock budget; the HTTP layer must enforce
the request time limit (#52). Draft schemas describe shape; the loader then
checks active catalogue references, grid coordinates, module IDs/nextId,
rotations, interfaces, carry depth, and limits.
C0 and DEL controls are rejected in labels/notes; no Unicode normalization
occurs. F22's name-length counting and label trimming/fallback choices are
**Open for Luna, not guessed**, with no value picked. The current loader's
Unicode scalar-value counting and preservation of empty or whitespace-only
labels without trimming or fallback are an **interim interpretation**. The
prototype instead uses JavaScript UTF-16 length units and trims labels with a
fallback. These differences are not a settled technical encoding contract.
Integers accept mathematical integral JSON numbers, including
1.0, within active bounds and the interoperable exact range.

`SchemaSet::validate`, `import_definition`, `load_rules`, `validate`, and `dock`
return at most 128 issues, including the truncation marker. On the 128th
attempted diagnostic for a returned list, collection stops with 127 ordinary
issues and one marker:
`code: "issues_truncated"`, `severity: "error"`, root `path: ""`, and
`details: {"limit": 128, "suppressedAtLeast": 1}`. Further issues are dropped
and remaining checks stop, so truncation makes the operation fail even if the
retained diagnostics were only warnings. `suppressedAtLeast` counts omitted
diagnostics as a lower bound; it does not count the unevaluated remainder or
claim a total issue count. Issues, including the marker, are sorted by path,
code, and severity. Diagnostics are bounded during collection rather
than accumulated in an unbounded temporary list. Internal schema-alternative
trials have the same bound; a truncated trial is a nonmatch, so another
alternative may still accept the input.
Structural issue collection, carried-craft diagnostics, and docking's structural
details use this same shared bound; no unbounded issue list is accumulated before
assembling the public result. A carried-craft failure summarizes its child
diagnostics without recursively copying their complete issue trees.
A truncated carried-craft report propagates its retained diagnostics and root
marker through the same cap. Truncation is terminal for `validate` and `dock`,
which return FAIL with `data: null`, as for top-level truncation.

All fifteen named formulas must exist. Numbers, per-formula variables, and
operator arrays form the entire expression format. Operators: add/mul take
2–64 operands, sub/div exactly 2, floor/ln exactly 1, min/max 1–64.
The 64-operand and 64-level formula guards are operational implementation bounds.
Unknown names/operators, wrong variables, malformed operands, and invalid arity
reject rules at load time (F10). Only structureMass, dryMass, cargoMass,
jumpFuelMass, and loadedMass are internally evaluated for direct totals.
Missing operands, zero division, invalid logarithms, overflow, and nonfinite
intermediates fail with a formula path; no zero fallback exists.
There is no performance evaluation API here: acceleration/range/burnSeconds/
exhaustVelocity/deltaV/fuelToDryMassRatio remain for #51. Applicable blank and
engine-less cases return a nonblocking `pending_F20` issue naming the excluded
evaluations. No excluded F20 value or numeric stand-in is emitted, and blank
imports succeed with pending reports.

The local schema checker implements a bounded Draft 2020-12 vocabulary:
$schema, $id (root only), $ref (bundled documents and JSON Pointers), $defs,
type, properties, required, additionalProperties, propertyNames, items,
prefixItems, minItems/maxItems, minProperties/maxProperties, minLength/maxLength,
minimum/maximum/exclusiveMinimum/exclusiveMaximum, enum, const, uniqueItems,
anyOf/oneOf/allOf, not, if/then/else, pattern, and annotations title,
description, $comment, default, examples. Unsupported keywords and unresolved
references reject schema loading. Pattern support is intentionally limited to
the three bundled ASCII identifier, canonical coordinate, and lowercase SHA-256
patterns; matching is linear without recursive regular expressions. Nested
cache payloads are opaque informational data, discarded rather than executed.
This is not a general-purpose schema library.

## Migration and numbers

Migration accepts v3 blueprints/wrappers, validates source shape, fills only
missing optional metadata from active defaults, preserves geometry/IDs/order/
labels/budget/loads, and recursively migrates carried blueprints. v1/v2 return
an actionable unsupported-version error. Reports identify source/target
versions, rule-identity changes, and deterministic changes; notes are deduplicated
on reimport. Migration-note overflow handling (F22) is
**Open for Luna, not guessed**, with no value picked. As an
**interim interpretation**, the current loader preserves existing full note
lists and leaves extra changes in the report with a warning; the prototype
keeps the newest notes instead. This difference is not a settled technical
encoding contract. Export is always v4.
Import alone does not approve missing systems, overlaps, hull access, or
carried-fit legality; call `validate` for these checks. Migration never repairs
Rustbucket or other structural faults.

## Structural validation and docking (#50)

`validate(bytes, rules, schemas, ValidationOptions{}, InputLimits{})` accepts the
same v3/v4 blueprint or definition inputs as import. It returns the DESIGN.md
result envelope with active rules identity, sorted structured issues, and
`data.definition` plus migration details if normalization succeeds. Structural
errors retain the inspectable definition unless issue collection is truncated.
`data.structuralPass` and the envelope status describe the enabled structural
checks. Cached validation is discarded.
`validation.ready` is false on structural failure and null on structural pass;
`jumpReady` stays null until #51 evaluates rule-defined performance checks.
Applicable `pending_F20` remains nonblocking and excluded values remain absent.
No performance warnings or affordability checks are introduced here.

CLI and HTTP always use default `ValidationOptions` and `DockOptions`; adapters
never map request fields onto these switches. The types remain in the public
library API because existing independent tests use them to exercise the real
checks without mocking. They are an explicit test seam, not adapter settings.
`ValidationOptions.crewRoutes` and `.approachClearance` default to true and are
reported in `data.checks`. Geometry uses active part footprints and cardinal
facing directions, four-neighbor floor/walk routes, and paired `verticalLink`
modules at matching coordinates on adjacent decks. Walkability follows the
active floor and part records. With production defaults, hull connectivity
matches v3: a structural flood starts at the first command module's origin,
crosses paired ladders, and reports any isolated floor cells, including a
separate floored deck with no ladder. Crew reach starts at one walkable service
cell beside that first command module, with the bottom edge considered first,
then left/right edges, then the top edge, matching v3 `bridgeStart`. The command
module does not connect otherwise separate crew routes. The explicit
`crewRoutes=false` test seam skips command-rooted cross-deck connectivity while
retaining local per-deck hull checks; adapters cannot select that seam. Missing floors,
overlap, out-of-grid footprints, duplicate unique systems, and carried-fit
violations retain specific issue codes and use the `invalid_blueprint` severity
family. All structural severities use the active validation checks and their
default severity; issue details identify the rule. Carried blueprints use these same checks; their
occupied dimensions and deck counts are checked against carrier limits without
inventing docking rotations.

`dock(hostBytes, targetDeckIndex, targetModuleId, visitorBytes, rules, schemas,
DockOptions{}, InputLimits{})` requires a station host, ship visitor, and active
rule-defined target. `data.checks.a` through `.d` contain enabled flags, statuses,
and measured details. `DockOptions.structural`, `.portReachability`,
`.matchingAirlock`, and `.approachClearance` independently disable F18(a), (b),
(c), and (d). With defaults, (a) includes all host/visitor structural issues.
Disabling (b) also disables the selected target's crew-access finding inside
(a); disabling (d) likewise disables that target's approach finding inside (a).
Other modules retain their checks. Disabling (a) leaves (b) and (d) active.
Visitor-airlock crew access and clearance are structural findings under (a), at
the active `unreachable_component` and `blocked_approach` severities (v3: error).
They are not duplicated under (c). This permits independent mutation evidence
for each check without mocking. Disabled checks are explicit in the output.

Check (c) counts compatible, exterior, outward-facing airlocks without choosing
one. If a candidate exists, it returns `pending_F21` and status `PENDING` rather
than guessing visitor-deck mapping, a distance measure, reference point, or
inclusivity of the 1-cell bound. Overall docking is FAIL while certification is
pending (`data.pending=true`), even though the pending issue is a warning.
Ports have no size fields or envelope checks. Hangars report active width,
length, and deck limits and enforce the deck limit. A visitor that fits without
rotation reports `widthLengthFit: true` without `pending_F19`. A definite size
error requires a rotation-proof bound: `min(w,l) > min(W,L)` or
`max(w,l) > hypot(W,L)`, where `w,l` are visitor dimensions and `W,L` are hangar
limits. Such a visitor reports `widthLengthFit: false` and
`dock_hangar_size_limit` at the active docking severity (v3: error), with measured
dimensions and limits, without `pending_F19`. Failure in both axis-aligned
orientations alone is insufficient. Every other case that does not fit unrotated
remains uncertified with `widthLengthFit: null` and `pending_F19`; no dock can
pass by ignoring that pending fit. No visitor rotation or airlock picker exists.

Multi-deck hold behavior is unavailable pending F11–F17. The validator and dock
recognize a requested blueprint `holds` field (including nested carried craft).
They schema-check a bounded copy with only those unsupported fields removed
before emitting blocking `pending_F11` through `pending_F17` issues with null
data, once per finding kind rather than once per hold occurrence. Schema errors
take precedence over pending features. The original input is not normalized or
repaired by this probe.
They do not assign a hold schema, capacity, mass, access, or floor/traversal rule.
In particular they never misreport the intended opening as an ordinary missing
floor. The #49 importer and bundled schemas remain unchanged. Ordinary cargo
modules and adjacent-deck ladders continue to work normally.

The independent tests session should exercise canonical structural outcomes,
rule-severity changes, ordinary ladders/crew routes, carried craft, each F18
switch with mutation evidence, multiple compatible airlocks, ports beyond former
size caps, hangar limits subject to F19, and all pending gates. No new tests or
workflow steps are supplied by this implementation session.

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.

`canonical_json` emits compact JSON, lexicographically ordered UTF-8 object
keys, original array/deck/module order and IDs, unescaped valid UTF-8, standard
JSON string escapes, and no trailing newline. Numbers use the pinned JSON
library's locale-independent shortest round-trip encoding; integer and floating
representations remain distinct (1 versus 1.0), floating negative zero becomes
0.0, and nonfinite programmatic values are rejected. Canonicalization is not
RFC 8785. No rounding or Unicode normalization occurs during serialization.
With the same rules bytes, export/reimport preserves the canonical definition
and adds no migration notes. Rule-file SHA-256 uses original bytes, never this
canonical form. Later independent tests should lock down the settled contracts;
the three F22 prototype differences remain open and answer none of F11–F17 or
F19–F21.
