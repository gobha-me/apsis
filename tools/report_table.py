"""Emit a small Markdown comparison table from already-formatted values."""

import argparse
import html
import json
import os
from pathlib import Path
import sys


def _cell(value: str) -> str:
    """Keep table delimiters and line breaks inside a single cell."""
    return (
        html.escape(value, quote=False)
        .replace("\\", "&#92;")
        .replace("|", "&#124;")
        .replace("\x00", "&#0;")
        .replace("\t", "&#9;")
        .replace("\u2028", "&#8232;")
        .replace("\r\n", "\n")
        .replace("\r", "\n")
        .replace("\n", "<br>")
    )


def render_table(columns: list[str], rows: list[list[str]]) -> str:
    """Render metric and comparison columns in their supplied order."""
    if (
        not isinstance(columns, list)
        or len(columns) < 3
        or any(not isinstance(label, str) or not label.strip() for label in columns)
    ):
        raise ValueError(
            "columns must contain a metric label and at least two comparison labels"
        )
    if not isinstance(rows, list):
        raise ValueError("rows must be a list")
    for index, row in enumerate(rows):
        if (
            not isinstance(row, list)
            or len(row) != len(columns)
            or any(not isinstance(value, str) for value in row)
        ):
            raise ValueError(
                f"row {index + 1} must contain {len(columns)} string cells"
            )

    def line(cells: list[str]) -> str:
        return "| " + " | ".join(_cell(value) for value in cells) + " |"

    return "\n".join([line(columns), line(["---"] * len(columns)), *map(line, rows)])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input", nargs="?", default="-", help="JSON file, or - for stdin"
    )
    args = parser.parse_args()
    try:
        source = (
            sys.stdin.read()
            if args.input == "-"
            else Path(args.input).read_text(encoding="utf-8")
        )
        report = json.loads(source)
        if not isinstance(report, dict):
            raise ValueError("input must be an object with columns and rows")
        table = render_table(report.get("columns"), report.get("rows"))
        # Reject lone surrogates even if stdout permits surrogate escapes.
        table.encode("utf-8")
        print(table, flush=True)
    except BrokenPipeError as error:
        # Prevent interpreter shutdown from retrying the failed stdout flush.
        with open(os.devnull, "w") as sink:
            os.dup2(sink.fileno(), sys.stdout.fileno())
        parser.error(str(error))
    except (OSError, RecursionError, UnicodeError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
