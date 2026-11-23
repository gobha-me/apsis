# Bounded run viewer

This is simulation tooling: a small WASI command exports one deterministic replay,
and the static page plays its recorded daily states. The browser does not execute
the simulation. All adapter code and generated outputs live outside the replaceable
`sim/` tree. No sim rules, events, or gate criteria change.

See [replay inspection and reproduction](../../docs/replay-viewer.md) for the
recorded entity inspector, exact native round trips, and optional browser smoke.

From the repository root, with the same Python `ziglang` package used by
`sim/build_wasm.sh` and Node's WASI support available:

```sh
sh tools/viewer/build.sh wasm
node sim/tools/run_wasm.mjs tools/viewer/build/viewer.wasm \
  < tools/viewer/run.txt > tools/viewer/run.json
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/viewer
```

Open <http://127.0.0.1:8000>. Use Play/Pause, Step one day, or the day slider.
Reload returns to the baseline and reloads the same artifact. Open exported JSON
also accepts a saved artifact. Generated binaries and `run.json` are ignored by git.
The page needs no package install, network service, or browser WASI shim.

Wrap compilation and replay commands in the host's existing exclusive heavy-work
lock and wrapper. A busy lock means skip the local build; do not bypass it. Build
outputs belong in an owned local scratch directory. The portable build script
itself does not manage the host's lock.

If Zig is unavailable, the identical adapter can export natively with a C++23
compiler (GCC 14 was used for local verification):

```sh
sh tools/viewer/build.sh native
tools/viewer/build/viewer-export < tools/viewer/run.txt > tools/viewer/run.json
```

The build records the current git HEAD in `metadata.adapterRevision`; build from
the same committed source to reproduce an artifact. It uses `APSIS_NO_THREADS`
and, for WASM, the existing `wasm32-wasi` command target and `-fno-exceptions` flags.
This is an export adapter, not a browser step/state ABI. WASM compilation requires
Zig; a successful native build alone does not verify that target.

## Run record and bounds

`run.txt` is the editable, complete replay recipe. Blank lines and `#` comments are
allowed. The first row is `seed majors startDay horizon station`. The supplied
recipe uses seed 333860, four majors, a day-9000 warmup (25 simulation years), a
720-day horizon, and start station 0. Its timed flight inputs target home stations
1, 2, 3, and 0. It does not promise war-gate statistic threshold changes, rescues,
or lost-in-space deaths: the sim determines outcomes and the viewer displays absent
observations plainly.

Supported bounds are a uint32 seed, **3–5 majors**, start day 0–36000, horizon
1–3600 days, and at most 4095 timed inputs. The adapter checks the start station
against the generated world. Six majors are excluded because this import indexes
the six-element `mapSide` array by the major count. The tooling restriction avoids
that existing edge case without editing the sim.

Every following row contains eight integers:

```text
day kind f0 f1 f2 f3 f4 f5
```

`day` is the **absolute pre-tick world day**, from startDay through
startDay + horizon − 1. Fields follow `apsis::Action` and
[`pilot.hpp`](../../sim/src/pilot/pilot.hpp); all six fields must be provided.
For example, `9001 6 1 -1 0 1 0 0` submits a pushed, empty FLY to station 1 on day
9001. The sim may ignore an inapplicable action; submission does not imply success.
Inputs on the same day use the sim's canonical kind/field order.

The adapter creates the world with the current default rules, core parameters,
and world parameters; it supports no settings overrides. It warms up with `step`,
records `A_START` at startDay, then uses only recorded `submit` inputs and `tick`.
Additional START actions are rejected to preserve one pilot lifetime. The baseline
frame is before START; the next frame is after that day's input processing and tick.
Pre-start warmup observations are outside the exported history window.

The JSON records resolved rules and every core/world setting, start, horizon,
canonical timed inputs including START, initial/daily frames, the tooling timeline,
and the final `apsis::hashBytes(checkpoint(world))`. The required `finalCheckpoint`
field encodes those exact checkpoint bytes as nonempty lowercase hex. This hashes
the full world and pilot, matching pilot replay; `hashState` alone omits the pilot.
Int64 settings, input fields, counters, and money are decimal strings to preserve browser precision.
Days and bounded identifiers use JSON numbers. Re-exporting the same recipe with
the same source/settings reproduces its timeline and final checkpoint hash.

## History-log gap

