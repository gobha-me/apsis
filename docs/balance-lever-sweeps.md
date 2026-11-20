# Balance-lever comparisons

`tools/lever_sweeps.py` runs four matched experiment cases and produces four
small comparison tables: tank, hull mass, drive, and hull space. It measures
explicit alternatives without selecting new shipped defaults. The driver uses
only the standard library and the existing native `pilot_exp` executable.
Build that executable from the same source tree as the driver; the manifest
records its SHA-256 and the source revision for review.

The baseline uses the defaults read from `sim/src/core/params.def`. Supply one
alternative for each of the other cases:

| Lever | Argument | Parameter and unit | Imported baseline |
| --- | --- | --- | --- |
| Hull mass | `--mass-param mediumMass --mass N` | Medium hull `mediumMass`, mass | 40 |
| Hull mass | `--mass-param heavyMass --mass N` | Heavy hull `heavyMass`, mass | 80 |
| Drive | `--drive N` | `stockDrivePm`, per mille J | 1500 |
| Hull space | `--space N` | Medium hull `mediumSpace`, space | 120 |
| Tank | `--tank N` | Comparison baseline tank, jumps | 6 |

Choose exactly one hull mass parameter. Space always varies the medium hull's
space budget; heavy hull mass and medium hull mass are distinct comparisons.
Mass, space, and drive alternatives must differ from their defaults. Drive
choices are 500, 1000, 1500, 2000, and 3000 per mille J, selecting existing
discrete drive classes. The options describe experiment inputs, not recommended
values.

`pilot_exp` explicitly sets career tanks to 3, 4, and 6. Changing `stockTank`
through `APSIS_SET` is therefore not an arbitrary tank-sweep interface. The tank
table reuses those three tank results from the baseline case: the selected
`--tank` is its baseline column, and the other two tanks are alternatives. The
mass, drive, and space tables compare their case against the baseline at the
same selected tank. Each case changes only its named lever.

## Plan and run

Run commands from the repository root. First inspect a plan; this creates a new
output directory and manifest without launching or requiring a native binary:

```sh
python3 tools/lever_sweeps.py run \
  --pilot-exp "$TMPDIR/apsis-native/pilot_exp" \
  --output "$TMPDIR/lever-plan" \
  --mass-param mediumMass --mass 45 --drive 2000 --space 130 \
  --years 40 --tank 6 --dry-run
```

The values 45, 2000, and 130 are hypothetical alternatives for demonstrating the
interface. They express no preference. To compare heavy hull mass instead,
replace the mass arguments with, for example, `--mass-param heavyMass --mass
90`. Horizon defaults to 40 years and must be positive; tank defaults to 6 and
must be 3, 4, or 6. Use an absolute path for `--pilot-exp` and a fresh output
directory for every plan or campaign. An existing output directory is rejected.
Mass, space, and horizon inputs are bounded to positive signed 32-bit integers
to avoid overflow in the native runner's integer parsing and arithmetic.

Native builds and campaigns must use the host's existing exclusive heavy-work
lock and wrapper. On a desktop, acquire the lock without waiting; skip the step
and record the skip if it is busy. Keep one heavy job per desktop, and keep
build and run artifacts in the session's owned `TMPDIR` within its documented
storage budget. The underlying build commands are:

```sh
cmake -S sim -B "$TMPDIR/apsis-native"
cmake --build "$TMPDIR/apsis-native" --target pilot_exp --parallel 2
```

The imported `pilot_exp` launches workers from `hardware_concurrency()` and has
no worker-count option. A short horizon does not cap those workers or remove
world generation. Launch a desktop campaign only if the host's worker and
resource policy permits this executable. Planning and report regeneration do
not need a native build.

On a host authorized for the native campaign, submit the same `run` command to
its heavy-work controls, omit `--dry-run`, and choose another new output
directory. The driver runs baseline, mass, drive, and space cases sequentially.
Each invokes the curated `pilot_exp YEARS` mode; it does not use the optional
wide-sample argument. Every alternative therefore uses the same seven curated
seeds, three starts per seed, horizon, and three layer-3 styles (`hauler`,
`crisis`, and `pathfinder`).

The driver rejects inherited nonempty `APSIS_SET`, `APSIS_DRIVE`,
`APSIS_MSPACE`, `APSIS_MMASS`, and `APSIS_OFF`. Clear these before starting a
plan or campaign. It sets each case's explicit `APSIS_SET` to the complete
recorded defaults plus that case's single override. This avoids relying on a
binary's compiled parameter defaults and avoids
legacy overrides, which `pilot_exp` reapplies inside careers, silently
conflicting with a declared parameter set.

## Artifacts and reporting

The output directory contains `manifest.json`, separate `baseline/`, `mass/`,
`drive/`, and `space/` directories, and generated reports. Each completed case
has its own `pilot_l3.json`, `stdout.log`, and `stderr.log`; fixed simulator
filenames never overwrite imported data. The manifest records parameter
defaults and units, case overrides and commands, horizon, and provenance so the
exact alternatives can be reviewed alongside the results.
It records all seven seed/start fixtures and the career tank overrides as well.
Reports check those fixtures, matched career/range keys, total pilot-years,
and artifact hashes before comparing results. Native JSON does not record its
own parameters, horizon, or executable provenance; the driver supplies these.

`reports/` contains `<lever>.table.json` and `<lever>.md` for each lever, and
the root `report.md` combines the four tables. The JSON cells contain formatted
aggregates. The driver feeds those files to the unchanged
[`tools/report_table.py`](../tools/report_table.py) renderer; see the
[table format](report-table.md).

Regenerate reports from a completed campaign without running the simulator:

```sh
python3 tools/lever_sweeps.py report "$TMPDIR/lever-results"
```

An uncompleted plan cannot be reported. Keep the manifest and all case data
together when moving a completed campaign.

Each style has rows for pay, refit timing, strandings per 1,000 legs, opening,
and range. Aggregation follows the relevant `sim/tools/agg3.py` definitions:

- Pay is the median of the simulator's `net`, expressed per requested career
  year. The simulator divides worth change by the requested horizon, including
  careers ending early through death; this is not pay per surviving year.
- Refit times are medians among careers that reached each recorded spend
  threshold (2,500 / 7,500 / 15,000 money), converted using 360 days per year.
  Reached counts accompany them.
  These thresholds track refit spending, not medium/heavy hull transitions.
- Strandings per 1,000 legs are `1000 * sum(strand) / sum(legs)`, not the mean
  of per-career rates. Zero total legs is reported as unavailable.
- Opening measures use first survey and first dark out-and-back timing, with
  reached counts and year-one dark-trip counts. Unreached events are excluded
  from timing medians.
- Range counts homes with a dark system within effective range, within pushed
  range, or reachable out and back on one tank without pushing. These home
  counts are style-independent and are repeated in each style's row for a
  self-contained comparison.

The driver does not invoke `agg3.py`, whose additional contested-clearings
summary assumes a 40-year horizon. It computes the selected aggregates for the
requested horizon and leaves table formatting to the renderer. These reports
measure outcomes; they do not choose a stock tank, hull mass, drive, or space.

Lint the authored Python with `python3 -m ruff check tools/lever_sweeps.py` and
`python3 -m ruff format --check tools/lever_sweeps.py` (Ruff 0.14.6, matching CI).
Tests are deferred to a separate session with `Agent-Role: tests`.
