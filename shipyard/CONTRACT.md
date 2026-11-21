# Loader contract

## Input, formulas, and schemas

Strict UTF-8 JSON rejects duplicate keys after escape decoding, nonfinite
numbers, unknown versions/authoritative fields, invalid shapes, and excessive
nesting before totals. Defaults are operational parser bounds of 8 MiB per
document and 64 nested containers; callers can set `InputLimits`. These are
library resource guards, not game limits or deployment sizing. Draft schemas
describe shape; the loader then checks active catalogue references, grid
coordinates, module IDs/nextId, rotations, interfaces, carry depth, and limits.
Text limits count Unicode scalar values, consistent with JSON Schema length
semantics; no Unicode normalization occurs. C0 and DEL controls are rejected
in labels/notes. Integers accept mathematical integral JSON numbers, including
1.0, within active bounds and the interoperable exact range.

All fifteen named formulas must exist. Numbers, per-formula variables, and
operator arrays form the entire expression format. Operators: add/mul take
2–64 operands, sub/div exactly 2, floor/ln exactly 1, min/max 1–64.
The 64-operand and 64-level formula guards are operational implementation bounds.
Unknown names/operators, wrong variables, malformed operands, and invalid arity
reject rules at load time (F10). Import evaluates structureMass, dryMass,
cargoMass, jumpFuelMass, and loadedMass for direct totals; `perf` evaluates
performance formulas with the same checked evaluator.
Missing operands, zero division, invalid logarithms, overflow, and nonfinite
intermediates fail with a formula path; no zero fallback exists.
Independent operands are checked even after another operand fails, so an F20
exclusion cannot hide an unrelated formula failure. Applicable blank and
engine-less cases return nonblocking `pending_F20` issues naming the excluded
evaluations. No excluded F20 value or numeric stand-in is emitted; blank imports
succeed with pending reports. See the performance contract below.

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
on reimport. Existing full note lists are preserved; extra changes remain in
the report with a warning. Export is always v4. Import alone does not approve
missing systems, overlaps, hull access, or carried-fit legality; call `validate`
for these checks. Migration never repairs Rustbucket or other structural faults.

## Structural validation and docking (#50)

`validate(bytes, rules, schemas, ValidationOptions{}, InputLimits{})` accepts the
same v3/v4 blueprint or definition inputs as import. It returns the DESIGN.md
result envelope with active rules identity, sorted structured issues, and
`data.definition` plus migration details if normalization succeeds. Structural
errors retain the inspectable definition. `data.structuralPass` and the envelope
status describe the enabled structural checks. Cached validation is discarded.
`validation.ready` is false on structural failure and null on structural pass;
`jumpReady` stays null in this structural operation; use `perf` for performance
and readiness.
Applicable `pending_F20` remains nonblocking and excluded values remain absent.
No performance warnings or affordability checks are introduced here.

`ValidationOptions.crewRoutes` and `.approachClearance` default to true and are
reported in `data.checks`. Geometry uses active part footprints and cardinal
facing directions, four-neighbor floor/walk routes, and paired `verticalLink`
modules at matching coordinates on adjacent decks. Walkability follows the
active floor and part records. Hull connectivity is checked within each deck;
crew connectivity between decks requires those ladder pairs. Missing floors,
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
length, and deck limits, enforce the deck limit, and leave width/length fit
uncertified with `pending_F19` until allowed rotations are settled. No dock can
pass by ignoring that pending fit. No visitor rotation or airlock picker exists.

Multi-deck hold behavior is unavailable pending F11–F17. The validator and dock
recognize a requested blueprint `holds` field (including nested carried craft)
and return blocking `pending_F11` through `pending_F17` issues with null data.
They do not assign a hold schema, capacity, mass, access, or floor/traversal rule.
In particular they never misreport the intended opening as an ordinary missing
floor. The #49 importer and bundled schemas remain unchanged. Ordinary cargo
modules and adjacent-deck ladders continue to work normally.

The independent tests session should exercise canonical structural outcomes,
rule-severity changes, ordinary ladders/crew routes, carried craft, each F18
switch with mutation evidence, multiple compatible airlocks, ports beyond former
size caps, hangar limits subject to F19, and all pending gates. No new tests or
workflow steps are supplied by this implementation session.

## Performance (#51)

`perf(bytes, rules, schemas, LoadFractions{}, InputLimits{})` accepts the same
v3/v4 blueprint or definition inputs as import, normalizes with active rules,
and runs the existing structural checks. Imported `derived`, `validation`, and
embedded `rules` never supply calculation inputs. Optional `cargo`, `jumpFuel`,
and `sublightFuel` overrides must be finite fractions in [0, 1]; omitted fields
retain blueprint loads. Overrides affect only the requested design's current
loads. Carried craft retain their own current loads; empty/half/full scenario
fractions come from the active rules.

Carried craft performance is evaluated recursively and exposed in
`performance.carriedCraft`. Nested issues retain active severities and identify
their blueprint/output locations; a child formula failure rejects the result.
This prevents a parent calculation from hiding a child-specific F10 failure.

The `apsis-shipyard-result-v1` envelope identifies operation `perf`, active rules,
and sorted issues. On successful calculation, `data.performance` and
`data.definition.derived.performance` contain current/empty/half/full mass,
thrust, acceleration, theoretical jump range, propellant, jump charges, endurance,
and idle/cruise/jump supply and demand. The definition retains recomputed direct
costs; reported jump-drive costs use active part prices, including all five
explicit range/price entries, without interpolation.

`data.ready` reflects structural and performance issues at active severities;
`data.jumpReady` additionally requires a drive, a currently loaded jump, and
sufficient jump power. Theoretical range alone does not establish either flag.
Performance checks read `validation.checks.<code>.severity`, defaulting to
`warning`; active `error` findings fail the result. Structural findings retain
their own active severities. Malformed input, invalid rules, and non-F20 formula
failures reject clearly; calculation failure returns null data.

Until Luna answers F20, omit exactly the excluded undefined results: blank
zero-mass ships/stations' acceleration and range division by `mass`, deltaV
division by `endMass` and ln(0), and fuel-to-dry-mass ratio division by `dryMass`;
engine-less designs' full/current burnSeconds and exhaustVelocity division by
zero `burnRate`. Each case returns structured `pending_F20` details. DeltaV
depending on omitted exhaustVelocity is also omitted with a pending dependency
issue. No zero, null metric, NaN, string placeholder, or successful unavailable
value substitutes for an excluded result. Other formula errors still reject,
including unrelated errors beside an excluded operand. Legitimate computed
zero-thrust/no-drive values remain numeric results.

`blank_blueprint(rules)` creates a ship using active defaults and minimum deck
count. `blank_blueprint(rules, BlankType::station)` creates a station explicitly.
Serialize either with `canonical_json` and pass it to `perf`; no prior editor
state is inherited. F20 does not reject these sources or Wayfarer Anchorage;
ordinary structural issues remain inspectable. Holds still return pending
F11–F17 before guessed totals. F19/F21 docking conventions remain unchanged.

The independent tests session should cover all four scenarios, load overrides,
carried loads and child-specific formula failures, all five explicit drive prices,
active severity changes, separate
ready/jumpReady/range outcomes, every F20 omission/dependency, and F10 rejection
including a non-F20 sibling beside an F20 expression. Preserve the five v3
goldens and compare only their rules-defined values; F24 records v4 differences.

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
canonical form. Later independent tests should lock down these contracts.
