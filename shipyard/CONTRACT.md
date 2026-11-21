# Loader contract

## Input, formulas, and schemas

Strict UTF-8 JSON rejects duplicate keys after escape decoding, nonfinite
numbers, unknown versions/authoritative fields, invalid shapes, and excessive
nesting before totals. Defaults are operational parser bounds of 8 MiB per
document and 64 nested containers; callers can set `InputLimits`. These are
library resource guards, not game limits or deployment sizing. Schema checking
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
