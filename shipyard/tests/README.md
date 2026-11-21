# Independent shipyard tests

These tests exercise the public APIs in `loader.hpp` and `validator.hpp`, with no mocks. Expectations
come from DESIGN.md, CONTRACT.md, the owner decisions, and the unchanged v3
fixtures. Loader tests cover loading, migration, and direct totals; validator,
dock, and pending suites cover #50. Performance evaluation remains #51 work;
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

For each check the first invocation exits 0 and the second exits 1 with a
`FAIL` diagnostic. The normal executable without arguments asserts both sides
and exits 0. These eight invocations are the session's mutation evidence.

Mull is deferred as a follow-up. This session uses the prescribed g++-13 build
and public option toggles; a separate LLVM-instrumented build and mutation run
would add toolchain/build work within the bounded scratch and session budget.
Mull is not added to CI. The existing CTest step runs every suite, including
`shipyard-formula-nesting`, without workflow changes. The v3 goldens remain
byte-identical.

## Required Mull mutation gate (#64)

This section supersedes the historical Mull deferral above. The required `ci`
job now runs a separate Clang/LLVM 20 build with Mull 0.37.0, in addition to
the unchanged ordinary g++-13 Release build and CTest step. CI installs the
Ubuntu LLVM compiler/runtime packages and checksum-verified, versioned Mull
release `.deb` packages; CMake, make, and OpenSSL are reused from earlier setup.

