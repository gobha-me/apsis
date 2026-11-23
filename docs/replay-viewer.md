# Replay inspection and reproduction

The standalone viewer in [`tools/viewer`](../tools/viewer/README.md) loads
`apsis-viewer-v1` JSON exported by the existing native/WASI simulation adapter.
It plays recorded daily frames and never modifies game content or runs new
simulation decisions in the browser. Build commands and the format's evidence
digests are documented in that directory's README.

## Inspect a recorded run

Serve `tools/viewer` over HTTP and open the page. The page loads `run.json`, or
use **Open exported JSON** for a saved recording. The loader limits exports to
32 MiB and checks frame, input, settings, timeline, and checkpoint evidence
before enabling playback.

Use **Play**, **Pause**, **Step one day**, and the recorded-day slider to select a
frame. Frame zero is the baseline before `A_START`; later frames are sampled
after the preceding day's timed inputs and tick. Reload returns to the baseline.

The **Recorded entity inspector** selects either the single pilot or the world's
derived war-gate statistic. It shows every recorded field, its value in the
previous frame, and its change. Int64 fields and their differences preserve exact
decimal values beyond JavaScript's safe integer range. At the baseline there is
no preceding recorded frame. Unknown JSON properties are not inspected because
they are outside the frame digest.

The current format records no individual station, ship, or faction snapshots.
The inspector cannot reconstruct their histories from the final checkpoint.
War-gate threshold changes are derived daily observations, not war declarations;
the timeline labels these separately from actual `Pilot.ev` events.

## Replay a recording through the simulator

The tooling CLI recovers the complete input recipe from a recording, injects no
additional actions, runs the native exporter, and compares all seven evidence
categories: resolved run/settings/inputs, frames, timeline, final checkpoint,
final checkpoint hash, frame hash, and timeline hash.

Wrap native builds and replays in the host's existing heavy-work controls. With
an exporter already built from the source tree being measured, run:

```sh
python3 tools/viewer/replay_roundtrip.py \
  tests/fixtures/viewer/recorded-run.json \
  --exporter "$TMPDIR/apsis-viewer-export" \
  --recipe-output "$TMPDIR/recovered-run.txt"
```

The exporter path must refer to the existing `tools/viewer/export.cpp` adapter.
The command prints a small JSON summary including hashes of the exporter and
recorded file. It writes the optional recipe only after an exact match and
refuses to overwrite an existing recipe file. A malformed recording, timeout,
exporter failure, settings drift, or mismatched evidence returns nonzero.
The default timeout is 60 seconds and can be set to 1–300 seconds.

The native exporter uses its own compiled default settings. The round-trip tool
does not apply recorded settings as overrides: a changed default is reported as
a run-record difference. Metadata, including `adapterRevision`, is intentionally
excluded so an unchanged simulation can reproduce an older recording. Keep the
original export and binary provenance when comparing source revisions. Matching
exporter evidence is a reproducibility check, not a signature or proof of
independent correctness.

## Verification

The default tests use the real viewer module through a deterministic DOM/clock
adapter and test recipe validation and all evidence comparisons without building
the sim:

```sh
python3 -m unittest discover -s tests -p 'test_viewer_*.py' -v
```

Set `APSIS_VIEWER_EXPORT` to the built native exporter to enable four additional
real round-trip tests. The existing exporter oracle tests also use
`APSIS_VIEWER_OBSERVATIONS`. Compilation and the native test invocation must use
the host's heavy-work controls.

For an optional real browser smoke with Playwright and Chromium available in an
owned scratch environment, start the HTTP server and run:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory tools/viewer
# In another terminal:
node tests/viewer_browser_smoke.mjs http://127.0.0.1:8000/
```

The smoke intercepts `run.json` with the existing pinned recording and checks
loading, inspection, step, scrub, play/pause, reload, and uncaught page errors.
No package installation or CI configuration is changed by this tooling.
