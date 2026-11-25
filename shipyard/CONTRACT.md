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
occurs. F22 name counting and trim/fallback are decided by
**Luna, under owner delegation 2026-11-23** and implemented separately in #80
and #81. This independent note-retention unit leaves the earlier name runtime
unchanged until those implementations are integrated; it adds no new name
interpretation or unresolved design question.
Integers accept mathematical integral JSON numbers, including
1.0, within active bounds and the interoperable exact range.

`SchemaSet::validate`, `import_definition`, `load_rules`, `validate`, `dock`, and `perf`
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
Structural issue collection, carried-craft diagnostics, performance formulas and
warning-only advisories, and docking's structural details use this same shared bound; no unbounded issue list is accumulated before
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
versions, rule-identity changes, and deterministic changes. Export is always v4.

**Luna, under owner delegation 2026-11-23** resolved F22 migration-note overflow
(#82). After adding any generated notes, import applies the active
`maxMigrationNotes` count to each blueprint independently, including carried
craft. On overflow it keeps the newest `maxMigrationNotes - 1` ordinary notes
in their original order and reserves the first slot for a dropped-history
notice. A cap of one keeps only the notice. Ordinary input notes are checked
for type, controls, and the active `maxNoteLength` before any cull. Retained
ordinary notes preserve their text and duplicates.
When active rules replace a stale rule identity, import appends the explanation
if the same note would otherwise be absent from the retained list.

The optional v4 blueprint field `migrationNotesDropped` accepts only `true`
and requires the first note to be `Older migration notes were dropped.` or `…`.
The field identifies that entry as the generated notice. The same text in
other entries or in a list without the field is ordinary note text. Import
uses the full notice when it fits the active text cap and the ellipsis
otherwise. Same-rules reimport preserves the retained list without another
cull or extra notice.

A cull produces a `migration_note_limit` warning with the active `limit` and
the `dropped` and `retained` counts of ordinary notes. When a generated
ordinary note exceeds the active text cap, the import warns and the migration
report still records the associated change.

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
`jumpReady` stays null in this structural operation; use `perf` for performance
and readiness.
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
length, and deck limits and enforce the exact deck limit. Unrotated fit uses
exact input bounds, `w <= W && l <= L`, and reports `widthLengthFit: true`
without `pending_F19`. A definite size error requires a rotation-proof bound:
the exact input-bound comparison `min(w,l) > min(W,L)` or the computed diagonal
comparison `max(w,l) > hypot(W,L) + numeric.epsilon`, where `w,l` are visitor
dimensions and `W,L` are hangar limits. The diagonal uses the same absolute
epsilon convention as `carried_fit`; equality or excess within epsilon does not
establish a diagonal rejection. A visitor exceeding either bound reports
`widthLengthFit: false` and
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
tolerance. The `carried_fit` width/length checks and hangar's computed diagonal
reject only when `actual > maximum + numeric.epsilon`. Hangar unrotated fit,
minimum-side rejection, and deck limits remain exact input-bound comparisons.
Input bounds, IDs, coordinates, versions, and counts are exact: epsilon never
admits an over-limit input. Computed masses are rounded at the
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
and adds no migration notes. One-time migration warnings appear in the import
result outside the exported `validation.issues` cache. Rule-file SHA-256 uses
the original rules bytes. The F22 decisions leave F11–F17 and F19–F21 open.

## Deck authoring (#55)

`authoring.hpp` exposes `insert_deck`, `move_deck`, `remove_deck`, and
`rename_deck`. Each accepts blueprint or definition bytes and active rules and
schemas, then returns `Result<Json>` containing a normalized v4 blueprint.
These are library authoring helpers, not new CLI/HTTP operations or structural
approval. They recompute direct totals/import diagnostics and preserve the
ordinary structured `pending_F20` omissions. Input failures have no result;
invalid indices return `invalid_deck_index`, and active minimum/maximum deck
violations return `deck_limit`. Name constraints and input bounds remain active.
Operations edit a copy, so failed edits leave the caller's source unchanged.

Deck indices are zero-based array positions; generated ordinary deck labels use
the corresponding one-based decimal level. This does not settle F16's open
multi-deck-hold convention. Naming is a literal prefix plus that level, with no
formatting language: bundled rules provide `defaults.deckNamePrefix: "Deck "`.
The field is optional for earlier v4 snapshots; without it, the prefix is the
existing `defaults.deckName` followed by one space. Rules loading checks the
prefix for controls and that generated names through `maxDecks` fit
`limits.maxDeckName`. Labels are never truncated or trimmed.

Adding `defaults.deckNamePrefix` changes the bundled v4 rules bytes and their
`rulesSha256`, while `rulesVersion` remains `4`. Saved v4 definitions carrying
the previous SHA report `rulesIdentityChanged: true` in the import migration
report; active rules are applied and direct totals are recomputed.

New generated decks, including those in a blank blueprint, carry
`nameGenerated: true`. An explicit name sets no ownership marker; absent or
false provenance always means explicit. Import preserves both text and any
provenance exactly; it never guesses ownership from a name pattern. Inserting,
moving, or removing a deck renumbers only generated root-deck names. An explicit
rename clears ownership, even when the supplied name equals a generated label;
passing `nullopt` to `rename_deck` restores generated naming. The same optional
name behavior applies to insertion. Empty explicit labels remain subject to the
existing interim F22 behavior. Retained deck contents, coordinates, module IDs,
`nextId`, and carried-craft labels are unchanged. Removal deletes only the
selected deck; subsequent structural validation reports any resulting faults.

F04's default blank ship and explicit blank station are already provided by
#65. F01's five prices are already in the v4 rules from #62; F02's port checks
omit size envelopes and hangars retain dimensions/deck caps through #63. #55
preserves these behaviors and leaves F06's concealed-cargo tooltip deferred
until a UI. None of F11–F17, F19, F20, or F21 is resolved here.


## Historical hangar wording before final Argus r2

The following prior F19-related wording is retained verbatim as superseded
merge evidence. The current docking section above governs: a definite size
failure needs the rotation-proof minimum-span or diagonal bound; failure in
both axis-aligned orientations is insufficient. No rotation rule is chosen
by this historical excerpt.

~~~text
Ports have no size fields or envelope checks. Hangars report active width,
length, and deck limits and enforce the deck limit. A visitor that fits without
rotation reports `widthLengthFit: true` without `pending_F19`. A visitor that
exceeds the envelope in both orientations reports `widthLengthFit: false` and
`dock_hangar_size_limit` at the active docking severity (v3: error), with measured
dimensions and limits, without `pending_F19`. Only a fit requiring rotation
remains uncertified with `pending_F19`; no dock can pass by ignoring that pending
fit. No visitor rotation or airlock picker exists.
~~~


<details>
<summary>Loader-only documentation at the main #62 squash</summary>

This loader-stage snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern this branch.
All open owner decisions remain open. No FINDINGS IDs were renumbered.

~~~text
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

`SchemaSet::validate`, `import_definition`, and `load_rules` return at most 128
issues, including the truncation marker. On the 128th attempted diagnostic for
a returned list, collection stops with 127 ordinary issues and one marker:
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
Missing systems, overlaps, hull access, and carried-fit legality remain for
#50; migration never repairs them.
Rustbucket remains available for later structural rejection.

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
~~~

</details>

<details>
<summary>Main efe5fd82 integration: preserved source text</summary>

These literal merge-source excerpts preserve both sides of the squash-history
merge. The current integrated contract and findings above govern this branch;
performance and authoring are already implemented. Superseded implementation
status is historical, and no open owner decision is resolved here.

~~~text
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

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.

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
~~~

</details>

<details>
<summary>Integrated performance 61ed6a0: preserved historical merge text</summary>

These literal source excerpts retain the complete historical text of both
branches. Current integrated behavior is specified above, including implemented
performance and deck authoring. No owner finding is resolved or renumbered.

````text
<details>
<summary>Loader-stage snapshot at the main #62 squash</summary>

This snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern.
No owner decisions are added and no FINDINGS IDs are renumbered.

## Historical numeric wording before Argus round 4

The following prior hangar and numeric-comparison paragraphs are preserved
verbatim as superseded merge evidence. The current docking and numeric sections
above govern: input dimensions/deck counts remain exact, while the computed
hangar diagonal uses active absolute numeric.epsilon. This records the reviewed
comparison correction and assigns no allowed docking rotation or owner rule.

~~~text
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

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.
~~~
````

</details>


<details>
<summary>Main #66 integration: complete preserved documentation</summary>

The following source documentation is retained verbatim to preserve both merge
parents, including the exact Mull gate and C++ lint requirements. Current
performance and deck-authoring behavior is documented above; historical
implementation status does not override it or resolve a pending finding.
No F entry is removed or renumbered.

````````text
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
length, and deck limits and enforce the exact deck limit. Unrotated fit uses
exact input bounds, `w <= W && l <= L`, and reports `widthLengthFit: true`
without `pending_F19`. A definite size error requires a rotation-proof bound:
the exact input-bound comparison `min(w,l) > min(W,L)` or the computed diagonal
comparison `max(w,l) > hypot(W,L) + numeric.epsilon`, where `w,l` are visitor
dimensions and `W,L` are hangar limits. The diagonal uses the same absolute
epsilon convention as `carried_fit`; equality or excess within epsilon does not
establish a diagonal rejection. A visitor exceeding either bound reports
`widthLengthFit: false` and
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
tolerance. The `carried_fit` width/length checks and hangar's computed diagonal
reject only when `actual > maximum + numeric.epsilon`. Hangar unrotated fit,
minimum-side rejection, and deck limits remain exact input-bound comparisons.
Input bounds, IDs, coordinates, versions, and counts are exact: epsilon never
admits an over-limit input. Computed masses are rounded at the
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

## Superseded original #66 contract text

The following verbatim original contract from `27b1e3b` is retained as history,
including the former F22 encoding choices and F18/F19 scope descriptions. The
reviewed current contract above is authoritative; the historical text does not
settle an open finding or override a correction. No F finding was renumbered.

<details>
<summary>Historical contract before the current #63 integration</summary>

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
`jumpReady` stays null until #51 evaluates rule-defined performance checks.
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

</details>


<details>
<summary>Historical r1 text retained at the r2 integration</summary>

The following pre-r2 text is preserved verbatim from the Mull branch. It is
superseded by the current contract and reviewed F19/F23 rotation-proof bounds
and terminal-truncation behavior above; it assigns no current owner decision.
No FINDINGS IDs were renumbered.

```md
## Structural validation and docking (#50)

`validate(bytes, rules, schemas, ValidationOptions{}, InputLimits{})` accepts the
same v3/v4 blueprint or definition inputs as import. It returns the DESIGN.md
result envelope with active rules identity, sorted structured issues, and
`data.definition` plus migration details if normalization succeeds. Structural
errors retain the inspectable definition. `data.structuralPass` and the envelope
status describe the enabled structural checks. Cached validation is discarded.
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
rotation reports `widthLengthFit: true` without `pending_F19`. A visitor that
exceeds the envelope in both orientations reports `widthLengthFit: false` and
`dock_hangar_size_limit` at the active docking severity (v3: error), with measured
dimensions and limits, without `pending_F19`. Only a fit requiring rotation
remains uncertified with `pending_F19`; no dock can pass by ignoring that pending
fit. No visitor rotation or airlock picker exists.

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
```

</details>


<details>
<summary>Loader-stage snapshot at the main #62 squash</summary>

This snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern.
No owner decisions are added and no FINDINGS IDs are renumbered.

~~~text
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

`SchemaSet::validate`, `import_definition`, and `load_rules` return at most 128
issues, including the truncation marker. On the 128th attempted diagnostic for
a returned list, collection stops with 127 ordinary issues and one marker:
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
Missing systems, overlaps, hull access, and carried-fit legality remain for
#50; migration never repairs them.
Rustbucket remains available for later structural rejection.

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
~~~

</details>


<details>
<summary>Historical r2 hangar and numeric wording before r4</summary>

This text is retained verbatim as merge evidence. The current r4 contract
above governs the active epsilon on the computed diagonal; exact unrotated
input bounds, minimum-span rejection, and deck limits remain exact. No owner
rotation decision is assigned and no FINDINGS ID is renumbered.

~~~text
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

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.
~~~

</details>

````````

</details>


<details>
<summary>Integrated performance aa456a9d: complete preserved documentation</summary>

This complete performance-parent text supplements the deck-authoring contract
and retains its current performance, regression-test and exact Mull-gate
requirements. Earlier implementation-status snapshots inside it are historical.
Every finding and both parents documentation are retained without renumbering.

`````````text
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

`SchemaSet::validate`, `import_definition`, `load_rules`, `validate`, `dock`, and `perf`
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
Structural issue collection, carried-craft diagnostics, performance formulas and
warning-only advisories, and docking's structural details use this same shared bound; no unbounded issue list is accumulated before
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
`jumpReady` stays null in this structural operation; use `perf` for performance
and readiness.
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
length, and deck limits and enforce the exact deck limit. Unrotated fit uses
exact input bounds, `w <= W && l <= L`, and reports `widthLengthFit: true`
without `pending_F19`. A definite size error requires a rotation-proof bound:
the exact input-bound comparison `min(w,l) > min(W,L)` or the computed diagonal
comparison `max(w,l) > hypot(W,L) + numeric.epsilon`, where `w,l` are visitor
dimensions and `W,L` are hangar limits. The diagonal uses the same absolute
epsilon convention as `carried_fit`; equality or excess within epsilon does not
establish a diagonal rejection. A visitor exceeding either bound reports
`widthLengthFit: false` and
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
tolerance. The `carried_fit` width/length checks and hangar's computed diagonal
reject only when `actual > maximum + numeric.epsilon`. Hangar unrotated fit,
minimum-side rejection, and deck limits remain exact input-bound comparisons.
Input bounds, IDs, coordinates, versions, and counts are exact: epsilon never
admits an over-limit input. Computed masses are rounded at the
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


## Historical hangar wording before final Argus r2

The following prior F19-related wording is retained verbatim as superseded
merge evidence. The current docking section above governs: a definite size
failure needs the rotation-proof minimum-span or diagonal bound; failure in
both axis-aligned orientations is insufficient. No rotation rule is chosen
by this historical excerpt.

~~~text
Ports have no size fields or envelope checks. Hangars report active width,
length, and deck limits and enforce the deck limit. A visitor that fits without
rotation reports `widthLengthFit: true` without `pending_F19`. A visitor that
exceeds the envelope in both orientations reports `widthLengthFit: false` and
`dock_hangar_size_limit` at the active docking severity (v3: error), with measured
dimensions and limits, without `pending_F19`. Only a fit requiring rotation
remains uncertified with `pending_F19`; no dock can pass by ignoring that pending
fit. No visitor rotation or airlock picker exists.
~~~


<details>
<summary>Loader-stage snapshot at the main #62 squash</summary>

This snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern.
No owner decisions are added and no FINDINGS IDs are renumbered.

~~~text
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

`SchemaSet::validate`, `import_definition`, and `load_rules` return at most 128
issues, including the truncation marker. On the 128th attempted diagnostic for
a returned list, collection stops with 127 ordinary issues and one marker:
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
Missing systems, overlaps, hull access, and carried-fit legality remain for
#50; migration never repairs them.
Rustbucket remains available for later structural rejection.

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
~~~

</details>


## Historical numeric wording before Argus round 4

The following prior hangar and numeric-comparison paragraphs are preserved
verbatim as superseded merge evidence. The current docking and numeric sections
above govern: input dimensions/deck counts remain exact, while the computed
hangar diagonal uses active absolute numeric.epsilon. This records the reviewed
comparison correction and assigns no allowed docking rotation or owner rule.

~~~text
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

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.
~~~


## Main integration archive and required CI (#66)

The following complete main-side documentation is retained verbatim at this
plain merge. The current performance API described above remains active.
The required Mull exact-ratio and survivor-identity gate and C++ lint from
main apply to this integrated branch. Historical deferrals below stay
historical. No F finding was renumbered; repeated IDs retain their existing
meaning and text from the two parents.

<details>
<summary>Main-side documentation preserved at the #66 integration</summary>

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
length, and deck limits and enforce the exact deck limit. Unrotated fit uses
exact input bounds, `w <= W && l <= L`, and reports `widthLengthFit: true`
without `pending_F19`. A definite size error requires a rotation-proof bound:
the exact input-bound comparison `min(w,l) > min(W,L)` or the computed diagonal
comparison `max(w,l) > hypot(W,L) + numeric.epsilon`, where `w,l` are visitor
dimensions and `W,L` are hangar limits. The diagonal uses the same absolute
epsilon convention as `carried_fit`; equality or excess within epsilon does not
establish a diagonal rejection. A visitor exceeding either bound reports
`widthLengthFit: false` and
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
tolerance. The `carried_fit` width/length checks and hangar's computed diagonal
reject only when `actual > maximum + numeric.epsilon`. Hangar unrotated fit,
minimum-side rejection, and deck limits remain exact input-bound comparisons.
Input bounds, IDs, coordinates, versions, and counts are exact: epsilon never
admits an over-limit input. Computed masses are rounded at the
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

## Superseded original #66 contract text

The following verbatim original contract from `27b1e3b` is retained as history,
including the former F22 encoding choices and F18/F19 scope descriptions. The
reviewed current contract above is authoritative; the historical text does not
settle an open finding or override a correction. No F finding was renumbered.

<details>
<summary>Historical contract before the current #63 integration</summary>

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
`jumpReady` stays null until #51 evaluates rule-defined performance checks.
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

</details>


<details>
<summary>Historical r1 text retained at the r2 integration</summary>

The following pre-r2 text is preserved verbatim from the Mull branch. It is
superseded by the current contract and reviewed F19/F23 rotation-proof bounds
and terminal-truncation behavior above; it assigns no current owner decision.
No FINDINGS IDs were renumbered.

```md
## Structural validation and docking (#50)

`validate(bytes, rules, schemas, ValidationOptions{}, InputLimits{})` accepts the
same v3/v4 blueprint or definition inputs as import. It returns the DESIGN.md
result envelope with active rules identity, sorted structured issues, and
`data.definition` plus migration details if normalization succeeds. Structural
errors retain the inspectable definition. `data.structuralPass` and the envelope
status describe the enabled structural checks. Cached validation is discarded.
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
rotation reports `widthLengthFit: true` without `pending_F19`. A visitor that
exceeds the envelope in both orientations reports `widthLengthFit: false` and
`dock_hangar_size_limit` at the active docking severity (v3: error), with measured
dimensions and limits, without `pending_F19`. Only a fit requiring rotation
remains uncertified with `pending_F19`; no dock can pass by ignoring that pending
fit. No visitor rotation or airlock picker exists.

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
```

</details>


<details>
<summary>Loader-stage snapshot at the main #62 squash</summary>

This snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern.
No owner decisions are added and no FINDINGS IDs are renumbered.

~~~text
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

`SchemaSet::validate`, `import_definition`, and `load_rules` return at most 128
issues, including the truncation marker. On the 128th attempted diagnostic for
a returned list, collection stops with 127 ordinary issues and one marker:
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
Missing systems, overlaps, hull access, and carried-fit legality remain for
#50; migration never repairs them.
Rustbucket remains available for later structural rejection.

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
~~~

</details>


<details>
<summary>Historical r2 hangar and numeric wording before r4</summary>

This text is retained verbatim as merge evidence. The current r4 contract
above governs the active epsilon on the computed diagonal; exact unrotated
input bounds, minimum-span rejection, and deck limits remain exact. No owner
rotation decision is assigned and no FINDINGS ID is renumbered.

~~~text
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

Computed comparisons use finite IEEE-754 binary64 values and absolute
`abs(a-b) <= numeric.epsilon` (currently 0.000001), without relative
tolerance. Input bounds, IDs, coordinates, versions, and counts are exact:
epsilon never admits an over-limit input. Computed masses are rounded at the
reference's direct-total boundaries to `numeric.precision` decimal places
(currently 6), nearest with ties toward positive infinity. Supported precision
is 0–15; overflow is an error. Other direct totals retain binary64 arithmetic.
Input numbers are not rounded on import.
~~~

</details>

</details>

`````````

</details>
