# Viewer test recording

`recorded-run.json` is a native export regenerated at adapter revision
`d5289f70de584f72aa9b6dcc6d4d4b57d666c993`, with `timelineHash` subsequently
computed from the unchanged recorded payload. The reproducible recipe is
`tools/viewer/run.txt`: seed 333860, four majors, start day 9000, horizon 720,
station 0, with these absolute pre-tick inputs (A_START is injected by the adapter):

```text
333860 4 9000 720 0
9001 6 1 -1 0 1 0 0
9180 6 2 -1 0 1 0 0
9360 6 3 -1 0 1 0 0
9540 6 0 -1 0 1 0 0
```

To reproduce from the repository root, using an owned scratch directory in
`LANE_TMP` and the desktop's existing heavy-work locks:

```sh
cmake -S sim -B "$LANE_TMP/sim-build" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
HEAVY_WAIT=0 /config/engine/tools/temp-heavy.v1.sh heavy-run \
  cmake --build "$LANE_TMP/sim-build" --target apsis_pilot --parallel 2
HEAVY_WAIT=0 /config/engine/tools/temp-heavy.v1.sh heavy-run \
  g++ -std=c++23 -O2 -fno-exceptions \
  -DAPSIS_VIEWER_REVISION=\"d5289f70de584f72aa9b6dcc6d4d4b57d666c993\" \
  -Isim/src tools/viewer/export.cpp \
  "$LANE_TMP/sim-build/libapsis_pilot.a" "$LANE_TMP/sim-build/libapsis_world.a" \
  -o "$LANE_TMP/viewer-export"
HEAVY_WAIT=0 /config/engine/tools/temp-heavy.v1.sh heavy-run \
  "$LANE_TMP/viewer-export" < tools/viewer/run.txt > tests/fixtures/viewer/recorded-run.json
```

On other hosts, use the local equivalent of the build wrapper. Build outputs
belong in scratch storage. The export retains all timed inputs and resolved
settings. Its 721 frames end at day 9720; the final full checkpoint hash remains
`2e41d7a6ad443e92`, and `framesHash` is `1b7f7ef382e6bf47`. `finalCheckpoint`
contains the 12596 checkpoint bytes as lowercase hex. Its observation timeline
is empty. Its `timelineHash` is `fb93e5f4223ddcee`, computed with the documented
`apsis-viewer-timeline-v1` encoding over the final checkpoint hash, canonical
complete run record, and empty timeline. Tests must preserve that absence and
all three hash pins.

The file supports deterministic page reload and integrity tests without rerunning
the 9000-day warmup. Page tests also construct explicitly synthetic daily frames
and observations to exercise playback and presentation. Their checkpoint bytes
and frame hashes are computed consistently for the real loader; they are not
recorded simulation outcomes.

`test_viewer_export.py` additionally drives the real native exporter and a test
oracle linked to the real sim. Set `APSIS_VIEWER_EXPORT` and
`APSIS_VIEWER_OBSERVATIONS` to their executable paths to enable those checks.
CI builds both after the existing native build and runs them; local runs without
the executables report skips. The Node page tests run through ordinary Python
unittest discovery, importing the real viewer with only DOM, fetch and animation
frame boundaries supplied by the test harness.

`transitions-run.txt` supplies a second real-sim recipe: seed 697804, four majors,
start day 23000, horizon 2100, station 0, and 95 ordinary timed actions. The seed's
war window was selected from the existing `sim/world_l2.json` recording. A bounded
single-seed probe recorded flights along reachable routes with high risk and
ordinary repairs when life-support health fell below 500; the retained action
rows replay without that policy or any overrides to sim state or settings.
Reproduce it with the same exporter:

```sh
HEAVY_WAIT=0 /config/engine/tools/temp-heavy.v1.sh heavy-run \
  "$LANE_TMP/viewer-export" < tests/fixtures/viewer/transitions-run.txt > "$LANE_TMP/transitions-run.json"
```

Its main loop observes the war-gate statistic cross the threshold on day 23910,
then rescues on days 24654 and 25012. The second rescue has delta 1 and cumulative
total 2. `test_real_main_loop_stranding_deltas_and_war_gate_transitions` compares
the entire exported timeline and final checkpoint hash with an independent Python
oracle over raw real-sim snapshots, and pins those observations so neither path
can become vacuous. It detects substituting the stranding total for its delta
(E3) and omitting the update to the preceding day's war statistic (E10).
