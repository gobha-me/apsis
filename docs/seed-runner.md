# Seed runner and confidence intervals

This framework defines execution and result records for a future simulation
adapter. It contains no simulation, game rules, harvest logic, or pass criteria.
It uses Python 3.12 or later and only the standard library at runtime.

From the repository root:

```sh
python -m tools.seed_runner --seeds 1 2 3 --confidence 0.95
```

With no hook, every seed is `skipped`, the summary is `no_data`, and the command
exits with code 2. Placeholder execution never counts as a passing run. Seeds
are integers, deduplicated in input order; negative seeds are allowed by the
interface. CLI seed tokens must contain ASCII digits with an optional sign,
without surrounding whitespace or underscores. Empty seed collections are
allowed through the Python API and have the same `no_data` summary.

## Adapter interface

The Python entry point is
`run_seeds(seeds, hook=None, confidence=0.95)` in `tools.seed_runner`.
A hook receives one integer seed and returns `SeedOutcome` with these fields:

| Field | Format |
| --- | --- |
| `status` | `passed`, `failed`, or `skipped`; the adapter defines pass criteria |
| `metrics` | Optional dictionary of string names to finite numeric values |
| `note` | Optional string |

No hook implementation is included. A future trusted adapter can be selected
with `--hook package.module:function`; running that option imports and executes
Python code. Adapters should import `SeedOutcome` from `tools.seed_runner`.
Hook exceptions or invalid returns produce an `error` record and execution
continues with the next seed. Error notes contain the exception type, without
exception text.

The Python API propagates `SystemExit` and `KeyboardInterrupt`. The CLI catches
these interruptions, writes a diagnostic to stderr, and exits without a report.
Hook stdout is redirected to stderr during CLI execution so progress messages
do not corrupt the JSON document.

## JSON record format

The output is one JSON document with `schema_version: 1`, a `results` array, and
a `summary` object. Every result contains `seed`, `status`, `metrics`, and `note`.
The summary contains:

| Field | Meaning |
| --- | --- |
| `status` | `failed` if any seed failed or errored; otherwise `no_data` when none completed, `incomplete` when some were skipped, or `passed` |
| `total`, `completed` | Distinct requested seeds; completed means passed plus failed |
| `passed`, `failed`, `skipped`, `error` | Outcome counts |
| `pass_rate` | Passed / completed, or `null` with no completed observations |
| `pass_rate_interval` | Object with `method: "wilson"`, `confidence`, `lower`, and `upper` |

The interval is a two-sided Wilson score interval for the pass probability.
Confidence is a fraction strictly between 0 and 1. Skipped and errored seeds are
excluded from its denominator; bounds are `null` when that denominator is zero.
This interval assumes independent binary observations from the intended seed
distribution. A hand-picked seed set or systematic adapter failures cannot
establish population confidence; the counts remain visible for interpretation.
The public `wilson_interval(passed, completed, confidence=0.95)` helper exposes
the same calculation without executing seeds.

CLI exit codes are 0 for `passed`, 1 for `failed`, and 2 for `no_data` or
`incomplete`. Invalid CLI input also exits with argparse's code 2 and writes no
result document. Hook import errors are invalid CLI input. Interrupted execution
or a report that cannot be serialized exits with code 2, a stderr diagnostic,
and no result document. Callers distinguish these errors from `no_data` or
`incomplete` by the absence of a JSON report on stdout. No simulation-specific
threshold or balance decision is made.

## Validation ownership

Lint with `python -m ruff check tools` and `python -m ruff format --check tools`
using Ruff 0.14.6. Run the existing tests with
`python -m unittest discover -s tests -v`. The `ci` job runs lint, tests, and the
bootstrap checks. Test changes belong to separate sessions whose commits use
`Agent-Role: tests`; production-code sessions leave tests unchanged.
