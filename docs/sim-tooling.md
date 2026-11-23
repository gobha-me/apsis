# Simulation tooling

The imported Layer 4a code under `sim/` is replaceable (see
[`sim/SOURCE.md`](../sim/SOURCE.md)). Build test adapters from `tools/`; they use
existing simulation APIs and leave shipped rules, parameters and content intact.
Python 3.12+, CMake 3.25+ and g++-13 with C++23 support are sufficient.

## Bounded invariant matrix

From the repository root, choose build and artifact directories outside `sim/`:

```sh
cmake -S tools -B "$TMPDIR/apsis-tools" -DCMAKE_CXX_COMPILER=g++-13
cmake --build "$TMPDIR/apsis-tools" --target sim_invariant_driver --parallel 2
python tools/sim_gate.py --driver "$TMPDIR/apsis-tools/sim_invariant_driver" \
  --output-dir "$TMPDIR/apsis-matrix" --seeds 333860,697804 --days 360
```

Use the host's existing `heavy-run` wrapper for native builds and campaigns where
required. The adapter runs one simulation at a time with the default one-thread
world; it does not invoke the hardware-concurrency experiment executables.

The default matrix runs six scenarios per seed: world without a pilot, hauler,
crisis, pathfinder, repayment and bankruptcy. `--seeds` accepts 1–256 distinct
uint32 seeds; select a subset with `--scenarios world,hauler`. `--days` is the
number of measured ticks (1–36,000), in addition to initialization. The bankruptcy
probe warms the world up for the default bankruptcy interval, starts a pilot,
then injects an old debt of 1,000,000,000 and its start clock at day zero. The
repayment probe starts with a wallet of 2,000 and a debt of 500 and submits REPAY.
These are synthetic test states, not samples of naturally occurring end states.
They do not edit balance values or turn rules off.

Every state, including initial state, warmup ticks and pilot start, checks:

- the simulation's exact resource ledger, including pilot cargo and losses;
- a configurable operational entity bound (`--max-entities`, default 100,000);
- a one-day clock increment after each tick.

During the measured horizon, the two explicit probes must observe successful
repayment or terminal bankruptcy respectively. An observed repayment remains a
success if later upkeep causes a new debt or the idle pilot eventually goes
bankrupt. The final `end_state` still records that later outcome.

The entity count includes stations, freighters, route states, agents, contests,
queued intents, applied-input log entries, pilot debts, warrants and events, plus
route pirate groups and patrol units. It is a guard on these collections; it does
not measure total memory or bound every auxiliary cache/history. The bound is a
test limit, not a game rule. Each case has a timeout (`--timeout-seconds`, default
30); timeouts/crashes fail even if partial output exists. Short horizons need not
contain deliveries or terminal outcomes in the ordinary bot scenarios.

Persisted simulation numbers are integral; NaN/Inf cannot be encoded in those
checkpoint fields. The JSON reader also rejects nonfinite constants and exponent
overflow, duplicate keys, wrong seed/scenario/horizon, missing checks and an exit
status inconsistent with the report. This does not prove absence of transient
floating-point issues in map generation. Layer 4a has death and bankruptcy but
no global win state. Deliveries are recorded as milestones; no artificial win
condition or balance acceptance threshold is introduced.

The output directory must be fresh. Each seed/scenario keeps `stdout.json` and
`stderr.txt`; top-level `summary.json` (schema version 1) and `summary.txt` include
all cases, failure diagnostics and commands. Full-checkpoint hashes use the
simulation's existing 64-bit hash helper and include pilot state. They are
regression digests, not cryptographic integrity checks. The driver's SHA-256
identifies the executed binary; it does not attest a source revision. Build from
the checkout being evaluated and retain its build commands. Ambient `APSIS_*`
environment overrides are stripped from child processes.

Exit status is **0** when all cases pass, **1** for invariant failures, and **2**
for CLI, execution or report errors. The runner continues after failed cases.
Artifacts from an interrupted runner are incomplete evidence.

