# Independent shipyard tests

These tests exercise the public APIs in `loader.hpp`, `validator.hpp`, and
`perf.hpp`, with no mocks. Expectations
come from DESIGN.md, CONTRACT.md, the owner decisions, and the unchanged v3
fixtures. Loader tests cover loading, migration, and direct totals; validator,
dock, and pending suites cover #50. The three perf suites cover #51 without
assigning values to open game decisions.

Run with g++-13, C++23, CMake, and OpenSSL 3.x development files:

```sh
cmake -S shipyard -B build/shipyard -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/shipyard --parallel 2
ctest --test-dir build/shipyard --output-on-failure
```

`shipyard-formula-nesting` covers the formula nesting boundary. It can also be
run directly:

```sh
build/shipyard/shipyard-rules-tests --formula-nesting
```

CONTRACT.md specifies a 64-level formula guard independently of the configurable
JSON parser nesting guard. The test raises `InputLimits.maxNesting` to 128 and
supplies 64 nested `floor` operators over a constant. That documented boundary
must load; 65 levels must fail with a readable `invalid_formula` issue at
`/formulas/structureMass`.

The ordinary suites still check parser depth boundaries, import resource guards,
carried-craft depth, formula operand bounds, and malformed formula rejection.

## Performance

`shipyard-perf` imports the five canonical v3 definitions normally, then evaluates
their public `perf` results using active v4 rules. F24 approves the preserved
prototype performance metrics as the baseline: all defined scenario, propellant,
jump, and power values are compared with finite binary64 arithmetic and absolute
`numeric.epsilon`, without relative tolerance. A fractional-load case checks
direct mass rounding at `numeric.precision` before acceleration/range evaluation.
Deliberately misleading imported `derived` and `validation` caches must leave the
complete recomputed result unchanged. A schema-valid misleading embedded `rules`
snapshot must preserve calculated results and structural findings while its
changed rule identity is recorded in the migration report.

The suite separately checks F24's v4 costs, including Kestrel Tender's reduction
from 65,580 to 64,330 cr, and all five explicit F01 jump-drive prices through
public perf results. Rustbucket stays structurally invalid with its missing
airlock and three unreachable modules. Its jump power shortfall (12 supply,
16.2 demand, -4.2 margin), missing jump fuel, and no-cargo findings retain active
rule severities. Skiff's no-cargo advisory also appears at its nested location
when carried by Kestrel Tender. No golden is repaired or regenerated.

`shipyard-perf_pending` uses `blank_blueprint` for default ships and explicit
stations. It checks every DESIGN F20 identity, every scenario's zero-mass
omissions, engine-less burn-rate omissions, deltaV's pending exhaust-velocity
dependency, and an explicit blank ln(0) evaluation. Omitted metrics cannot acquire
zero, null, NaN, or string stand-ins. Blank structural reports remain inspectable;
Wayfarer Anchorage remains structurally valid and passes perf. Positive-mass
engine-less cases distinguish legitimate computed zeros from excluded formulas.

`shipyard-perf_scenario` checks current/empty/half/full loads, optional overrides,
active scenario fractions, exact input bounds and nonfinite loads, separate
readiness/jump-readiness/theoretical-range outcomes, and a performance check
promoted to error through public rules loading. F10 failures reject, including
an independent invalid operand beside an F20 exclusion and a formula that fails
only in carried Skiff. Parent load overrides preserve the child's current loads.

All three suites use the existing CTest wiring and warning-as-error test flags;
the existing CI build/test step discovers them without workflow changes.

## Validation and pending decisions

`shipyard-validator` imports all five canonical definitions normally, then calls
`validate` with deliberately misleading caches. Wren, Skiff, Kestrel Tender,
and Wayfarer Anchorage must structurally pass. Rustbucket must fail with
`missing_airlock` and `unreachable_component` for module IDs 1, 5, and 6;
normalization must preserve its geometry. Structural success leaves complete
readiness and jump readiness unset, as required by F23.

`shipyard-pending` checks requested holds through `validate` and `dock`, including
nested carried craft: F11–F17 must be structured blocking issues with null data,
without guessed hold totals or ordinary missing-floor errors. Blank ship and
station imports succeed with `pending_F20`; structural validation may still
report missing required systems. Wayfarer structurally passes with that warning.
Excluded performance values are absent rather than numeric substitutes.

