# Apsis

Apsis is being set up. No simulation code is included yet.

The `ci` workflow creates a placeholder build and runs bootstrap checks.

[Seed runner and confidence intervals](docs/seed-runner.md) define a simulation-free
execution hook, per-seed JSON records, and pass-rate summaries.

The [comparison report table format](docs/report-table.md) and its small Python
helper are available for future balance-lever sweeps and other comparisons.

The [bounded run viewer](tools/viewer/README.md) exports one seeded simulation
replay through WASI or native C++ and plays its recorded state in a static page.
