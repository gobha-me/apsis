# Viewer test recording

`recorded-run.json` is the unmodified native export retained from the prior code
session, copied from its durable worktree on D1. It records adapter revision
`728a37406763332d1f6404643ee8e5ea5f731d4e`, seed 333860, four majors, start day
9000, horizon 720, station 0, and all timed inputs and resolved settings. Its 721
frames end at day 9720; the final full checkpoint hash is `2e41d7a6ad443e92`.
Its observation timeline is empty. Tests must preserve that absence.

The file supports deterministic page reload tests without rerunning the 9000-day
warmup. The page tests also construct explicitly synthetic daily frames and
observations to exercise playback and presentation, including derived transitions
and actual-event labels. Those cases are not recorded simulation outcomes.

`test_viewer_export.py` additionally drives the real native exporter and a test
oracle linked to the real sim. Set `APSIS_VIEWER_EXPORT` and
`APSIS_VIEWER_OBSERVATIONS` to their executable paths to enable those checks.
CI builds both after the existing native build and runs them; local runs without
the executables report skips. The Node page tests run through ordinary Python
unittest discovery, importing the real viewer with only DOM, fetch and animation
frame boundaries supplied by the test harness.

A separate expected-failure regression documents the loader accepting a tampered
string checkpoint hash. It asserts rejection, so repairing hash validation will
produce an unexpected success until the marker is removed in a future tests
session. The viewer currently displays the supplied hash without verifying it.
