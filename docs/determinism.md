# Native determinism CI

`tools/determinism.py` wraps existing native binaries without changing simulation
rules or gate meanings. The `ci` job keeps every import build and gate command.
It reuses their fresh JSON outputs, publishes a compact GitHub job summary, and
uploads `native-determinism` with the summary, source reports, and world run log.
Command failures remain failures even if a binary leaves a passing JSON report.
The report and artifact steps run after failures so partial evidence is available.

The added `world` command invokes `world_gates [years]` in the artifact directory.
It requires an integer `intents.threadsOk == 7`, confirming equal final world
hashes at thread settings 1, 2, and 8 on all seven existing seeds. Missing reports,
invalid JSON, duplicate keys, non-finite numbers, missing fields, and wrong field
types fail the wrapper. It also checks `charMs.years == years * 7` against the
requested horizon and requires nonnegative integer pool/decision counters.

The wrapper defaults to the binary's **300 years per seed**, and CI explicitly
uses that same horizon. All seven seeds and all three thread settings run. The
binary's fixed general-war careers also run unchanged, independently of this
horizon. Existing layer-1 and world/pilot gate horizons and coverage remain
unchanged. Local smoke runs can supply a shorter positive `--years` value, which
is recorded in the report and cross-checked against the binary's output.

Thread settings alone do not establish that parallel decisions ran. The report
therefore shows `intents.poolRuns` (pool-use days summed over 2/8-thread runs),
`deciderDays` (from the 1-thread runs), and `maxDeciders`. A zero pool count is
reported as zero; it does not create another gate. Layer-1's existing daily hash
and exercised-pool check remains separate from this final-world hash comparison.

The report reuses pilot replay, midpoint resume, byte-exact round trip, and
canonical input order (5/5 each), world resume and round trip with intents off/on
(14/14 each), and world corruption teeth (20/20). World corruption is caught when
restore refuses **or the resulting checkpoint differs**. This does not claim
authenticated rejection of every corruption. Pilot teeth is informational,
matching the existing binary's behavior. Switches, endstates, and law outputs
are included as raw evidence; their existing command status remains authoritative.

Before native gates, `prepare` deletes only the known report filenames from the
run directory and artifact directory. It never reads imported `sim/*.json`
snapshots. `world` independently removes its prior output before invocation and
records the requested horizon, command, exit status, and validation error in
`world-run.json`. The final report also requires successful CI step outcomes,
including preparation, so stale or partial evidence cannot mask a failed run.

For a local run, use an isolated scratch directory and an existing native build:

```sh
python tools/determinism.py prepare --report-dir "$RUN_DIR"
# Run the unchanged native layer-1 and world/pilot CI commands in RUN_DIR.
python tools/determinism.py world --build-dir "$BUILD_DIR" --report-dir "$RUN_DIR"
python tools/determinism.py report --report-dir "$RUN_DIR" \
  --build-status success --prepare-status success --layer1-status success \
  --standing-status success --world-status success
```

Pass actual command outcomes to the report; do not label failed runs successful.
The wrapper uses only the Python standard library. Existing CI Ruff checks cover
it. Mismatch, missing/malformed-report, and command-failure fixture tests belong
to a separate `Agent-Role: tests` session. WASM parity remains deferred; this
workflow does not invoke `gates_l2.sh`, port/WASM/wide campaigns, or frozen v0.20.
