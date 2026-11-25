# Apsis

Apsis includes a headless C++23 simulation and Python analysis and test tools.

The `ci` workflow runs Python, native simulation, Shipyard and galaxy tests,
alongside bootstrap checks. [Simulation tooling](docs/sim-tooling.md) explains
the bounded invariant matrix, replay and input-property checks, and existing
standing suites.

[Seed runner and confidence intervals](docs/seed-runner.md) define a simulation-free
execution hook, per-seed JSON records, and pass-rate summaries.

The [comparison report table format](docs/report-table.md) and its small Python
helper are available for future balance-lever sweeps and other comparisons.

The [bounded run viewer](tools/viewer/README.md) exports one seeded simulation
replay through WASI or native C++ and plays its recorded state in a static page.

The [rescue-debt sampler](docs/rescue-debt-sampler.md) runs a bounded sample to
an episode target and reports debt-duration p50/p90 by style and tank.

The imported simulation also has a [fresh paired world-wide gate adapter](docs/world-wide-gate.md)
for 1,000-seed war confidence intervals and descriptive recovery/depth reports.
