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
