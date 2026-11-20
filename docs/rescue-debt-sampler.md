# Rescue-debt sampler

`tools/rescue_debt_sampler.py` runs fresh deterministic seeds until it observes
at least 100 rescue-debt episodes, or reaches a maximum seed count. It completes
all nine careers for a seed before checking the target: the existing Hauler,
Crisis, and Pathfinder styles at tanks 3, 4, and 6. The final seed can overshoot
the target. Debt duration is a report, not a gameplay gate.

## Build and run

The tools require Python 3.12 or later, CMake 3.25 or later, and a C++23 compiler
with `std::format` support. Build outside the replaceable `sim/` tree. From the
repository root, with `TMPDIR` already set to the session's scratch directory:

```sh
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run sh -c '
  cmake -S tools -B "$TMPDIR/rescue-debt-build" &&
  cmake --build "$TMPDIR/rescue-debt-build" --target rescue_debt_seed --parallel 2
'
```

The target builds the serial sampling helper and its simulation libraries; it
does not build the imported experiment or gate executables. Expensive builds
and sampling runs on a desktop must use `heavy-run` and the existing desktop
lock. Keep one heavy job per desktop, at most two build workers, and scratch
below 1.5 GiB in the owned session directory. If the non-blocking lock is busy,
skip the local heavy step and record the skip. Do not work around a lock.

For a normal campaign, provision enough campaign time before running:

```sh
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run \
  python3 tools/rescue_debt_sampler.py \
    --seed-driver "$TMPDIR/rescue-debt-build/rescue_debt_seed" \
    --output "$TMPDIR/rescue-debt-sample" \
    --target-debts 100 --max-seeds 300 --years 40
```

This is a bounded campaign invocation, not a request to run a long campaign on
D1 during an implementation session. A tiny local run uses one seed and a
one-year career horizon:

```sh
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run \
  python3 tools/rescue_debt_sampler.py \
    --seed-driver "$TMPDIR/rescue-debt-build/rescue_debt_seed" \
    --output "$TMPDIR/rescue-debt-tiny" \
    --target-debts 100 --max-seeds 1 --years 1
```

The tiny run normally reports `max_seeds` and exits 2 because it has not met
the 100-episode target. That outcome preserves a useful report; it is not a
simulation failure. Use a new output directory for every invocation. The
sampler refuses an existing directory and does not resume campaigns.

Options are `--seed-driver` and `--output` (required), `--target-debts` (default
100), `--max-seeds` (300), `--start-index` (0), `--years` (40), and
`--seed-timeout` (300 seconds per complete seed). Counts and timeout must be
positive; the horizon must be 1 through 1,000,000 years. The timeout bounds an
individual helper process; a timed-out seed is not counted.

## Seeds and episode counts

The helper matches `pilot_exp` wide mode's existing deterministic schedule:

- Seed: `hashN(20261120, seedIndex)`.
- Start year: `20 + 60 * (seedIndex % 3)`, cycling through 20, 80, and 140.
- Major count: five when `seedIndex % 5 == 4`, otherwise four.
- Careers: Hauler, Crisis, and Pathfinder at tanks 3, 4, and 6.

Indices must stay in `[0, 2**32)`, including the whole requested maximum-seed
range. The hash takes uint32 arguments; prohibiting wrap avoids repeated seeds.
Each helper invocation receives one index, so adding seeds never reruns and
recounts a prefix. The controller also rejects duplicate returned seeds and
requires a complete nine-career sweep before counting anything.

An episode starts when a rescue adds debt and no rescue-debt episode is already
open. It clears when all debt reaches zero. Repeated rescues while an episode
is open do not create additional episodes. Wallet-paid rescues are not debts.
The stopping count is the number of `debtClear` entries plus one for each
`debtOpen >= 0`. The `-1` sentinel means no open episode; an open age of zero
still counts. Rescue-price entries are retained for inspection but never drive
the target.

Both tools reject the presence of `APSIS_SET`, `APSIS_OFF`, `APSIS_DRIVE`,
`APSIS_MSPACE`, or `APSIS_MMASS`, including empty values. Unset them before
running so the sample uses the imported default rules and parameters.

## Outputs and outcomes

The output directory contains:

- `seed-NNNNNNNNNN.json`: one complete seed's metadata and nine career records,
  including completed durations, open age, rescue prices, ledger status, and
  career end state/day.
- `summary.json`: target, actual episode count, overshoot, outcome, maximum and
  completed seed counts, next index, requested horizon, helper path, timeout,
  per-style statistics, and the seed/start/major-count/tank manifest with links
  to seed filenames.
- `report.md`: a readable report with every style at each tank and with tanks
  3/4/6 pooled by episode. Pooled rows repeat the strata and are never added
  again to the stopping count.

After every completed seed the sampler updates the summary and report. Files
are replaced atomically individually. If a helper fails, emits invalid output,
times out, or is interrupted, the sampler records the outcome and retains only
previously completed seeds in its count. This checkpoint supports inspection
and choosing a later `--start-index`; it is not automatic resume or aggregation
across separate invocations. Preserve output before session scratch is deleted.

Exit codes are 0 for `target_reached`, 2 for `max_seeds`, 1 for a runtime error,
and 130 for interruption. Invalid command-line arguments also exit 2 before a
campaign starts. A maximum-seed outcome explicitly says the requested target
was not reached; it never pretends the available sample is 100 episodes.

Each report row gives the career, total-episode, completed-episode, and
open-episode counts. Observed p50/p90 combine completed clearing durations with
open ages so far. Open ages are **lower bounds at career observation end**,
which can precede the requested horizon at death or bankruptcy. These mixed
statistics are not estimates of completed clearing times. `summary.json` also
provides separate `completedDurationsOnly` counts and quantiles.

Units and quantiles follow `sim/tools/aggwide.py`: 360 days per year, median
p50, and nearest-rank p90 (`ceil(0.9 * count)`, using a one-based rank). Empty
groups remain present with zero counts and `null` quantiles in JSON, rendered
as `n/a` in Markdown. The adaptation includes zero-day open episodes and uses
the requested horizon rather than the imported aggregator's fixed 40-year text.

## Imported hooks and follow-up work

`tools/rescue_debt_seed.cpp` includes the imported
`sim/tools/pilot_exp.cpp` with its main renamed, then calls its existing
`career()` helper serially. The imported threaded main is never invoked.
The adapter uses existing style constants, tank values, world creation and
stepping, and career results. Those results come from
`Pilot.rescueDebtFrom` and `debtClear` in `sim/src/world/pilotstate.hpp` and the
unchanged debt handling in `sim/src/pilot/pilot.cpp`.

The import has no debt-count stop, seed-offset/list option, incremental output,
or public callable career header. The new controller supplies stopping and
checkpoints, and the helper supplies one-index execution without changing
`sim/`. Including the imported tooling implementation is a layer-4a coupling:
review that adapter when replacing `sim/` with a later import. The missing
public helper also requires suppressing the renamed main's return-type warning
only while including that file.

The seed-runner scaffolding from issue #1 / PR #3 is not part of this import
base and is not integrated here. A later change can compose with its documented
interface. CI wiring and tests belong to a separate `Agent-Role: tests` session;
this implementation adds neither tests nor assertions.

Lint the authored Python with `ruff check tools/rescue_debt_sampler.py` and
`ruff format --check tools/rescue_debt_sampler.py`. Format-check the adapter
with `clang-format --dry-run --Werror tools/rescue_debt_seed.cpp` when available.
