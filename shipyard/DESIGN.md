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

Choose **C++23**, matching `galaxy/`, now on main, and the existing native
simulation and tools. `sim/CMakeLists.txt` and `tools/CMakeLists.txt` already
require C++23 and CI builds with g++-13. Use an independent CMake entry point under
`shipyard/`; do not couple authoring logic to simulation internals.

One core library owns loading, normalization, rules evaluation, validation,
performance, docking, and the definition model. CLI and HTTP adapters call the
same operations and serialize the same results. The renderer consumes that
model; it does not recalculate legality or maintain a second part catalogue.
CLI and HTTP always call validation and docking with default `ValidationOptions`
and `DockOptions`. They never map request fields onto check switches. The public
types remain available for existing independent real-library tests; adapters
must not expose that test seam as a way to skip structural or docking checks.
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
| `build` | Exactly one source: blueprint/definition, stock ID, or blank design options. Normalize, derive totals, and run validation. Blank defaults to a ship; station creation is explicit. Pending-F20 formula cases must not reject a blank build; return the definition and structural validation report without assigning those formula results. Bots can author blueprint JSON directly; no editing script language is required. | v4 definition and structural validation report, including warnings and repair issues |
| `validate` | Blueprint or definition; check schema, hull, parts, required systems, crew routes, adjacent-deck ladders, station modules, carried craft, and approaches using active rules. | PASS/FAIL and structured issues; recomputed definition when normalization succeeds |
| `perf` | Normalized design and optional load fractions; report current, empty, half, and full scenarios. | Thrust/mass, acceleration, jump range, propellant/endurance, jump charges, and supply/demand at idle, cruise, and jump, subject to the F20 gate below; issues with rule-defined severity (default `warning`) |
| `dock` | Host station, target deck index/module ID, and visiting ship. Evaluate all F18 checks using any compatible matching visitor airlock meeting F03; the tool does not pick an airlock. | PASS/FAIL, structured issues, and measured check details; hangar dimensions/decks only for hangars |
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

Structural errors fail a build. The rules file sets validation severity; the
default for performance checks is `warning`, including low thrust, inadequate
power, or absent jump fuel. A performance finding fails a build when the active
rules give it error severity; warnings remain non-blocking. Malformed input or
invalid rules still fail `perf`. An undefined or invalid formula rejects the design
with a clear structured error, including during performance evaluation (F10).
Until Luna answers F20, this rejection excludes these evaluations: in zero-mass
blank ships and blank stations, `acceleration` and `range` division by `mass`,
`deltaV` division by `endMass` and ln(0), and `fuelToDryMassRatio` division by
`dryMass`; in engine-less designs, including stations, full/current `burnSeconds`
and `exhaustVelocity` division by zero `burnRate`. Open for Luna, not guessed.
Do not assign a result or handling rule here.
Docking incompatibility fails `dock`. A self-test
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
Provide `GET /healthz` for process liveness and `GET /readyz` for loaded,
SHA-verified rules/assets, available capacity, and a service that is not draining.
Readiness does not run a costly self-test per request.