Ports admit compatible candidates from Wren (20×34 m) and a structurally valid
visitor wider than the former v3 cap without a size-envelope failure. Multiple
compatible exterior airlocks are counted without selecting one. These cases
remain `PENDING` under check (c), with `pending_F21`, `data.pending=true`, and
overall FAIL: alignment has not been certified. Hangar tests distinguish a
definite unrotated fit, a definite oversize error, and a fit possible only after
rotation (`pending_F19`), while retaining active dimensions and deck limits.

## Per-check fault coverage

These public API tests author concrete faults in real blueprints and assert the
responsible issue or measured check. The table maps the review mutants to those
faults; no mutation-run score is claimed. Positive controls establish that the
baseline reaches the intended check. Each fixture uses the installed schemas,
rules, catalogue footprints, and library implementation.

| Reviewed fault | Authored input | Required public result | Suite |
| --- | --- | --- | --- |
| S07: command access removed | Bridge enclosed by nonwalkable parts | `command_access` | `structure_regression` |
| S10 / S12: airlock reach / clearance ignored | Sole airlock unreachable or approach obstructed | `missing_airlock` in each case | `structure_regression` |
| S14: station propulsion removed | Station carrying an engine | `station_propulsion` | `structure_regression` |
| S17 / S18: carried dimensions / decks ignored | Craft exceeds one berth dimension or its deck count | `carried_fit` | `structure_regression` |
| S21: module footprint ignored | Another module occupies the approach corridor | `blocked_approach` on the approached module | `structure_regression` |
| S23 / K05: exterior always true | Floor directly in front of matching visitor airlock | No exterior candidate; `dock_no_matching_airlock` | `structure_regression`, `dock_regression` |
| S27: negative-dy approach reversed | Rotation-2 module obstructed on its outward side | `blocked_approach` | `structure_regression` |
| K08 / K09: type early return removed | Nonstation host / nonship visitor | Typed rejection with `data: null` | `dock_regression` |
| K10: nondocking target accepted | Ordinary station system selected as target | `invalid_docking_target`; no escaping exception | `dock_regression` |
| K12: inclusive hangar deck boundary rejected | Visitor with exactly two decks | `deckCountFits: true`, no deck-limit issue | `dock_regression` |
| K14: failed check ignored at warning severity | Docking rule severity is warning and a check fails | Overall FAIL despite absence of error issues | `dock_regression` |
| K18: target kinds ignored | Station airlock selected as a target | `invalid_docking_target` | `dock_regression` |
| Cross-deck hull / bridge shortcut differences | Unlinked floored deck / separated bridge-side crew paths | `disconnected_hull` / `unreachable_component` | `structure_regression` |
| M1: unbounded diagnostic amplification | Grid-capacity overlapping modules / large malformed holds body | At most 128 issues including exactly one structured truncation error | `validator_cap` |

`shipyard-dock` also retains paired `DockOptions` enable/disable cases for F18(a)
through (d). Those are wiring checks: they show that each public switch controls
its check and the selected target duplicate suppression. They do not establish
that the check logic rejects every fault. The fault-specific regressions above
exercise that logic with defaults (apart from an explicit isolated warning-rule
case). No validator implementation is replaced.

The existing CTest step runs every suite without workflow changes. The v3
goldens remain byte-identical.

`shipyard-schema_boundary` exercises embedded informational formulas in a v4
wrapper with the parser bound raised. The bundled schema reaches recursion step
512 with 127 nested `floor` arrays; that input must import, while 128 arrays
must return a structured `schema_error`. These two cases distinguish a 384-step
cap and an unbounded checker from the documented 512-step cap.

`shipyard-issue_cap` checks the shared 128-slot diagnostic limit through public
schema, import, formula-loading, and rules-loading paths. A saturated list keeps
127 ordinary issues and exactly one root `issues_truncated` error with the
documented limit and suppression details. `shipyard-loader_regression` checks
engine-equipped canonical ships, grid capacity, and embedded-rule identity.


