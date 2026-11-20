# Comparison report tables

`tools/report_table.py` emits a small side-by-side Markdown table for later
balance-lever sweeps or other comparisons. It uses Python 3.12 or later and only
the standard library. It does not run a simulation or calculate metrics, deltas,
confidence intervals, or a preferred outcome.

The JSON input has two fields:

- `columns`: a list of at least three nonempty strings. Put the metric label
  first, followed by the baseline and one or more comparison labels.
- `rows`: a list of lists, each with exactly one string per column. An empty
  string represents an unavailable value. An empty row list is allowed.

All values are strings so callers choose units, precision, and interval notation
before rendering. Column and row order are preserved; there is no sorting.

```json
{
  "columns": ["Metric", "Baseline", "Candidate A"],
  "rows": [
    ["Run label", "pending", "pending"],
    ["Pass rate (95% CI)", "", ""]
  ]
}
```

Save the input as `comparison.json`, then run:

```sh
python3 tools/report_table.py comparison.json > comparison.md
```

Omit the path or use `-` to read stdin. Markdown is written to stdout with a
trailing newline. Invalid JSON, invalid table shape, non-string cells, and input
file errors produce a diagnostic on stderr and exit status 2.

The example emits:

| Metric | Baseline | Candidate A |
| --- | --- | --- |
| Run label | pending | pending |
| Pass rate (95% CI) |  |  |

Cells escape `&`, `<`, `>`, backslashes, and pipes. CRLF, CR, and LF line breaks
become `<br>` within the cell. Other inline Markdown is retained. The helper
always uses left-aligned columns; provide signs, units, and explanatory row
labels explicitly.

For callers in the repository, `from tools.report_table import render_table`
exposes the same renderer as `render_table(columns, rows)`, returning Markdown
without a trailing newline and raising `ValueError` for invalid table shape.

Lint: `ruff check tools/report_table.py` and `ruff format --check tools/report_table.py`
with Ruff 0.14.6. No tests are included in this scaffolding change. Tests are
deferred to a separate session whose commits will use `Agent-Role: tests`.
