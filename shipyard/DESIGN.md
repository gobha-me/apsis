# Shipyard design note

Status: proposed for [#47](https://github.com/gobha-me/apsis/issues/47).
Argus reviews this note; Talos reviews the container and deploy section before
any implementation code lands. These reviews are still required. This change
adds documentation only. It does not add fixtures, code, tests, or CI changes.

## Purpose and authority

Build a stable, headless ship and station design tool under `shipyard/` that bots
can drive through JSON, a CLI, and HTTP. It must build definitions, validate them,
report performance, check docking, and produce previews without an editor.
Keep the first service stateless and synchronous. A UI, WebAssembly, gameplay
simulation, combat, and deployment automation are outside this first step.
WebAssembly remains optional and undecided.

The owner owns game rules and design; unresolved questions go to Luna. The
versioned rules file is the single source of truth for dimensions, costs, masses,
formulas, defaults, limits, and validation severities. Stock ship builds are ours.
Procedural stations must pass exactly the same validator as hand-built stations.
Log prototype defects and unexplained differences in [FINDINGS.md](FINDINGS.md)
and surface questions for Luna in the relevant PR. Do not copy sandbox numbers
into the rules to make comparisons pass. Reconciliation belongs to
[#56](https://github.com/gobha-me/apsis/issues/56).

## Core language and boundaries

Choose **C++23**, matching the requested `galaxy/` direction and the existing
native simulation and tools. On the main snapshot used for this note, `galaxy/`
is not present; `sim/CMakeLists.txt` and `tools/CMakeLists.txt` already require
C++23 and CI builds with g++-13. Use an independent CMake entry point under
`shipyard/`; do not couple authoring logic to simulation internals.

One core library owns loading, normalization, rules evaluation, validation,
performance, docking, and the definition model. CLI and HTTP adapters call the
same operations and serialize the same results. The renderer consumes that
model; it does not recalculate legality or maintain a second part catalogue.
The generator produces ordinary definitions and calls the same validator.
There is no browser, JavaScript runtime, or network dependency in the core.

Use small, reviewed dependencies for JSON/schema handling, HTTP, PNG encoding,
and ZIP output where needed. Select their versions and licenses in their scoped
implementation PRs. Do not introduce a framework, database, job queue, plugin
runtime, or general expression language. Rules formulas use a bounded set of
operators with checked operands and arity, never executable code.

Proposed organization, to be created by later issues:

| Location | Responsibility |
| --- | --- |
| `shipyard/include/`, `shipyard/src/` | Core and thin executable adapters |
| `shipyard/rules/`, `shipyard/schemas/` | Authoritative v4 data and JSON Schemas |
| `shipyard/stock/` | Our editable stock ship/station blueprints |
| `shipyard/fixtures/v3/` | Byte-identical historical inputs and hash manifest (#48) |
| `shipyard/tests/` | Independent tests and approved v4 expectations |
| `shipyard/DESIGN.md`, `shipyard/FINDINGS.md` | Contract and decision/discrepancy log |

Load and validate one rules snapshot at startup. Each result identifies its
schema, rules version, and SHA-256 of the exact rules bytes. HTTP clients cannot
replace installed rules per request; a CLI rules-file option is explicit and
validates the entire replacement. Never trust imported `derived`, `validation`,
or embedded `rules` as authority. Recompute from the blueprint and active rules.

## CLI and HTTP contract

Use `shipyard <operation>` and `POST /v1/<operation>` for the six operations
below. `/v1` versions the transport contract independently of v4 data schemas.
The CLI accepts UTF-8 JSON from `--input FILE` or stdin (`--input -`); HTTP
accepts `application/json`. File access is a CLI concern. HTTP accepts embedded
definitions and stock identifiers, never caller-selected server paths or URLs.

| Operation | Input and behavior | Result data |
| --- | --- | --- |
| `build` | Exactly one source: blueprint/definition, stock ID, or blank design options. Normalize, derive totals, and run validation. Blank defaults to a ship; station creation is explicit. Bots can author blueprint JSON directly; no editing script language is required. | v4 definition and structural validation report, including warnings and repair issues |
| `validate` | Blueprint or definition; check schema, hull, parts, required systems, crew routes, adjacent-deck ladders, station modules, carried craft, and approaches using active rules. | PASS/FAIL and structured issues; recomputed definition when normalization succeeds |
| `perf` | Normalized design and optional load fractions; report current, empty, half, and full scenarios. | Thrust/mass, acceleration, jump range, propellant/endurance, jump charges, and supply/demand at idle, cruise, and jump; advisory issues |
| `dock` | Host station, target deck index/module ID, and visiting ship. Select a visitor airlock when needed; report the selection and rule-defined alignment/clearance checks. | PASS/FAIL, structured issues, and measured check details; hangar dimensions/decks only for hangars |
| `render` | Valid design plus bounded render options (deck/stack, dimensions). | One PNG per deck, `stack.png`, a manifest, and a stack ZIP containing those artifacts |
| `selftest` | Installed, versioned example suite; no arbitrary test code supplied by callers. | Per-case expected/actual results and aggregate PASS/FAIL |

Later `build` may accept a station-generator source with an explicit seed and
generator version (#54). Until implemented, return `unsupported_operation` for
that source. Likewise an API delivered before the renderer (#53) must return a
structured unavailable result for rendering, never a successful empty preview.
Seeds, generator version, and rules identity determine generated output; any
invalid generated station is a generator bug, with no validator bypass.

Every operation returns one JSON envelope with `schema` (initially
`apsis-shipyard-result-v1`), `operation`, `status` (`PASS` or `FAIL`), `pass`
(boolean), `rulesVersion`, `rulesSha256`, `issues`, and `data` (nullable).
An issue has a stable `code`, `severity`, readable `message`, and a JSON Pointer
`path` when applicable; details may identify a deck, module, or measurement.
Sort issues by path, code, and severity. Clients branch on codes, not prose.
If rules cannot load, identity fields are null and the operation fails clearly.

Structural errors fail a build. Performance findings are warnings and never
block an otherwise legal build, including low thrust, inadequate power, or absent
jump fuel. Malformed input or invalid rules still fail `perf`; an unfavorable
performance value does not. Docking incompatibility fails `dock`. A self-test
passes when actual behavior matches expectations, including expected rejection
of a negative fixture. `ready`, `jumpReady`, and theoretical range are distinct:
a range number alone does not mean the ship has fuel for a jump.

CLI exit codes: 0 for PASS (warnings allowed), 1 for domain FAIL or a self-test
mismatch, 2 for usage/input/schema errors, and 3 for unavailable operations or
internal failures. Write only the envelope to stdout and diagnostics to stderr.
Rendering writes files only to an explicit `--output-dir`; stdout describes
their names, media types, dimensions, and hashes. HTTP returns the same metadata
and base64 artifact bytes in `data`, within an output limit; no artifact store or
download service is needed initially. Binary output never corrupts JSON stdout.

HTTP returns 200 for evaluated domain results, including validation/docking
FAIL; 400 for malformed requests, 413 for oversized input, 422 for schema-invalid
input, 501 for an unavailable operation, 503 for capacity/readiness failure, and
500 for internal errors. JSON errors use the envelope. Do not expose exception
stacks, filesystem paths, or input contents in public diagnostics or access logs.
Provide `GET /healthz` for process liveness and `GET /readyz` for successfully
loaded rules/assets. Readiness does not run a costly self-test per request.

Start with one request at a time and explicit configurable input/output byte,
JSON nesting, render-pixel, and execution-time limits. Use the same input limits
in both adapters. Game limits still come from the rules file; operational caps
are separate service settings. Reject work exceeding a cap with a structured
resource error. Defaults must be measured and documented with #52/#53 and reviewed
by Talos for deployment, rather than invented as balance constraints here.

## v4 schemas and v3-to-v4 migration

Define JSON Schemas for `apsis-ship-rules-v4` (`version: 4`),
`apsis-shipyard-v4` blueprints, and `apsis-ship-definition-v4` wrappers (#49).
Use JSON Schema Draft 2020-12; validate schema structure and then apply the
active rules for semantic constraints. Do not duplicate balance constants in
schemas or allow silent acceptance of unknown versions/fields. Reject duplicate
JSON keys, nonfinite numbers, invalid IDs, coordinates, rotations, types, and
excessive recursion before calculations.

Retain the familiar definition wrapper: `schema`, `blueprint`, `derived`,
`validation`, and `rules`. The blueprint retains `name`, `buildType`, `budget`,
`loads`, ordered `decks`, `nextId`, and `migrationNotes`. Decks retain named floor
maps and module lists; modules retain IDs, part types, coordinates, rotations,
interfaces, and optional carried blueprints. Add explicit `rulesVersion` and
`rulesSha256` to the v4 wrapper; embedded `rules` is an informational snapshot of
the active rules. `buildType` distinguishes ships and stations without a separate
station validator. JSON Schemas describe input shape; the core checks occupancy,
connectivity, reachability, and rule-dependent relationships.

The following owner decisions take effect in the v4 rules file and migration,
and in the v4 validator. They do not change v3 golden fixtures:

1. Jump drive price scales with range: 0.5 J = 1,500 cr, 1.0 J = 2,500 cr, 3.0 J = 7,500 cr.
2. Docking ports check only airlock alignment and approach clearance, with no size limit, so a 20x34 m ship fits a port. Only hangars limit size, by width, length and deck count.

The v3 0.5 J price is 2,500 cr; v4 uses 1,500 cr. The 1.0 J and 3.0 J anchors
already match. V3 also has 1.5 J and 2.0 J drives: do not infer interpolation or
change their provisional values without Luna's confirmation (F01).
Remove per-port width, length, and deck caps from v4 rules and all port checks;
do not merely raise the numbers. Hangars retain rule-defined width, length, and
deck-count limits. Global definition limits still apply through the validator.
The precise airlock alignment convention needs Luna's clarification (F03);
interface-label equality alone must not silently become that decision.

Migration is an explicit, deterministic adapter, available to `build` and all
operations accepting definitions. It reports source/target versions and changes:

1. Recognize a v3 blueprint or definition wrapper; validate its source shape.
   Extract the blueprint, treating cached totals, validation, and embedded rules
   as historical input, never executable authority.
2. Preserve ship/station mode, geometry, deck order and user names, module IDs,
   interfaces, carried-craft geometry, budget, and load fractions. Recursively
   migrate carried craft within the rules' nesting limit. Do not add systems,
   resize ships, renumber IDs, or rearrange a failing design to make it pass.
3. Apply the v4 schema identifiers and active v4 rules, including both owner
   decisions above. Recompute all totals, prices, performance, readiness, and
   docking outcomes. Record rule-identity differences and migration notes.
4. Emit a v4 definition plus issues, retaining an inspectable draft when it has
   domain errors. Reject malformed source data without silently dropping fields.
   Reimporting the exported v4 definition must preserve semantic equivalence and
   not append duplicate migration notes. Export v4 only; no lossy v3 downgrade.

Keep v3 files byte-identical under the fixture plan in #48, including the rules
and original self-test report. Store approved v4 expectations separately with
the rule/decision explaining each changed outcome. A v3-to-v4 price change may
affect budget results when budget enforcement is explicitly selected; preserve
the rules' default funding policy. Do not promise all historical examples pass:
Rustbucket has structural errors as well as performance warnings (F07).
Older v1/v2 migration is outside this first adapter; return an actionable
unsupported-version error until a separate approved compatibility change exists.

Use stable JSON serialization (ordered object keys, preserved deck order and
module IDs), locale-independent numbers, and the rules' precision/epsilon
policy. Define the exact numeric comparison and canonicalization contract in
#49 with independent tests; keep unavailable quantities explicit rather than
writing NaN or inventing zero after a calculation failure (F10).

## Headless rendering

Implement a CPU renderer over the normalized model (#53), with no DOM, GPU, or
interactive editor requirement. Use rule-sourced footprints, fixed camera fit,
fixed animation time, explicit dimensions, bundled font/assets, and stable deck
order. Each manifest identifies renderer version, rules hash, definition hash,
render options, and artifact hashes. Filenames use deck indices and sanitized
names; ZIP entry order and timestamps are fixed. A preview is also the screenshot
surface for bots; a browser page screenshot is not required for this stage.

Require identical repeated artifacts in the supported pinned runtime. Document
any allowed platform pixel variance with a bounded comparison in the renderer
PR; semantic geometry and manifest identity remain exact. Compare the prototype
previews visually where useful, but its manifest describes an edited five-deck
Kestrel, not all five canonical designs. Preserve unexplained differences as
findings. Preview PNGs and UI screenshots stay reference material for now; #48
imports only the seven specified JSON goldens, not image archives or the HTML.

## Container and deploy shape — Talos review required

**Talos must review this section before any implementation code lands. Argus
must review the design note. This proposal authorizes no deployment.**

For #57, propose a multi-stage native image: build the standalone C++23 targets
and run their checks in a builder, then copy only runtime binaries, validated
v4 rules, schemas, stock designs, renderer assets, and a compact versioned
self-test suite with approved expectations into a small runtime image.
Select and pin the base image/dependencies in that reviewed work. Support both
one-shot CLI execution and `shipyard serve`; exclude compilers, prototype HTML,
source archives, and the full historical v3 fixture corpus from the service image.

Propose a non-root runtime with read-only root filesystem/assets and a bounded
writable temporary volume for rendering. Clean request scratch on success,
failure, and shutdown. Initial HTTP listens on configurable port 8080; local
standalone use binds loopback by default, container use explicitly binds the
service interface. No database, persistent volume, outbound fetching, or direct
cluster access is needed. SIGTERM stops accepting work and drains bounded work.

The deploy request to Talos must justify the bot service, port, measured CPU,
memory and scratch bounds, concurrency/timeouts, liveness/readiness probes,
network access, and lack of persistence. Include the immutable image digest,
rules/assets identities, successful CI/self-test evidence, and rollback to the
previous digest. Talos decides ingress, TLS/authentication, access policy,
resource allocations, release and rollback, and performs every deploy. The
service must not be publicly exposed before that access policy is approved.
No cluster commands, manifests applied to a cluster, registry publishing, deploy
secrets, or automated release triggers are part of this documentation PR.

## Test strategy and delivery gates

Implementation and test authoring come from separate sessions. One agent never
writes both a change and its tests. The implementation session provides its
contract, findings, and validation evidence; an independent test session authors
the tests and expected results. Implementation authors may run those tests and
fix code, but must not weaken their expectations. Review discrepancies against
the rules, involve Luna for game semantics, and land implementation with its
independent verification before declaring that capability complete.

| Area | Independent checks |
| --- | --- |
| Historical goldens (#48) | SHA-256 manifest protects the seven v3 JSON files; compare import bytes to the reference; do not regenerate expected v3 results |
| Loader/migration (#49) | Malformed rules/formulas, unknown fields/versions, numeric and nesting bounds; all five canonical definitions; preservation of geometry/IDs/names/loads; v4 import/export equivalence and migration idempotence |
| Validator (#50) | Overlap, missing floors/systems, disconnected hull, crew routes, ladder pairing and deck reorder, station modules, carried craft; exact expected errors and warning severities |
| Owner decisions | Price anchors; 20x34 m ship at a port; a valid visitor exceeding former port caps; blocked/misaligned airlock; hangar width, length and deck boundaries, including allowed rotations after rule clarification |
| Performance (#51) | Independently calculated thrust/mass, empty/half/full/current range, separate fuel pools and charges, idle/cruise/jump power; unfavorable figures remain advisory |
| Adapters (#52) | CLI/HTTP parity, exit/status mapping, clean JSON stdout, malformed/oversized input, no request path/URL loading, and import/export round trip |
| Renderer (#53) | PNG dimensions/deck count, manifest and ZIP contents, repeated-output hashes, geometry checks and approved pixel comparisons |
| Generator (#54) | Same seed/version/rules repeats exactly, call-order/thread independence if relevant, varied seeds and boundaries; every result uses the ordinary validator |
| Nits/container (#55/#57) | Blank ship default and explicit station, generated deck names match level; image starts as non-root, readiness and bounded rendering/shutdown work |

The prototype self-test is evidence, not a complete test oracle: it covers five
embedded templates and omits Rustbucket. Preserve its report; define a separate
v4 suite that includes negative cases. Rustbucket's power warning must not be
turned into an error, nor its missing-airlock error into a warning to make a
nominally underpowered example pass.

Feature and test PRs may include `ci.yml` install, build, lint, and test-run setup
steps that exist only to run tests; those changes count as tests and must obey
the separate-session rule. Changes to workflow triggers, permissions, secrets,
deploys, or action pins require their own small PR and the owner's OK. Retain
the single protected `main` and existing required `ci` check. This PR makes no
workflow edits; run the existing CI and wait for its conclusion.

Delivery sequence: approved design and Talos review first; byte-preserved goldens
may proceed in parallel (#48); rules/schemas/migration (#49); validator and
performance (#50/#51); adapters, renderer and generator as their dependencies
become available (#52–#54); nits and reconciliation (#55/#56); image and deploy
request (#57). Keep each PR small, attach independent test evidence, and require
Talos to perform the eventual deployment.
