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