`shipyard-perf_issue_cap` uses authored warning-policy rules and bounded
blueprints through the real `load_rules`, `import_definition`, and `perf` APIs.
It covers warning-only carried-craft amplification, the public result limit,
127-versus-128 independent F20 formula evaluations, and a saturated child whose
truncation marker must remain a structured root error. Below-cap controls retain
ordinary advisories, structured `pending_F20`, and absent excluded metrics.
Saturated performance must fail with null data and at most 128 issues including
exactly one marker. Default parser, grid, deck, carrier and formula-arity limits
remain active; no fixture or golden is changed.

`shipyard-loader_r2` compares public parsing of 400,000 object and array
entries, times the 6.6 MB malformed-holds import, and rejects truncated craft
schema alternatives and floor-only grid overflow. Timing output records the
measured durations. The object/array ratio check applies in every build; the
1-second parse and 5-second import absolute limits apply only in Release builds
(`NDEBUG`). The existing 30-second watchdog and 120-second CTest timeout are
unchanged.

The r2 public-API suites extend the reviewed fault coverage:

- `shipyard-structure_r2`: command-origin hull flood (N02), bottom-first
  bridge service selection (N03), missing-command reachability (N06), and
  local hull connectivity with crew routes disabled (N07).
- `shipyard-hangar_r2`: rotation-proof minimum-span and diagonal rejection,
  inclusive boundaries, Skiff 6x10 in 8x8 and a 2x10 visitor in 9x9 remaining
  pending F19, plus Wren in 10x40 to distinguish the N14 orientation fault.
- `shipyard-validator_r2_cap`: carried-craft terminal truncation (N10),
  combined below-cap designs (N16), and marker retention with structural
  reporting disabled (N15). The public combined result has 124 overlaps,
  three prior import warnings, and one marker inside the shared 128 slots.

The unchanged `shipyard-validator_cap` inputs retain the 300,000-hold cases;
the inherited linear SAX parser keeps those calls below existing watchdogs.

## Historical archive: superseded F18 evidence

The following excerpt is preserved verbatim from the pre-merge documentation to
retain every historical F18 entry and its text. The current **Per-check fault
coverage** section above is authoritative for reviewed evidence; its concrete
fault regressions supersede the historical toggle-only evidence and old Mull
deferral below. The reviewed F19 behavior above (definite unrotated fit, definite
oversize, and rotation-only `pending_F19`) is authoritative. Any earlier blanket
F19 deferral remains historical and does not change current expectations. The
excerpt records the earlier session; it does not claim a current mutation score
or defer the current reviewed fault coverage.

Historical F19 context accompanying the original F18 evidence:

Ports admit compatible candidates from Wren (20×34 m) and a structurally valid
visitor wider than the former v3 cap without a size-envelope failure. Multiple
compatible exterior airlocks are counted without selecting one. These cases
remain `PENDING` under check (c), with `pending_F21`, `data.pending=true`, and
overall FAIL: alignment has not been certified. Hangar width/length fit remains
null with `pending_F19`; tests retain rule-defined dimensions and enforce the
settled deck cap without deciding permitted rotations.

## F18 disable-toggle mutation evidence

`shipyard-dock` has a separate case for each check and asserts both sides of
each toggle. A valid baseline controls the authored fault. Only the named
option changes between calls; all other options retain their true defaults.
Disabling (b) or (d) must also suppress the selected target's duplicate finding
inside (a), as CONTRACT.md specifies. Visitor-airlock access/clearance remain
under (a), at active rule severities, without duplicate failures under (c).

| Check | Option | Fault | Enabled-only assertion | Same assertion with only that option disabled | Invocation on `shipyard-dock-tests` |
| --- | --- | --- | --- | --- | --- |
| (a) Structural | `structural` | Reactor footprint lacks a floor | Green (exit 0): `missing_floor` caught, (a) FAIL | Red (exit 1): finding missing, (a) DISABLED | `--enabled-only a`, then append `--disable a` |
| (b) Station crew access | `portReachability` | Nonwalkable seats surround the selected port | Green (exit 0): `dock_port_unreachable` caught, (b) FAIL | Red (exit 1): finding missing, selected duplicate suppressed, (b) DISABLED | `--enabled-only b`, then append `--disable b` |
| (c) Matching airlock | `matchingAirlock` | Visitor airlock has an incompatible interface | Green (exit 0): `dock_no_matching_airlock` caught, (c) FAIL | Red (exit 1): finding missing, (c) DISABLED | `--enabled-only c`, then append `--disable c` |
| (d) Approach | `approachClearance` | Connected hull obstructs the selected port's approach | Green (exit 0): `dock_blocked_approach` caught, (d) FAIL | Red (exit 1): finding missing, selected duplicate suppressed, (d) DISABLED | `--enabled-only d`, then append `--disable d` |

