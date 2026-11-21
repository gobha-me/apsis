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

The measured baseline at #63 head
`f476383d6c25ba485c07935b14dc17a2080fb1d6` is **336 killed / 446 distinct
mutations = 75.33632286995515%, with 110 survivors**. It was measured using
Clang 20.1.8 and Mull 0.37.0 (LLVM 20 plugin), with all eight existing CTest
entries, including the `--formula-nesting` invocation. CI uses Ubuntu's Clang
20 package. The complete measured survivor identities, source spans, operators,
and replacements are recorded in [`../mull-baseline.json`](../mull-baseline.json).
Survivors are follow-up test work; no existing tests or assertions were changed.

The chosen minimum is the **exact measured ratio, 336/446**, rather than a
rounded percentage. Mull's own integer threshold is 75%; `mutation.py` also
requires `killed * 446 >= 336 * total`, at least 446 distinct mutations, no more
than 110 survivors, and every current survivor identity to be in the measured
list. Thus neither percentage rounding nor additional killed mutations can
conceal an additional or replacement survivor. The existing survivors are
explicitly retained, without lowering the measured minimum.

[`../mull.yml`](../mull.yml) fixes Mull's standard `cxx_default` operators
(increment, arithmetic, comparison, and boundary) and includes shipyard sources
and public headers. Test code and vendor code are outside that scope. Each
CTest command is discovered from CTest's JSON manifest and run with its original
arguments. `--include-not-covered` also executes mutations on uncovered lines;
there is no coverage-based exclusion. The per-command SQLite results are
combined by mutant identity: a failure in any suite kills the mutant. Individual
runs allow survivors so that every suite completes; the final reporter and
exact baseline checks enforce the gate. A fresh database is used each time.

Both build and mutation workers are limited to two. Mull's `--timeout 3000`
limits its original test runs; mutant timeouts use Mull 0.37.0's default of
ten times the baseline duration, with a 30 ms minimum. Crashes and timeouts
are detected mutations under Mull's scoring rules; unexpected or unexecuted
result statuses fail the helper.

After installing the pinned packages shown in `.github/workflows/ci.yml`, run:

```sh
export MULL_CONFIG="$PWD/shipyard/mull.yml"
cmake -S shipyard -B build/shipyard-mull \
  -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  "-DCMAKE_CXX_FLAGS=-g -grecord-command-line -fpass-plugin=/usr/lib/mull-ir-frontend-20"
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
