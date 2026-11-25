# Read-only Shipyard reports

`tools/shipyard_report.py` measures a corpus or an explicit scalar grid with the
merged Shipyard validator. It writes a new report directory; source definitions,
active rules, schemas, and cached source data are never written. Variant inputs
exist only in memory and are passed to the native adapter on stdin. The tool
neither recommends designs nor changes shipped values.

The standalone adapter calls public `validate()` with its default checks. It
recomputes imported caches using active rules. Structural PASS describes those
checks; it does not certify complete or jump readiness. On the current library,
complete readiness is null on structural PASS and false when structural checks
fail on a loaded definition. If the validator returns no definition data,
complete readiness is null and direct totals are unavailable. Jump readiness
remains null throughout. Performance evaluation belongs to separate Shipyard
tooling.

Build from the repository root, using the host's heavy-work wrapper:

```sh
heavy-run sh -c 'cmake -S tools/shipyard_analysis -B "$TMPDIR/shipyard-analysis" \
  -DCMAKE_CXX_COMPILER=g++-13 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && \
  cmake --build "$TMPDIR/shipyard-analysis" --target shipyard-validate --parallel 2'
```

CMake 3.20+, C++23, and OpenSSL 3.x development files match the existing Shipyard
library requirements. This entry point leaves `shipyard/CMakeLists.txt` and the
root tooling build unchanged. A busy heavy-work slot means defer the build.

Inspect a plan without a binary, then use another fresh directory for the run:

```sh
python3 tools/shipyard_report.py shipyard/fixtures/v3/*definition-v3.json \
  --output "$TMPDIR/shipyard-plan" --dry-run
python3 tools/shipyard_report.py shipyard/fixtures/v3/*definition-v3.json \
  --driver "$TMPDIR/shipyard-analysis/shipyard-validate" \
  --output "$TMPDIR/shipyard-corpus"
```

The default rules and schema directory are `shipyard/rules/apsis-rules-v4.json`
and `shipyard/schemas`. `--rules` and `--schemas` select other existing inputs;
these flags never edit them. Each campaign runs a temporary copy of the adapter
against a snapshot of the regular `*.schema.json` files. Each input is at most
8 MiB. At most 64 total source × grid cases are allowed, with a default 30-second
timeout per case (`--timeout`, bounded to 300 seconds). Larger analyses should
use the host's heavy-work controls and separate bounded plans.

For example, measure Wren's current loaded mass across an explicit cargo and
jump-fuel load grid:

```sh
python3 tools/shipyard_report.py \
  shipyard/fixtures/v3/apsis-wren-definition-v3.json \
  --driver "$TMPDIR/shipyard-analysis/shipyard-validate" \
  --axis '/blueprint/loads/cargo=[0,0.5,1]' \
  --axis '/blueprint/loads/jumpFuel=[0,1]' \
  --output "$TMPDIR/shipyard-load-grid"
```

Each `--axis` is an RFC 6901 JSON Pointer followed by `=` and a JSON array of
scalar values. Pointers address existing scalar fields in the input's blueprint,
including array indices; they cannot address cached totals, validation, or
embedded rules. Raw blueprints use pointers such as `/loads/cargo`. No field is
created and no invalid value is clamped. The unchanged validator reports any
schema or structural failure in a hypothetical candidate. Axis order, input
order, and value order determine the Cartesian grid order; duplicate axes and
duplicate values are rejected. With no axes, each source is measured once from
its original bytes, including sources the native validator rejects as invalid.

Every completed output directory contains:

- `manifest.json` records input/rules/schema hashes, original driver path and schema
  directory, exact adapter command, binary and Python-tool hashes, candidate hashes
  and scalar overrides, status, summary, and report artifact hashes. The adapter
  command records the temporary executable and schema paths used for the run.
  The candidate hash covers the bytes supplied to stdin. It equals the source
  hash in corpus mode and covers the compact JSON candidate in grid mode.
- `results.json` records per-case status, complete/jump readiness, recomputed direct
  totals, migration metadata, and every bounded validator issue. Cases without
  definition data show `"ready": null`, `"jumpReady": null`, and `"totals": {}`.
- `results.csv` records case, source, overrides, status, readiness, issue codes, and
  selected direct totals (`cost`, `dryMass`, `currentMass`, `cargo`, `fuel`).
  Empty readiness cells represent null/unknown, and `False` records failed
  complete readiness. Empty total cells indicate unavailable totals.
  `currentMass` is the active loader's loaded-mass result. `cargo` and `fuel`
  report capacities.
- `report.md`: a compact observation table. Issue occurrences count findings,
  including repeats within a case, rather than affected-case prevalence.

A structural FAIL is a completed measurement and exits zero, including malformed
or out-of-range definitions for which the validator returns no data. Operational
errors (bad rules/schema files, process failure, invalid responses, timeout) exit
2 and leave a failed manifest when a run directory was created. A partially
failed campaign has no completed summary. Existing output directories are
rejected, so a failed run cannot silently reuse old results. Dry runs contain
only a planned manifest.

For the unabridged public validator envelope, invoke the adapter directly:

```sh
"$TMPDIR/shipyard-analysis/shipyard-validate" shipyard/schemas \
  shipyard/rules/apsis-rules-v4.json \
  < shipyard/fixtures/v3/apsis-skiff-definition-v3.json
```

The adapter's exit status also distinguishes a completed validation (0, including
structural FAIL) from operational failure (2, JSON diagnostics on stderr). It
accepts no switches that disable structural checks.

Run unit and optional real-validator integration tests:

```sh
python3 -m unittest discover -s tests -p test_shipyard_report.py -v
APSIS_SHIPYARD_VALIDATE="$TMPDIR/shipyard-analysis/shipyard-validate" \
  python3 -m unittest discover -s tests -p test_shipyard_report.py -v
```

The native cases skip when the environment variable is absent. They cover the
canonical corpus, original source bytes, source immutability, ignored forged
caches, scalar grids, and reports containing both valid and invalid measurements.