The table concerns the fault-specific assertion, not overall dock status:
F21 still blocks compatible candidates when other checks are disabled. The
ordinary CTest suite stays green by explicitly asserting that enabled catches
the fault and disabled misses it. No validator implementation is replaced.

After the build above, a reviewer can run the identical enabled-only assertions
and see each go red by flipping its own option:

```sh
for check in a b c d; do
  build/shipyard/shipyard-dock-tests --enabled-only "$check"
  build/shipyard/shipyard-dock-tests --enabled-only "$check" --disable "$check"
done
```

For each check the first invocation exits 0 and the second exits 1 with a
`FAIL` diagnostic. The normal executable without arguments asserts both sides
and exits 0. These eight invocations are the session's mutation evidence.

Mull is deferred as a follow-up. This session uses the prescribed g++-13 build
and public option toggles; a separate LLVM-instrumented build and mutation run
would add toolchain/build work within the bounded scratch and session budget.
Mull is not added to CI. The existing CTest step runs every suite, including
`shipyard-formula-nesting`, without workflow changes. The v3 goldens remain
byte-identical.


## Independent nits coverage (#55; F04/F05/F06)

`shipyard-authoring` uses the public `authoring.hpp` and loader APIs. It checks
default blank ships and explicit/imported stations, automatic names for all
active minimum decks, literal custom prefixes and older-v4 prefix fallback.
Insertion, movement in both directions, removal, and rename preserve retained
floors, module geometry/IDs, `nextId`, metadata, and nested carried craft.
Automatic labels follow ordinary one-based physical deck positions. Imported
labels and explicit names, including generated-looking names, retain their
text. True, false, and absent provenance survive normalization and canonical
export/reimport; renaming explicitly clears ownership and nullopt restores it.
This chooses no F16 multi-deck-hold numbering convention.

Positive controls accompany invalid indices, exact deck-count/input-byte/name
bounds, malformed UTF-8, C0/DEL controls, and invalid provenance types. Rejected
edits must return structured errors with no partially edited result. Blank
authoring/import retains structured `pending_F20` and absent excluded metrics,
without numeric or null substitutes. Empty/whitespace labels retain only the
existing interim F22 contract. F11–F17/F19/F20/F21 remain open.

`shipyard-nits_owner` independently checks all five explicit owner-picked jump
prices through public performance and direct costs; ports without size fields
or an envelope for Wren; exact and over-limit hangar width, length, and deck
counts. Deck-boundary visitors have real paired ladders and pass ordinary
structural validation. Eligible docking remains pending F21; definite fits and
oversize cases do not assign F19 rotation rules. Existing perf, pending, dock,
and regression suites retain their wider-than-former-port-cap coverage.

The migration suite's literal v4 rules SHA pin was independently reviewed and
updated for the sole rules-file delta, `defaults.deckNamePrefix: "Deck "`. Its
independent digest and whitespace-sensitive identity assertions remain intact.
No v3 fixture or manifest was edited. F06's concealed-cargo tooltip remains
explicitly deferred until a UI exists, as issue #55 and FINDINGS require.


## Independent #55 coverage after final Argus r2 integration

The reviewed CONTRACT/F19/F23 rotation-proof bounds supersede the earlier
two-axis oversize assumption in the original nits coverage above. The original
6 by 9 hangar with a 6 by 10 Skiff remains covered as an uncertain fit:
`widthLengthFit` is null, structured `pending_F19` remains nonblocking as an
issue but blocks certification, and no guessed size error is reported. The
same checks cover the 6 by 8 diagonal equality boundary. Both requests still
fail with pending data even when only the F21 matching-airlock check is
disabled through the public test seam; enabled structure, crew access, and
approach checks pass. Neither case establishes an allowed rotation or a fit.

