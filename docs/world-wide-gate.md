# Fresh paired world-wide gate

`tools/world_wide_gate.py` invokes the existing native `world_wide` binary in a
new output directory. It defaults to **1,000 paired seeds**, retains the raw
report and process logs, writes a versioned `result.json` and `summary.txt`, and
returns a real gate exit status. It does not build or modify the simulation.
Python 3.12+ is sufficient; no third-party Python packages are needed.

From the repository root, with a binary built from this checkout:

```sh
python3 tools/world_wide_gate.py \
  --world-wide /path/to/build/world_wide \
  --output-dir "$TMPDIR/world-wide-campaign"
```

The output directory must not already exist. The default process timeout is
3,600 seconds; `--timeout-seconds` can select a campaign-appropriate deadline.
Do not use a checked-in historical report as fresh campaign evidence. No
1,000-seed Actions job is added.

A read-only invocation plan (no binary execution or output files):

```sh
python3 tools/world_wide_gate.py \
  --world-wide /path/to/build/world_wide \
  --output-dir "$TMPDIR/world-wide-plan" --seeds 2 --dry-run
```

`--dry-run` returns 0 for printing the plan and labels its status `dry_run`; it
does not claim a gate PASS. For an actual tiny smoke run, omit `--dry-run` and
keep `--seeds 2`. Non-1,000 samples are explicitly labeled `smoke_or_custom` in
the result and do not stand in for the requested 1,000-seed campaign.

On D1, all native builds and executions must use the existing heavy-work
wrapper and nonblocking desktop lock. For example, with an unused build path:

```sh
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run \
  cmake -S sim -B "$TMPDIR/world-wide-build" -DCMAKE_CXX_COMPILER=g++-13
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run \
  cmake --build "$TMPDIR/world-wide-build" --target world_wide --parallel 2
flock -n /tmp/desktop-temp-heavy.lock env HEAVY_WAIT=1 heavy-run \
  python3 tools/world_wide_gate.py \
  --world-wide "$TMPDIR/world-wide-build/world_wide" \
  --output-dir "$TMPDIR/world-wide-smoke" --seeds 2 --timeout-seconds 120
```

Stop if a lock is busy. The imported binary uses `hardware_concurrency()` and
has **no worker-limit flag**. Two seeds bound the number of active simulation
workers to two for a local smoke run. A full campaign needs an environment
whose worker policy permits the binary's hardware-derived concurrency, or a
future upstream worker-limit hook. CPU affinity alone is not a worker-count
hook. This adapter does not change that sim behavior.

## Decisions and failure handling

The existing `sim/gates_l2.sh` war decision is preserved:

```text
PASS if 0.75 <= (on war rate / off war rate) <= 1.25
     OR the two 95% Wilson intervals overlap (inclusive boundaries)
```

Both arm counts, seed denominators, rates and intervals are printed and saved.
Wilson uses the existing `z = 1.96`. If off has zero wars, the ratio is null
and the overlap alternative still applies; this avoids division by zero
without adding a new balance rule. Wilson intervals concern seed-level war
events, not the paired difference or correlated dip observations.

Exit codes are 0 for PASS, 1 for a failed war decision, and 2 for execution,
timeout, report, provenance or output errors (including CLI errors). A
nonzero native exit, including an assertion abort, is an error even if it
left a report behind. Missing reports also fail: `world_wide` does not itself
fail when its output-file write fails. The adapter rejects malformed JSON,
duplicate keys, non-finite JSON constants, missing required fields, boolean
or negative counts, inconsistent denominators, and inconsistent dip counts
or capacity values. It retains logs and writes an error result when possible.
It never overwrites a previous output directory.

This is one aggregate adapter, not the entire legacy layer-2 suite. The
legacy script's other standing gates are outside this result. Frozen v0.20
is absent per `sim/SOURCE.md`; the result marks the port gate `unavailable`.
It never calls the legacy script or supplies a substitute v0.20.

## Recovery beside depth

Each arm reports these **descriptive dip-level** quantities together:

| Figure | Definition |
| --- | --- |
| `fed/episodes` | Share observed recovering above the supply line within **at most 30 days** |
| `slow`, `slow_share` | Count and `slow/episodes`: recoveries taking **more than 30 days**, plus dips still open at the horizon and older than 30 days |
| `slowLowMeanPm`, `slowLowP10Pm` | Mean lowest stock and lowest-stock p10, retained as raw per-mille values and converted to labeled capacity percentages by dividing by 10 |
| `slowEmpty`, `slow_empty_share` | Count reaching zero stock and `slowEmpty/slow` |
| `slowDaysMean` | Mean observed days below the line among slow dips |
| `horizon_open_at_most_30_days` | `episodes - fed - slow`; these younger open dips remain in the recovery denominator |

Lower lowest-stock values mean deeper dips. “Slow” does not mean exclusively
unrecovered at the horizon: late recoveries are included, and their depths
are pooled with older open dips. The current hook cannot separate those
groups. Open durations are measured only through the horizon. The JSON
means use integer division and can differ slightly from the binary's stdout
or historical `sim/world_wide_l3.txt`; this adapter uses the fresh JSON.
With no dip/slow observations, derived rates/means display as unavailable
and are null in JSON (raw per-mille zero placeholders are retained).

There is no recovery/depth pass threshold in the imported hooks. None is
invented here. Successful report validation is a harness check; recovery
and depth remain descriptive and receive no seed-level confidence interval.

## Artifacts, provenance and future hooks

`result.json` schema version 1 contains `status`, `sample`, invocation and
timing metadata, checkout `revision` and `source_dirty`, binary SHA256 and
`binary_revision_verified: false`, `arms`, `war_gate`, `report_check`, and
the unavailable `port_gate`. Error results can lack fields not yet obtained.
The checkout revision records harness/source context; a supplied binary's
build revision is not discoverable through the current CLI and is **not
attested**. Build from the recorded checkout and retain build provenance
alongside campaign artifacts. `stdout.txt`, `stderr.txt`, and the fresh
`world_wide_l2.json` preserve the original evidence.

The seed recipe is `apsis::hashN(20261118, i)` for `0 <= i < N`, paired with
intents off/on; majors are five when `i % 5 == 4`, otherwise four. Both arms
warm up 20 years, then measure 40 years through year 60, at 360 days/year.
The adapter checks that each aggregate `n` equals the requested seed count.
**No per-seed completion records, paired-outcome records or seed manifest
are exposed**, so it cannot independently confirm all 1,000 individual
records. That missing hook is reported rather than fabricating records.

Issue #1 / PR #3's seed-runner scaffolding is absent from this base. The
local Wilson helper follows its public interval object shape (`method`,
`confidence`, `lower`, `upper`), adding `z` to make the legacy constant
explicit. A later integration can compose the statistics helper while
preserving `z = 1.96`; the aggregate binary cannot honestly implement the
runner's individual-seed outcome hook yet.

Hooks: `sim/tools/world_wide.cpp`, `sim/gates_l2.sh`, `sim/world_wide_l2.json`
(schema context), `sim/world_wide_l3.txt` (historical explanation), and
`sim/SOURCE.md`. No sim source, game rules, assertions, goldens or CI wiring
are changed. Tests belong to a separate `Agent-Role: tests` session.

Refs: https://github.com/gobha-me/apsis/issues/9