`World.log` is only the input journal and is not checkpointed. No complete seed
outcome history exists. The adapter therefore owns this bounded observation list:

- **Derived daily war-gate statistic threshold changes:** sample `generalWarPairs`
  after each tick using exactly `hostilePairs * 2 >= pairs && pairs >= 2`. The UI
  labels this "War-gate statistic (derived, daily)". The starting statistic is a
  baseline; neither it nor a threshold change is a war state or
  declaration.
- **Derived rescues and lost-in-space deaths counter deltas:** compare
  `Pilot.n.strand` between frames. This counter increments on both rescue and
  lost-in-space death. A delta is a derived observation, not a new `Pilot.ev` event.
- **Actual pilot outcomes:** copy each new `[day, kind, arg]` from `Pilot.ev`.
  `EV_RESCUED` (kind 5) uses its real `arg` as the rescue price. Other existing
  events retain their kinds and arguments, including debt warnings and deaths.

These observations are sampled daily and do not claim a complete sub-tick history.
No event kinds or logging are added under `sim/src/`. Rescue handling immediately
relocates the pilot and sets `busy`; no rescue trajectory exists to animate.
The page shows location, trip status, busy interval, state, and observations only.
The displayed final hash belongs to the completed export, not the selected frame.
Before showing the run or enabling playback, the browser validates run-record,
frame, and timeline field types and recomputes `apsis::hashBytes` from
`finalCheckpoint`, requiring `finalStateHash` to be exactly 16 lowercase
hexadecimal digits.

The exporter also writes `framesHash`, using the same two-lane `hashBytes` algorithm
over the UTF-8 bytes of `apsis-viewer-frames-v1\n`, followed by `finalStateHash`, a
newline, and the compact canonical frames JSON. The frame array preserves day
order. Each frame uses the exporter's `frame()` field order: `day`, `war` (`active`,
`hostilePairs`, `pairs`), then `pilot` (`alive`, `at`, `system`, `busyUntil`,
`tripActive`, `tripDestination`, `end`, `endDay`, `wallet`, `fuel`, `strandings`,
`debt`). The browser reconstructs this encoding from the loaded fields and checks
`framesHash` before rendering. Changing any displayed frame field while retaining
the original evidence therefore fails, even if the checkpoint and its hash remain
untouched. JSON whitespace and object-property order do not affect this check.

The required `timelineHash` binds the complete displayed run record and observation
timeline to `finalStateHash`, independently of the unchanged `framesHash`. Its
UTF-8 evidence is `apsis-viewer-timeline-v1\n`, `finalStateHash`, a newline, compact
canonical run JSON, a newline, and compact canonical timeline JSON. Run fields
are ordered `seed`, `majors`, `startDay`, `horizon`, `station`, `settings`, `inputs`.
Settings groups are `rules`, `core`, `world`, with setting names sorted within each
group and array order preserved. Inputs retain their recorded order and use fields
`day`, `kind`, `fields`. Timeline entries retain their recorded order and start
with `day`, `type`, `source`, followed by `active`, `hostilePairs`, `pairs` for
`general-war`; `delta`, `total` for `stranding`; or `kind`, `arg` for `pilot-event`.
The browser reconstructs this encoding, so whitespace and object-property order
do not affect the digest.

Run scalar fields and input days/kinds must be bounded integers. Rule settings
must be booleans; core settings and all input fields must be int64 decimal strings;
world settings may also contain nested arrays of those strings. Timeline entries
must have exactly their type's fields: war observations require a boolean and
nonnegative integer pair counts, stranding deltas/totals require int64 decimal
strings, and pilot events require an integer kind and int64-string argument.
Null entries, unexpected fields, and object-valued payloads are rejected before
rendering.

Missing or malformed checkpoint/hash fields, malformed run/frame/timeline fields,
and digest mismatches leave the run hidden with an error and disabled playback.
Older exports without `finalCheckpoint`, `framesHash`, or `timelineHash` must be
regenerated. Chosen files and fetched `run.json` are both limited to 32 MiB.

These are consistency checks on supplied checkpoint, frame, run-record, and
timeline evidence, not cryptographic signatures, authentication, or independent
verification that the frames or observations resulted from the simulation.
Anyone editing the export can also forge the evidence. The browser does not rerun
the simulation. Metadata is not covered by these digests.

Existing viewer coverage lives under `tests/`; test and fixture updates belong to
a separate `Agent-Role: tests` session.