Start with one request at a time and explicit configurable input/output byte,
JSON nesting, render-pixel, and execution-time limits, with finite default
request-size and execution-time limits. Use the same input limits
in both adapters. Game limits and technical input sanity limits come from the
rules file, with distinct labels; operational caps are separate service settings.
The library itself has no wall-clock budget; the HTTP layer must enforce the
request time limit (#52). HTTP request threads need at least 1 MB of stack for
the loader's bounded recursive checks (#52).
Use the technical input caps as interpreted below from F09's owner picks;
the shipyard does not enforce player affordability. Reject work exceeding a cap
with a structured resource error. Defaults must be measured and documented with #52/#53 and reviewed
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

The following owner picks, recorded on 2026-11-20 via Luna, take effect in
the v4 rules file and migration, and in the v4 validator. They do not change v3
golden fixtures:

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Jump drives: scale price with range, 1,500 / 2,500 / 7,500"

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F01: 1.5 J = 3,750, 2.0 J = 5,000"

**Interpretation (F01):** The 4:53 PM options were for 0.5 / 1.0 / 3.0 J.
Together the picks set 0.5 J = 1,500, 1.0 J = 2,500, 1.5 J = 3,750,
2.0 J = 5,000 and 3.0 J = 7,500 cr.
The v3 0.5 J, 1.5 J, and 2.0 J prices are respectively 2,500, 5,000, and
7,500 cr; v4 uses 1,500, 3,750, and 5,000 cr. The 1.0 J and 3.0 J prices
already match. Use these five explicit prices without inferring interpolation.

Owner pick, 2026-11-20 4:53 PM ET (Luna question card): "Docking: yes, ports check airlock and clearance, only hangars limit size"

**Interpretation (F02):** The question was whether a 20×34 m ship should fit a
docking port. It may pass if every F18 check passes; the size decision does not
remove structural or crew-reachability checks.
Remove per-port width, length, and deck caps from v4 rules and all port checks;
do not merely raise the numbers. Hangars retain rule-defined width, length, and
deck-count limits. Global definition limits still apply through the validator.

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F03: same deck, exterior, facing out, within 1 cell"

**Interpretation (F03), pending F21:** The v4 model must expose the information
needed to check same deck, exterior position, outward facing, and "within 1 cell".
Which visitor deck is "the same deck", the distance measure, and its reference
point remain Open for Luna, not guessed (F21). Treating the 1-cell bound as
inclusive is an interpretation pending that answer, not a settled rule. The
validator and `dock` require all four owner-picked conditions once clarified;
interface-label equality alone is insufficient. These conditions are part of
F18's complete port-dock pass rule below. Hold-specific
docking/airlock rules remain open in F17.

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F07: Rustbucket stays a deliberate failing example"

**Interpretation (F07):** Keep Rustbucket as a known-bad negative fixture the
validator must reject; migration must not repair it to make it pass.

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F09: rules file wins, 80 chars / 100M"

Jeff's typed question, 2026-11-20 5:14 PM ET (Luna question card): "Budget limits I thought it would be up to whatever could be afforded?"

Owner pick, 2026-11-20 5:14 PM ET (Luna question card): "Keep 100M as a technical sanity limit only; in game, budget is what the player can afford"

**Interpretation (F09):** The 80-character name limit stands. The 100M cr value
is a technical input sanity cap, not a game budget. In v4 rules, put these
fields under technical limits: `technicalLimits.maxBlueprintNameCharacters`
(80) and `technicalLimits.maxBudgetInputCr` (100000000). Label the latter as a
technical sanity cap on budget input, never a game budget or affordability rule.
The core, CLI/HTTP adapters, and future UI read these same fields; schemas must
not introduce competing limit constants. Preserve valid imported names and
budget values without silent truncation or clamping. Reject inputs beyond the
rules' sanity limits with a clear field error. The shipyard reports costs and
retains budget as planning input; it does not enforce player affordability.
The decision whether a player can afford the build belongs to the sim (F09).

Owner pick, 2026-11-20 5:12 PM ET (Luna question card): "F10: reject with an error"

**Interpretation (F10):** An undefined or invalid formula rejects the design
with a clear error, never a silent zero. Pending Luna's answer, F20 excludes the
zero-mass blank-ship/station evaluations (`acceleration` and `range` on `mass`,
`deltaV` on `endMass` and ln(0), `fuelToDryMassRatio` on `dryMass`) and the
engine-less evaluations (full/current `burnSeconds` and `exhaustVelocity` on
zero `burnRate`, including stations). Open for Luna, not guessed. Do not assign
a result or handling rule here.

Migration is an explicit, deterministic adapter, available to `build` and all
operations accepting definitions. It reports source/target versions and changes:

1. Recognize a v3 blueprint or definition wrapper; validate its source shape.
   Extract the blueprint, treating cached totals, validation, and embedded rules
   as historical input, never executable authority.
2. Preserve ship/station mode, geometry, deck order and user names, module IDs,
   interfaces, carried-craft geometry, budget, and load fractions. Recursively
   migrate carried craft within the rules' nesting limit. Do not add systems,
   resize ships, renumber IDs, or rearrange a failing design to make it pass.
3. Apply the v4 schema identifiers and active v4 rules, including the owner
   decisions above. Recompute all totals, prices, performance, readiness, and
   docking outcomes. Record rule-identity differences and migration notes.
4. Emit a v4 definition plus issues, retaining an inspectable draft when it has
   domain errors. Reject malformed source data without silently dropping fields.
   Reimporting the exported v4 definition must preserve semantic equivalence and
   not append duplicate migration notes. Export v4 only; no lossy v3 downgrade.

Keep v3 files byte-identical under the fixture plan in #48, including the rules
and original self-test report. Store approved v4 expectations separately with
the rule/decision explaining each changed outcome. A v3-to-v4 price change
changes reported costs, without adding game budget ceilings or player
affordability checks. **Interpretation (F07):** Rustbucket remains a known-bad
negative fixture: the v4 validator must reject its structural errors. Its
performance findings use rules-set severity, with `warning` as the default for
performance checks. Migration must not repair it to make it pass.
Older v1/v2 migration is outside this first adapter; return an actionable
unsupported-version error until a separate approved compatibility change exists.

Use stable JSON serialization (ordered object keys, preserved deck order and
module IDs), locale-independent numbers, and the rules' precision/epsilon
policy. Define the exact numeric comparison and canonicalization contract in
#49 with independent tests.

**Interpretation (F10):** Reject malformed formulas, unknown variables or
operators, and invalid arity when loading rules. If evaluation encounters a
missing variable, division by zero, invalid logarithm input, nonfinite result,
or another undefined outcome, reject the design with a clear error identifying
the formula's rule/path. Until Luna answers F20, F10 rejection excludes exactly:

- In zero-mass blank ships and blank stations: `acceleration` and `range`
  division by `mass` = 0; `deltaV` division by `endMass` = 0 and ln(0);
  `fuelToDryMassRatio` division by `dryMass` = 0.
- In engine-less designs, including stations: full/current `burnSeconds`
  (`fuel/burnRate`, twice) and `exhaustVelocity` (`thrust/burnRate`, once)
  division by `burnRate` = 0.

Wayfarer Anchorage encounters the three `burnRate` evaluations; each blank ship
and blank station also encounters the zero-mass and undefined-log cases. These
pending-F20 formula cases must not reject a blank `build`. Open for Luna, not
guessed. Do not assign a result or handling rule here. Until Luna answers, #51
must hold on F20 by returning a structured `pending F20` issue rather than
outputting excluded values; do not invent numeric stand-ins.
For other formula failures, never substitute zero, NaN, or a successful
unavailable value. Legitimate computed zero-thrust/no-drive cases remain
distinct from formula errors (F10).

## Port-dock checks and visitor airlock (F18)

**Closed by owner decision, 2026-11-20, via Luna.** Luna's questions and
readbacks are her wording; Jeff's affirmation below is the owner decision.

Luna's dock-check question:

Luna's question, 2026-11-20 5:24 PM ET (Luna question card): "Keep the prototype's other dock checks (structural faults, crew can reach the port, matching airlock)?"

Jeff's typed reply/comment:

Jeff's typed reply/comment, 2026-11-20 5:24 PM ET (Luna question card): "Test"

Luna's readback:

Luna's readback, 2026-11-20 5:25 PM ET (Luna question card): "keep them and test each one"

Luna's visitor-airlock question:

Luna's question, 2026-11-20 5:25 PM ET (Luna question card): "Which airlock does the visiting ship use?"

Jeff's typed reply/comment:

Jeff's typed reply/comment, 2026-11-20 5:25 PM ET (Luna question card): "Autodock? Docking could be hard and damage both ... Thoughts"

**Interpretation of Luna's proposal:** The shipyard answers whether the ship
can dock at all, using the checks and any compatible matching airlock, without
picking an airlock in the tool. Autodock versus manual flight docking and
possible damage to both ships belong to the game/sim outside the shipyard.

Jeff's owner decision / Owner pick answered three asks from Luna, in order:

1. Luna read Jeff's "Test" as keeping the three v3 checks and testing each one.
2. Send the Shipyard part to Argus and Engine Lead: the tool checks only whether
   the ship can dock, any compatible matching airlock works, and nothing is picked.
3. Autodock and docking damage are saved as a game mechanic for Claude.

The readbacks and proposals are Luna's wording; Jeff affirmed all three:

Owner pick, 2026-11-20 5:25 PM ET (Luna question card): "Yes and yes and yes"

**Interpretation — settled port-dock rule:** For a valid station port target
and visiting ship, `dock` passes only if all four conditions hold:

- (a) No structural fault in either ship (station host or visitor).
- (b) Station crew can reach the port.
- (c) At least one compatible matching visitor airlock meets F03: same deck,
  exterior, facing out, within 1 cell. That airlock must also be crew-reachable
  and clear, as v3 requires; violations tie to check (a) at the rules' `error`
  severity (v3). F21 leaves the visitor-deck mapping, distance measure, and
  reference point open for Luna.
- (d) Approach clearance.

Each check needs its own independent test in #50. Any matching airlock works;
the tool does not pick an airlock. Ports have no size envelope; hangars limit
width, length and deck count (F02). Flight docking and damage are outside the
shipyard, on the sim side. These ordinary port checks do not answer F17's
hold-specific question, which remains open.

Prototype evidence: `index.html:3791-3792` rejects host/visitor structural
errors; `:3636-3640` records visitor-airlock reachability and clearance, and
`:3793` filters for both before `:3800` requires a matching interface; `:3801`
requires station crew reachability. F03 adds geometric alignment conditions,
with their unresolved details in F21; F02 removes the prototype's port size envelope. Issue #50 already
contains these acceptance items and remains open.

## Multi-deck cargo holds

**Owner requirement, 2026-11-20, via Atlas**, recorded in
[#60](https://github.com/gobha-me/apsis/issues/60) (part of #50; depends on
#47, #48, and #49):

one hold spans two or more decks, for tall cargo or vehicles. It counts as a single volume for capacity, mass and walkability, with open space (no floor) between the decks it spans.

The v4 blueprint schema represents each hold with one record identifying the
hold, its footprint, and its deck span covering N >= 2 decks. The span references
the blueprint's decks; it is not a separate cargo module on every deck. The
definition wrapper carries that same blueprint representation. Capacity and
mass are computed once for the entire hold, and walkability treats it as one
volume. Inside its footprint, there is open space with no floor between the
decks it spans; normalization and rendering must preserve that opening.
Validation must distinguish this required opening from an ordinary missing-floor
error, without inferring access or traversal rules.

All unresolved game rules remain pending Luna, not guessed: internal ladders
and access (F11); crossing the opening, edges, railings, and falls (F12); hangar
interfaces or overlap and deck-count limits (F13); partial openings versus a
full span (F14); minimum beyond the required two decks and maximum span (F15);
deck/level numbering, including generated names (F16); and docking/airlock
interactions (F17). The single
walkability volume does not settle how traversal across open space works.
Record any further open point in [FINDINGS.md](FINDINGS.md) as pending Luna
before assigning it a rule.

The prototype has no multi-deck holds, so there is nothing to match from v3.
Leave v3 goldens unchanged. Schema/validator implementation and independent
valid/invalid-span fixtures belong to later sessions for #49/#50/#60, after the
applicable Luna decisions; this note adds no implementation or fixtures.

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

## Container and deploy shape

Talos (Infra Lead) reviewed the container and deploy section at 32ce061b and sent nine requirements to Engine Lead on 2026-11-20, 5:10 PM ET; they are folded in below.
These requirements apply to [#57](https://github.com/gobha-me/apsis/issues/57).
Talos review remains a gate before any implementation code lands; Argus must review the design note.
**This proposal authorizes no deployment. Talos performs every deploy.**

### Image

Use a multi-stage native image: build the standalone C++23 targets and run their
checks in a builder, then copy only runtime binaries, validated v4 rules, schemas,
stock designs, renderer assets, and a compact versioned self-test suite with
approved expectations into a minimal runtime base with no shell or package
manager. Select and pin the base image/dependencies in #57; statically link small
dependencies where practical. Set a numeric non-root `USER`, for example
`65532:65532`, and OCI labels recording the git SHA and rules SHA-256.

Support one-shot CLI execution and `shipyard serve`; exclude compilers,
prototype HTML, source archives, and the full historical v3 fixture corpus from
the service image. Target an image under 100 MB. Images live in the internal registry.
Deploy only by `@sha256` digest, never by tag.

### Pod security

Set `allowPrivilegeEscalation: false`, drop `ALL` capabilities, and use seccomp
`RuntimeDefault`. Set `automountServiceAccountToken: false` and
`readOnlyRootFilesystem: true`. No `hostPath`, `hostNetwork`, or privileged pods.

### Scratch and request caps

Use one disk-backed `emptyDir` mounted at `/tmp`, with a `sizeLimit` of about
`256Mi`, justified from measurements in #57. It must not be memory-backed.
Clean request scratch on success, failure, and shutdown. Document per-request
caps for maximum body size, deck count, PNG dimensions and total pixels, and ZIP
bytes. Require finite default request-size and execution-time limits; an
unset override must still enforce those defaults. Set the numeric caps from
measurements in #57; no values are invented here. Keep these operational caps
distinct from game limits in the rules.

### Concurrency and resources

Use one replica and a fixed worker/concurrency cap, initially one request at a
time as in the HTTP contract. When full, return `503` with
`Retry-After`; do not create an unbounded queue. Enforce a finite default
per-request timeout, including when no override is supplied.
Measure requests and limits in #57: expect about `100m` CPU / `128Mi` memory
requests and at most `500m` / `512Mi` limits. Rendering, including the worst
allowed request, must stay within the limit.

### Probes and shutdown

`/healthz` is liveness only: it performs no work and does not re-read rules.
`/readyz` means rules are loaded and their SHA verified, the service is under the
concurrency cap, and it is not draining. On `SIGTERM`, make readiness false first,
then stop accepting requests, then drain work. State a maximum drain time in
#57, with exact seconds measured and set there, satisfying
`request timeout < max drain time < terminationGracePeriodSeconds`.

### Network

Use a `ClusterIP` Service only, with port named `http` on `8080`. Set the bind
address by flag or environment: `0.0.0.0` in the container, loopback by default
for local standalone use. The service has no built-in auth; access is controlled
by the cluster NetworkPolicy. No Ingress or TLS until the access policy is
approved. Deny egress entirely; the service needs no DNS. Allow ingress only
from the namespaces of the calling bots named in #57; that issue must name them
before deployment, without inventing namespace names here. No outbound fetching
or direct cluster access is needed.

### Placement

Give the service its own namespace with a `ResourceQuota` and `LimitRange`.
No PVC, ConfigMap, or Secrets. Rules are baked into the image, so the image digest
pins the rules identity. No database or persistence is needed.

### Observability

Write JSON logs to stdout with request id, op, status, and duration. Never log
bodies or blueprint contents, and cap log line size. Optional `/metrics` exposes
request counts, latency, in-flight requests, and rejects.

### Deploy request for #57

The request to Talos must justify the bot service, port, measured CPU, memory and
scratch bounds, concurrency/timeouts, probes, network access, and lack of
persistence. Include all of the following:

- CI and self-test evidence for the exact image digest, plus rules/assets identities.
- Image size.
- Measured peak RSS and scratch use under the worst allowed request.
- The previous digest, for rollback.
- Release through Talos only, with no automated deploy trigger.

Talos decides ingress, TLS/authentication, access policy, resource allocations,
release and rollback, and performs every deploy. No cluster commands, manifests
applied to a cluster, registry publishing, deploy secrets, or automated release
triggers are part of this documentation PR.

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
| Owner-pick interpretation | All five drive prices; Rustbucket rejected; 80-character name and 100M cr technical input boundaries read from rules; no shipyard affordability enforcement; undefined/invalid formulas fail clearly except the exact blank ship/station and engine-less evaluations listed in F20 (Open for Luna, not guessed); those pending cases must not reject a blank build; legitimate zero remains valid |
| Docking (#50; F02/F03/F18/F19/F21) | Each F18 condition has its own test: (a) structural faults in either ship, (b) station crew reachability, (c) at least one compatible matching visitor airlock that is crew-reachable and clear (violations tied to (a) at the rules' `error` severity (v3)) and meets same-deck/exterior/outward alignment and the 1-cell bound, (d) approach clearance. Each (a)-(d) test must fail when its own check is disabled, with mutation evidence and no mocking of the unit under test. Treat inclusive 1-cell boundary coverage as an interpretation pending F21's visitor-deck mapping, distance measure, and reference point (Open for Luna, not guessed); finalize at/beyond-bound expectations after that answer. Include a combined pass case, any matching airlock and no tool airlock picking; 20×34 m visitor and a valid visitor exceeding former port caps; hangar width, length and deck boundaries; allowed rotations after rule clarification in F19 (Open for Luna, not guessed) |
| Performance (#51) | Independently calculated thrust/mass, empty/half/full/current range, separate fuel pools and charges, idle/cruise/jump power; rules file sets severity, default for perf checks is `warning`; verify warnings and rule-defined errors; until Luna answers, #51 must hold on F20 by returning a structured `pending F20` issue rather than outputting any excluded values, with no numeric stand-ins |
| Adapters (#52) | CLI/HTTP parity, exit/status mapping, clean JSON stdout, malformed/oversized input, no request path/URL loading, and import/export round trip |
| Renderer (#53) | PNG dimensions/deck count, manifest and ZIP contents, repeated-output hashes, geometry checks and approved pixel comparisons |
| Generator (#54) | Same seed/version/rules repeats exactly, call-order/thread independence if relevant, varied seeds and boundaries; every result uses the ordinary validator |
| Nits/container (#55/#57) | Blank ship default and explicit station, generated deck names match level; image starts as non-root, readiness and bounded rendering/shutdown work; finite default request-size/time limits and NetworkPolicy access control for the service with no built-in auth |

The prototype self-test is evidence, not a complete test oracle: it covers five
embedded templates and omits Rustbucket. Preserve its report; define a separate
v4 suite that includes Rustbucket as a known-bad negative case the validator must
reject. Preserve the historical v3 power warning and missing-airlock error.
For v4, the rules file sets severity and the default for perf checks is `warning`;
do not override active rule severities to force a desired fixture result.

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
