# Configurable balance measurements

`tools/sim/balance_sweep.py` complements the four fixed comparisons in
[balance-lever-sweeps.md](balance-lever-sweeps.md). It runs a Cartesian grid or
reproducible discrete Latin-hypercube design over existing core parameters, with
matched seeds, bot styles, and horizons. It never writes shipped configuration,
rules, content, or simulation code. Experiments express observations, not chosen
defaults. All simulation rules remain enabled at their imported defaults.

The standalone C++ adapter uses `create`, `setParam`, `step`, `submit`, `tick`,
and `decideBot`. Overrides affect an in-memory world before warmup; each run starts
with a newly generated world. Unlike `pilot_exp`, it launches no worker pool and
does not replace the requested tank with a hardcoded three-tank loop. The world
and bot implementations are unchanged. Output directories must be new.

## Build and inspect a plan

Use a C++23 compiler (GCC 13 works). Keep build/run artifacts in an owned scratch
directory. On a shared host, use its existing `heavy-run` wrapper for native builds
and campaigns, with at most two build workers:

```sh
heavy-run sh -c 'cmake -S tools/sim -B "$TMPDIR/balance-build" -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Release && cmake --build "$TMPDIR/balance-build" --target balance_seed --parallel 2'
python3 tools/sim/balance_sweep.py run --output "$TMPDIR/balance-plan" \
  --driver "$TMPDIR/balance-build/balance_seed" --dry-run
```

Dry runs require no binary. The default plan is 12 runs: tanks 3/6, seeds
333860/110478, hauler/crisis/pathfinder, 180 measured days, no warmup, and resource
samples every 30 days. Default budgets are 24 runs and 100,000 simulated days,
including warmup and START. Plans exceeding either budget fail before execution.
Big sweeps must run through `heavy-run`; increasing `--max-runs` or
`--max-sim-days` is an explicit operational choice. Split very large experiments.
Do not commit generated campaign data; keep each committed asset below 1 MB.

## Grid and discrete Latin hypercube

```sh
heavy-run python3 tools/sim/balance_sweep.py run \
  --driver "$TMPDIR/balance-build/balance_seed" --output "$TMPDIR/balance-grid" \
  --axis stockTank=3,6 --axis mediumMass=35,40 \
  --seeds 333860,110478 --styles hauler,crisis,pathfinder --days 360

heavy-run python3 tools/sim/balance_sweep.py run \
  --driver "$TMPDIR/balance-build/balance_seed" --output "$TMPDIR/balance-latin" \
  --method latin --samples 4 --design-seed 19 \
  --axis mediumMass=35,40,45,50 --axis stockTank=3,4,6 \
  --seeds 333860,110478 --styles hauler --days 360
```

Axes contain unique integer values and are sorted before sampling. For Latin
hypercubes, each axis divides [0,1) into N equal strata, draws one uniform value
from each stratum, shuffles them independently, then maps quantiles to that
axis's declared finite domain. This is a discrete projection of LHS, not a
continuous parameter optimizer. Mapping can repeat configurations; those points
run only once. The manifest keeps all design candidates and the unique executed
points. A design seed is separate from simulation seeds. Repeated deterministic
configurations do not increase an effective sample size.

Supported axes are deliberately bounded tooling inputs; these limits are not
design recommendations:

| Existing parameter | Accepted values | Unit |
| --- | --- | --- |
| `stockTank` | 1–12 | jumps |
| `mediumMass` | 20–120 | mass |
| `heavyMass` | 40–200 | mass |
| `mediumSpace` | 60–240 | space |
| `stockDrivePm` | 500, 1000, 1500, 2000, 3000 | per mille J |

No rule switches or world-generation overrides are accepted. Unsupported axes,
duplicate axes, duplicate values, and noncanonical drive classes fail. All
inherited `APSIS_*` environment variables are stripped from child processes; only
variable names are recorded. The native adapter also rejects the known override
hooks when invoked directly. Majors are 3–5, warmup is 0–36,000 days, measured
horizon is 1–7,200 days, and at most 1,001 curve points are allowed per run. Six
majors are excluded because the imported generator has an existing array-index
edge case there. Each child has a configurable wall timeout (default 60 seconds).

## Evidence and interpretation

`manifest.json` records resolved core parameters, rule defaults, exact commands,
matched inputs, source revision/dirty flag, SHA-256 of every `sim/src` file,
adapter/controller source hashes, binary hash, and raw result hashes. Build from
the recorded sources; hashes describe supplied evidence, not an attestation that
a binary was built from those sources. World parameters use compiled defaults
from the hashed source. These checks do not independently verify simulation
correctness or authenticate artifacts.

`runs/` contains one JSON result and stderr log per seeded run. Exact integral
resources are decimal strings in raw JSON. The measured baseline is immediately
after START's tick; elapsed day zero is absolute world day `startDay + 1`. Each
run advances up to `--days` further ticks, ending early on a terminal outcome.
Samples include baseline, each interval, and the final observation. Ledger checks
cover world creation, warmup, START, and measured ticks. Invalid recipes, settings,
resources, curve coordinates, or ledger failures abort the campaign. An interrupted
or failed campaign is never summarized as complete.

Generated `runs.csv`, `summary.csv`, `curves.csv`, `summary.json`, and `summary.md`
include survival, lost/hazard/bankruptcy counts, terminal timing, first job-completion
timing per run, final resource medians, and resource-curve means. Rebuild reports
without running the sim:

```sh
python3 tools/sim/balance_sweep.py report "$TMPDIR/balance-grid"
```

Regeneration requires complete matched evidence and verifies its recorded hashes.
Layer 4a has no global win state, so `winRate` is null; survival is explicitly
reported instead. Horizon observations are right-censored, not successful endings.
Terminal-day medians exclude censored runs and are not survival-analysis estimates.
Final resource summaries use each run's last observation, which can occur early.
Curve means use only runs observed on the exact plotted day; `observedRuns` exposes
the changing denominator. No deceased-run carry-forward or interpolation is used.
Completed jobs are `Pilot.n.done`; independent cargo sales are excluded.
Money and fuel are descriptive resource measurements, not an economic welfare score.

Small curated samples and bot policy choices cannot establish population-wide
balance. Compare matched per-seed results, retain warmup and horizon details, and
frame reports as observations for designer review. These tools never propose or
apply new rules in game code.

## Verification

```sh
python3 -m unittest discover -s tests -p test_balance_sweep.py -v
heavy-run env APSIS_BALANCE_DRIVER="$TMPDIR/balance-build/balance_seed" \
  python3 -m unittest discover -s tests -p test_balance_sweep.py -v
```

The first command runs planner, evidence, failure, censoring, and environment-hook
checks. The second also exercises a real four-point grid, deterministic reruns,
offline regeneration, and native input rejection. Native checks are opt-in; no CI
workflow is changed. Python lint uses Ruff 0.14.6, matching the repository workflow.