```sh
# Pure runner tests use fixture executables; no compiler/native build is needed.
python -m unittest discover -s tests -p test_sim_gate.py -v
# Explicit opt-in runs all native scenarios and a deliberately tiny entity bound.
APSIS_SIM_INVARIANT_DRIVER="$TMPDIR/apsis-tools/sim_invariant_driver" \
  python -m unittest discover -s tests -p test_sim_gate.py -v
```

The existing default Python discovery runs the pure tests and reports native
tests as skipped unless the corresponding environment variable is set. The
invariant matrix does not add a CI step; the separately reviewed replay suite
below has its own test-only CI step.

## Existing standing suites

[`docs/determinism.md`](determinism.md) describes the existing 1/2/8-thread world
comparisons and checkpoint standing gates. Build `sim/` separately and run
`gates`, `world_core gates` and `pilot_gates gates`, `switches`, `endstates` and
`law` from a disposable output directory. The `gates` executable also needs the
absolute path to `sim/ref/vectors.txt`, a day count and an output filename.
`tools/determinism.py prepare/world/report` collects their fresh evidence; see
its CLI help and the workflow for exact report inputs. Frozen v0.20 and WASM
parity are unavailable/deferred as documented in `sim/SOURCE.md`.

For the paired war-rate campaign use
[`world-wide-gate.md`](world-wide-gate.md); for rescue-debt sampling use
[`rescue-debt-sampler.md`](rescue-debt-sampler.md). Those reports describe their
own worker limits and interpretation. Shipyard tests and measured Mull checks
are documented in [`shipyard/tests/README.md`](../shipyard/tests/README.md).

## Recorded replays and seeded input properties

The independent adapters introduced in [PR #72](https://github.com/gobha-me/apsis/pull/72)
have a [detailed guide](https://github.com/gobha-me/apsis/blob/b3fa7b59b14b7cff3fde0dc3b6b86623b283c5bd/docs/sim-replay.md).
In a checkout containing that change, run:

```sh
cmake -S tests/sim -B "$TMPDIR/apsis-replays" -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG'
cmake --build "$TMPDIR/apsis-replays" \
  --target sim_replay_driver sim_input_properties --parallel 2
ctest --test-dir "$TMPDIR/apsis-replays" --output-on-failure --parallel 2
```

The tiny recorded input corpus pins daily full-checkpoint hashes; tests compare
checkpoint bytes for fresh replay, canonical input order and save/load. For
optimization parity, build `sim_replay_driver` in a separate directory using
`-O0 -DNDEBUG`, then set `APSIS_SIM_REPLAY_DRIVER_O0` to that executable when
running CTest against the O2 build. These checks cover the tested corpus and
toolchain only. Native CTest paths are set automatically; ordinary Python
discovery skips native checks without explicit driver variables.

The property executable uses a fixed seed, 8,192 serialized actions and eight
30-day random command streams; its Python wrapper enforces a 15-second process
deadline. Full integer extrema stay in serialization/inbox properties; gameplay
commands have bounded fields. The guide documents explicit fixture recording;
tests never regenerate golden hashes.

## Shipyard follow-on suites

The assertions in [PR #73](https://github.com/gobha-me/apsis/pull/73) extend the
existing pending/schema-boundary suites with hold-finding deduplication,
identifier alphabets and SHA-256 syntax. Build the Release test tree and run all
existing entries:

```sh
cmake -S shipyard -B "$TMPDIR/apsis-shipyard" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 -DBUILD_TESTING=ON
cmake --build "$TMPDIR/apsis-shipyard" --parallel 2
ctest --test-dir "$TMPDIR/apsis-shipyard" --output-on-failure --parallel 2
```

For measured mutation checks use the instrumented Clang20/Mull0.37.0 build
instructions in the Shipyard test README, with the shipped `shipyard/mull.yml`.
Run `python shipyard/mutation.py /path/to/instrumented-build` through the host's
mutation wrapper (`agent-mutate` where available). Do not use `--record-baseline`
to accommodate a failure. Disable core dumps for deliberate mutation failures
(`ulimit -c 0`) and retain actual before/after mutant identities with the report.