The historical baseline at #63 head
`f476383d6c25ba485c07935b14dc17a2080fb1d6`, committed in original #66
`27b1e3b45af598e6ddde9f70ffc6e1b1db86c39f`, was **336 killed / 446 distinct
mutations = 75.33632286995515%, with 110 survivors**. It used Clang20.1.8,
Mull0.37.0 and the LLVM20 plugin over all eight then-existing CTest commands,
including `--formula-nesting`. Its complete survivor list remains in the
[historical baseline](https://github.com/gobha-me/apsis/blob/27b1e3b45af598e6ddde9f70ffc6e1b1db86c39f/shipyard/mull-baseline.json).
Prior required CI [run44846858065](https://github.com/gobha-me/apsis/actions/runs/44846858065)
measured **338/446 = 75.784753363%, with 108 survivors**, all a subset of that
110-survivor list. The original documented minimum remained the earlier exact
local **336/446** ratio, rather than the higher CI observation.

The gate uses the exact committed ratio, not a rounded percentage. Mull's own
integer threshold is supplemented by `measured_killed * baseline_total >=
baseline_killed * measured_total`, a total floor, a survivor-count ceiling, and
membership of every survivor's operator and begin/end source span in the
committed list. Added kills or rounding cannot conceal an extra or replacement
survivor. An explicitly requested rebaseline also refuses a lower total, worse
exact ratio, or larger survivor count than the preceding baseline.

[`../mull.yml`](../mull.yml) fixes Mull's standard `cxx_default` operators
(increment, arithmetic, comparison, and boundary) and includes shipyard sources
and public headers. Test code and vendor code are outside that scope. Each
CTest command is discovered from CTest's JSON manifest and run with its original
arguments. `--include-not-covered` also executes mutations on uncovered lines;
there is no coverage-based exclusion. The per-command SQLite results are
combined by mutant identity: a failure in any suite kills the mutant. Individual
runs allow survivors so that every suite completes; the final reporter and
exact baseline checks enforce the gate. A fresh database is used each time.

Both build and mutation workers are limited to two. The helper uses each
CTest command's existing `TIMEOUT` (120 seconds here) for Mull's unmutated
warmup and baseline, bounded at 120 seconds; every original command must pass.
The former 3000 ms original-run limit cannot accommodate the expanded cap
suites. Mutant timeouts retain Mull0.37.0's policy of ten times the measured
baseline duration, with a 30 ms minimum. Crashes and timeouts remain detected
mutations; unexpected or unexecuted statuses fail the helper.

For each command, a wrapper snapshots only actual killed IDs from completed
earlier commands in the same fresh SQLite measurement. It returns exit0 for
an active ID already killed there; otherwise it execs the exact original CTest
command and arguments, retaining the mutant environment. Warmup and baseline
have no active mutant and always execute the original command. A bypass adds
only a survived row, while the earlier observed kill remains in the unchanged
OR summary. No committed baseline, previous run, or guessed kill is reused;
all commands and all discovered identities remain in the combined report.
The wrapper's startup time is included in Mull's measured baseline. This uses
Mull0.37.0's pinned
[`--test-program` integration](https://github.com/mull-project/mull/blob/0.37.0/docs/tutorials/NonStandardTestSuite.rst).

After installing the pinned packages shown in `.github/workflows/ci.yml`, run:

```sh
export MULL_CONFIG="$PWD/shipyard/mull.yml"
cmake -S shipyard -B build/shipyard-mull \
  -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  "-DCMAKE_CXX_FLAGS=-g -gz=zlib -grecord-command-line -fpass-plugin=/usr/lib/mull-ir-frontend-20" \
  "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--compress-debug-sections=zlib"
cmake --build build/shipyard-mull --parallel 2
ctest --test-dir build/shipyard-mull --output-on-failure --parallel 2
python3 shipyard/mutation.py build/shipyard-mull
```

The helper prints the exact score and every survivor; the combined report is
`build/shipyard-mull/shipyard-mull.sqlite`. Mull reports survival when the
applicable test programs exit zero with that mutation active. It does not
establish whether a survivor is equivalent, unexecuted, or insufficiently
asserted. Do not infer a missing assertion from survival alone.

An intentional rebaseline requires a complete measured run, review of every
survivor, and an updated explanation here. `--record-baseline` writes that
measurement explicitly; CI never passes this option. Do not regenerate the
baseline merely to make a regression pass. See Mull's
[CTest integration](https://mull.readthedocs.io/en/latest/tutorials/CTestIntegration.html)
and [multiple-target reporting](https://mull.readthedocs.io/en/latest/tutorials/MultipleTestTargets.html)
for the combined-report approach.
`shipyard-issue_cap` checks the shared 128-slot diagnostic limit through public
schema, import, formula-loading, and rules-loading paths. A saturated list keeps
127 ordinary issues and exactly one root `issues_truncated` error with the
documented limit and suppression details. `shipyard-loader_regression` checks
engine-equipped canonical ships, grid capacity, and embedded-rule identity.

## Phase2 test setup while final review is pending

The current #63 integration adds six CTest commands to the historical eight:
`shipyard-issue_cap`, `shipyard-loader_regression`, `shipyard-schema_boundary`,
`shipyard-structure_regression`, `shipyard-dock_regression`, and
`shipyard-validator_cap`. Every command remains discovered automatically.
The authorized rebaseline is on hold until the final reviewed #63 is plainly
merged into #66. The committed minimum remains **336/446**, with the original
**110-survivor** list; no new mutation score or survivor list is accepted here.

The instrumented configuration uses Release with full `-g`, recorded compiler
arguments, and the same LLVM20 plugin. Debug at `-O0`, `-O1`, `-O2`, and Release
at the integrated `1cf4ea7` source all discovered the same **497** identities
and replacements, including uncovered mutations; this is discovery evidence,
not a mutation score. Mull0.37.0 registers its
[instrumentation before LLVM optimization](https://github.com/mull-project/mull/blob/0.37.0/tools/mull-ir-frontend/mull-cxx-ir-frontend.cpp#L40).
Release sets `-O3 -DNDEBUG`; all shipyard expectations use `require()` and do
not depend on `assert` or `NDEBUG`, as checked in tests, sources, public headers,
and CMake. This mode changes vendor/internal debug assertions in the same way
as the ordinary Release build. The existing 30-second test checks and
120-second CTest limits remain active. Optimization alone did not make the
old malformed-hold parser path pass its 30-second check, so a valid full
measurement remains pending the reviewed production remedy.

Full DWARF is retained with `-g -gz=zlib`, and executable linking explicitly
uses `-Wl,--compress-debug-sections=zlib` so the linker preserves compressed
output. This is lossless debug-section compression, with no omitted types,
line tables, source files, or mutation filters. A bounded proof on one parked
prototype dock executable reduced its size from 78,839,384 to 31,475,424 bytes.
All allocated sections (including code) and the raw `.mull_mutants` section
remained byte-identical; each compressed DWARF section decompressed to the
original bytes. Separate Mull 0.37.0 `--dry-run --include-not-covered` SQLite
reports had identical 490 identity/source-span/replacement records, all status
6 (not executed). That count describes only the prototype compression probe;
it is neither a current-tree measurement nor an accepted score or baseline.
The probe's original warmups passed. Full-tree validation and remeasurement
after the final reviewed #63 integration remain required.

The final source tree must pass its original suites, lint, and complete Mull
measurement before any new exact minimum can be accepted.

## C++ static analysis

CI installs the Ubuntu `clang-tidy-20` package through apt. The versioned major
matches the existing LLVM 20 tooling; local validation used Ubuntu LLVM 20.1.8.
Clang-tidy uses its default compiler diagnostics and static-analyzer checks,
with every emitted warning promoted to an error by `--warnings-as-errors='*'`.
This checks C++ semantics without imposing an unrelated formatting rewrite.

The compilation database comes from the ordinary g++-13 Release C++23 build,
with its existing `-Werror`, includes, and test definitions. Exporting the
database does not change compilation flags. Every tracked `shipyard/src/*.cpp`
and `shipyard/tests/*.cpp` is checked (20 translation units at this integration).
The header filter also includes owned source, public, and test headers; vendor
code remains a system include. No NOLINT, disabled checks, or weaker flags are
used. The lint step runs before the existing mutation gate and has no condition
that could mask a failure in either check.

To reproduce from the repository root after installing `clang-tidy-20`:

```sh
cmake -S shipyard -B build/shipyard \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCMAKE_CXX_FLAGS=-Werror \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/shipyard --parallel 2
ctest --test-dir build/shipyard --output-on-failure
git ls-files -z 'shipyard/src/*.cpp' 'shipyard/tests/*.cpp' | \
  xargs -0 -r clang-tidy-20 -p build/shipyard --warnings-as-errors='*' \
    --header-filter='(^|/)shipyard/(src|include|tests)/'
```

No test assertions, time bounds, or survivor-killing cases were changed for
this lint setup. The historical mutation minimum and survivor list remain
unchanged while final #63 review and the authorized remeasurement are pending.

## Superseded F18 toggle evidence and F19 context

The following verbatim excerpt from original #66 (`27b1e3b`) is historical.
The reviewed per-check fault coverage and current pending-decision description
above supersede its former mutation-evidence and hangar-fit claims. The required
Mull gate supersedes its Mull deferral. No F finding was renumbered.

<details>
<summary>Historical F19 context and F18 disable-toggle evidence</summary>

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


</details>

`shipyard-loader_r2` compares public parsing of 400,000 object and array
entries, times the 6.6 MB malformed-holds import, and rejects truncated craft
schema alternatives and floor-only grid overflow. Timing output records the
measured Release durations; the parser regression guards stay below the
existing 30-second watchdog and 120-second CTest timeout.

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
