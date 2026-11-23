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

After the measured horizon, the two explicit probes must reach successful
repayment or terminal bankruptcy respectively.

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
tests as skipped unless the corresponding environment variable is set. No CI
workflow is changed by this tooling.

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
