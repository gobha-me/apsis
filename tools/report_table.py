"""Emit a small Markdown comparison table from already-formatted values."""

import argparse
import html
import json
from pathlib import Path
import sys


def _cell(value: str) -> str:
    """Keep table delimiters and line breaks inside a single cell."""
    return (
        html.escape(value, quote=False)
        .replace("\\", "&#92;")
        .replace("|", "&#124;")
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
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))
    print(table)


if __name__ == "__main__":
    main()
