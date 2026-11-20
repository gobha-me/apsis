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
trailing newline. Invalid JSON, excessive JSON nesting, invalid table shape,
non-string cells, encoding errors, input file errors, and closed output pipes
produce a diagnostic on stderr and exit status 2.

The example emits:

| Metric | Baseline | Candidate A |
| --- | --- | --- |
| Run label | pending | pending |
| Pass rate (95% CI) |  |  |

In the Markdown source, cells escape `&`, `<`, `>`, backslashes, and pipes as
HTML character references. NUL, TAB, and U+2028 are emitted as `&#0;`, `&#9;`, and
`&#8232;`, respectively. CRLF, CR, and LF line breaks become `<br>` within the
cell. Other inline Markdown is retained, so backtick code spans display these
character references and `<br>` literally. Omit backticks around content that
needs these escapes to render as characters or line breaks.

The helper emits `---` separators without explicit column alignment; alignment
depends on the Markdown renderer. Provide signs, units, and explanatory row
labels explicitly.

For callers in the repository, `from tools.report_table import render_table`
exposes the same renderer as `render_table(columns, rows)`, returning Markdown
without a trailing newline and raising `ValueError` for invalid table shape.

Lint: `ruff check tools/report_table.py` and `ruff format --check tools/report_table.py`
with Ruff 0.14.6. Existing renderer and CLI tests run with
`python3 -m unittest discover -s tests -v`. Additional tests for SF1, SF2, and the
nits tracked in [issue #5](https://github.com/gobha-me/apsis/issues/5) will come
from a separate session whose commits use `Agent-Role: tests`.