Inclusive width/length endpoints and independent definite overbounds remain
covered: 6 by 10, 6 by 11, and 7 by 10 certify unrotated dimensions; 5 by 10
fails the minimum-span bound, while 6 by 7 fails the diagonal bound because
its diagonal is shorter than the visitor's 10 metre length. Both overbounds
require the active docking error and a failed envelope. The original price,
port, connected-deck-cap, and authoring assertions are preserved. F19/F21 stay
open; no fixture, v3 golden, production rule, or allowed rotation is changed.


<details>
<summary>Loader-only documentation at the main #62 squash</summary>

This loader-stage snapshot is retained verbatim as historical merge evidence.
The current integrated contract and reviewed findings above govern this branch.
All open owner decisions remain open. No FINDINGS IDs were renumbered.

~~~text
# Independent loader tests

These tests exercise the public API in `loader.hpp`, with no mocks. Expectations
come from DESIGN.md, CONTRACT.md, the owner decisions, and the unchanged v3
fixtures. They cover loading and direct totals only. Structural rejection
(including Rustbucket), performance evaluation, and docking remain #50/#51 work;
no open game decision is assigned a value here.

Run with g++-13, C++23, CMake, and OpenSSL 3.x development files:

```sh
cmake -S shipyard -B build/shipyard -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS=-Werror
cmake --build build/shipyard --parallel 2
ctest --test-dir build/shipyard --output-on-failure
```

`shipyard-formula-nesting` covers the formula nesting boundary. It can also be
run directly:

```sh
build/shipyard/shipyard-rules-tests --formula-nesting
```

CONTRACT.md specifies a 64-level formula guard independently of the configurable
JSON parser nesting guard. The test raises `InputLimits.maxNesting` to 128 and
supplies 64 nested `floor` operators over a constant. That documented boundary
must load; 65 levels must fail with a readable `invalid_formula` issue at
`/formulas/structureMass`.

The ordinary suites still check parser depth boundaries, import resource guards,
carried-craft depth, formula operand bounds, and malformed formula rejection.

`shipyard-schema_boundary` exercises embedded informational formulas in a v4
wrapper with the parser bound raised. The bundled schema reaches recursion step
512 with 127 nested `floor` arrays; that input must import, while 128 arrays
must return a structured `schema_error`. These two cases distinguish a 384-step
cap and an unbounded checker from the documented 512-step cap.

`shipyard-issue_cap` checks the shared 128-slot diagnostic limit through public
schema, import, formula-loading, and rules-loading paths. A saturated list keeps
127 ordinary issues and exactly one root `issues_truncated` error with the
documented limit and suppression details. `shipyard-loader_regression` checks
engine-equipped canonical ships, grid capacity, and embedded-rule identity.

`shipyard-loader_r2` compares public parsing of 400,000 object and array
entries, times the 6.6 MB malformed-holds import, and rejects truncated craft
schema alternatives and floor-only grid overflow. Timing output records the
measured Release durations; the parser regression guards stay below the
existing 30-second watchdog and 120-second CTest timeout.
~~~

</details>

<details>
<summary>Main efe5fd82 integration: preserved source text</summary>

These literal merge-source excerpts preserve both sides of the squash-history
merge. The current integrated contract and findings above govern this branch;
performance and authoring are already implemented. Superseded implementation
status is historical, and no open owner decision is resolved here.

~~~text
`shipyard-loader_r2` compares public parsing of 400,000 object and array
entries, times the 6.6 MB malformed-holds import, and rejects truncated craft
schema alternatives and floor-only grid overflow. Timing output records the
measured Release durations; the parser regression guards stay below the
existing 30-second watchdog and 120-second CTest timeout.

These tests exercise the public APIs in `loader.hpp` and `validator.hpp`, with no mocks. Expectations
come from DESIGN.md, CONTRACT.md, the owner decisions, and the unchanged v3
fixtures. Loader tests cover loading, migration, and direct totals; validator,
dock, and pending suites cover #50. Performance evaluation remains #51 work;
no open game decision is assigned a value here.
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
````

</details>
