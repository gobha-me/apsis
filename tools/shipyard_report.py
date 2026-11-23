#!/usr/bin/env python3
"""Measure an immutable ship-design corpus or a bounded scalar-parameter grid."""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import itertools
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 8 * 1024 * 1024
MAX_CASES = 64
TOTALS = ("cost", "dryMass", "currentMass", "cargo", "fuel")


class ReportError(ValueError):
    """Invalid plan, failed adapter, or untrustworthy response."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def strict_json(text: str):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ReportError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def constant(value):
        raise ReportError(f"nonfinite JSON number: {value}")

    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant)


def read_bytes(path: Path) -> bytes:
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ReportError(f"input exceeds 8 MiB: {path}")
    return data


def pointer_parts(pointer: str) -> list[str]:
    if not pointer.startswith("/") or re.search(r"~(?![01])", pointer):
        raise ReportError(f"invalid non-root JSON pointer: {pointer}")
    return [
        part.replace("~1", "/").replace("~0", "~") for part in pointer[1:].split("/")
    ]


def location(document, pointer: str):
    parts = pointer_parts(pointer)
    if isinstance(document, dict) and "blueprint" in document:
        if len(parts) < 2 or parts[0] != "blueprint":
            raise ReportError(
                "axes must address blueprint fields, never cached/rules data"
            )
    elif parts[0] in {"schema", "rules", "derived", "validation"}:
        raise ReportError("axes must address editable blueprint fields")
    target = document
    try:
        for part in parts[:-1]:
            if isinstance(target, list):
                if not re.fullmatch(r"0|[1-9][0-9]*", part):
                    raise ReportError(
                        "array indices must be canonical nonnegative integers"
                    )
                target = target[int(part)]
            else:
                target = target[part]
        key = parts[-1]
        if isinstance(target, list):
            if not re.fullmatch(r"0|[1-9][0-9]*", key):
                raise ReportError(
                    "array indices must be canonical nonnegative integers"
                )
            key = int(key)
        value = target[key]
    except (KeyError, IndexError, TypeError, ValueError) as error:
        raise ReportError(
            f"pointer does not select an existing field: {pointer}"
        ) from error
    if isinstance(value, (dict, list)):
        raise ReportError(f"axis must select an existing scalar: {pointer}")
    return target, key


def parse_axes(values: list[str]) -> list[tuple[str, list]]:
    axes = []
    seen = set()
    for value in values:
        pointer, separator, encoded = value.partition("=")
        if not separator or pointer in seen:
            raise ReportError("axes require distinct POINTER=[VALUES] arguments")
        pointer_parts(pointer)
        choices = strict_json(encoded)
        if not isinstance(choices, list) or not choices or len(choices) > MAX_CASES:
            raise ReportError("each axis needs 1..64 scalar JSON values")
        if any(isinstance(choice, (dict, list)) for choice in choices):
            raise ReportError("axis values must be JSON scalars")
        canonical = [
            json.dumps(choice, allow_nan=False, sort_keys=True) for choice in choices
        ]
        if len(set(canonical)) != len(canonical):
            raise ReportError("duplicate values in an axis")
        seen.add(pointer)
        axes.append((pointer, choices))
    return axes


def plan(paths: list[Path], axes: list[tuple[str, list]]) -> tuple[list, list]:
    count = len(paths)
    if not 1 <= count <= MAX_CASES:
        raise ReportError("plan needs 1..64 source definitions")
    for _, choices in axes:
        count *= len(choices)
        if count > MAX_CASES:
            raise ReportError("plan exceeds 64 cases; split the corpus/grid")
    sources, cases = [], []
    for path in paths:
        raw = read_bytes(path)
        source = strict_json(raw.decode("utf-8"))
        if not isinstance(source, dict):
            raise ReportError(f"definition must be a JSON object: {path}")
        for pointer, _ in axes:
            location(source, pointer)
        sources.append({"path": str(path.resolve()), "sha256": digest(raw)})
        for combination in itertools.product(*(choices for _, choices in axes)):
            candidate = copy.deepcopy(source)
            overrides = {}
            for (pointer, _), value in zip(axes, combination, strict=True):
                target, key = location(candidate, pointer)
                target[key] = value
                overrides[pointer] = value
            encoded = json.dumps(
                candidate, allow_nan=False, separators=(",", ":")
            ).encode()
            if len(encoded) > MAX_BYTES:
                raise ReportError("candidate exceeds 8 MiB")
            cases.append(
                {
                    "id": len(cases),
                    "source": str(path.resolve()),
                    "sourceSha256": digest(raw),
                    "candidateSha256": digest(encoded),
                    "overrides": overrides,
                    "input": encoded,
                }
            )
    return sources, cases


def summarize(case: dict, response: dict, rules_hash: str) -> dict:
    if (
        not isinstance(response, dict)
        or response.get("schema") != "apsis-shipyard-result-v1"
    ):
        raise ReportError("adapter returned an unsupported result envelope")
    passed = response.get("pass")
    if (
        type(passed) is not bool
        or response.get("operation") != "validate"
        or response.get("status") != ("PASS" if passed else "FAIL")
        or response.get("rulesSha256") != rules_hash
        or response.get("rulesVersion") != 4
    ):
        raise ReportError("adapter result identity/status mismatch")
    issues = response.get("issues")
    if not isinstance(issues, list) or len(issues) > 128:
        raise ReportError("adapter returned invalid or unbounded issues")
    for issue in issues:
        if not isinstance(issue, dict) or any(
            not isinstance(issue.get(key), str)
            for key in ("code", "severity", "message", "path")
        ):
            raise ReportError("adapter returned a malformed issue")
    if passed and any(issue["severity"] == "error" for issue in issues):
        raise ReportError("PASS result contains an error")
    data = response.get("data")
    if data is not None and not isinstance(data, dict):
        raise ReportError("adapter returned malformed data")
    definition = data.get("definition") if data is not None else None
    if data is None and passed:
        raise ReportError("PASS result is missing its definition")
    if data is not None:
        if data.get("structuralPass") is not passed or not isinstance(definition, dict):
            raise ReportError("adapter returned inconsistent structural result")
        if data.get("checks") != {"crewRoutes": True, "approachClearance": True}:
            raise ReportError("adapter did not use the default structural checks")
    validation = definition.get("validation", {}) if definition else {}
    derived = definition.get("derived", {}) if definition else {}
    if not isinstance(validation, dict) or not isinstance(derived, dict):
        raise ReportError("adapter returned malformed definition caches")
    if definition is not None and (
        validation.get("ready") is not (None if passed else False)
        or validation.get("jumpReady") is not None
    ):
        raise ReportError("adapter confused structural approval with readiness")
    return {
        **{key: value for key, value in case.items() if key != "input"},
        "status": response["status"],
        "structuralPass": passed,
        "ready": validation.get("ready"),
        "jumpReady": validation.get("jumpReady"),
        "totals": derived,
        "issues": issues,
        "migration": data.get("migration") if data else None,
    }


def markdown_text(value) -> str:
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace("|", "&#124;")
        .replace("`", "&#96;")
        .replace("\n", " ")
        .replace("\r", " ")
    )


def write_reports(output: Path, rows: list[dict]) -> dict:
    counts = Counter(issue["code"] for row in rows for issue in row["issues"])
    summary = {
        "cases": len(rows),
        "structuralPass": sum(row["structuralPass"] for row in rows),
        "structuralFail": sum(not row["structuralPass"] for row in rows),
        "issueOccurrences": dict(sorted(counts.items())),
    }
    (output / "results.json").write_text(
        json.dumps(
            {"schemaVersion": 1, "summary": summary, "cases": rows},
            indent=2,
            allow_nan=False,
        )
        + "\n"
    )
    with (output / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "id",
                "source",
                "overrides",
                "status",
                "ready",
                "jumpReady",
                "issues",
                *TOTALS,
            ],
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    **{
                        key: row[key]
                        for key in ("id", "source", "status", "ready", "jumpReady")
                    },
                    "overrides": json.dumps(row["overrides"], sort_keys=True),
                    "issues": ";".join(issue["code"] for issue in row["issues"]),
                    **{key: row["totals"].get(key) for key in TOTALS},
                }
            )
    lines = [
        "# Shipyard design-space observations",
        "",
        (
            f"{len(rows)} cases: {summary['structuralPass']} structural PASS, "
            f"{summary['structuralFail']} structural FAIL."
        ),
        "",
        (
            "Structural PASS does not certify complete or jump readiness. "
            "Null readiness remains unknown; performance evaluation is outside this report."
        ),
        "",
        "| Case | Source | Overrides | Structural | Cost | Loaded mass | Issue codes |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        cells = [
            row["id"],
            Path(row["source"]).name,
            json.dumps(row["overrides"], sort_keys=True),
            row["status"],
            row["totals"].get("cost", "unavailable"),
            row["totals"].get("currentMass", "unavailable"),
            ", ".join(issue["code"] for issue in row["issues"]) or "none",
        ]
        lines.append("| " + " | ".join(markdown_text(cell) for cell in cells) + " |")
    lines += [
        "",
        (
            "Issue occurrences count findings, including repeated findings within one case. "
            "The grid is an explicit sample, not a population estimate or design recommendation."
        ),
        "",
    ]
    (output / "report.md").write_text("\n".join(lines))
    return summary


def execute(args) -> dict:
    axes = parse_axes(args.axis)
    sources, cases = plan(args.definitions, axes)
    if not cases:
        raise ReportError("at least one definition is required")
    rules_bytes = read_bytes(args.rules)
    schemas = {
        path.name: digest(read_bytes(path))
        for path in sorted(args.schemas.glob("*.json"))
    }
    if not schemas:
        raise ReportError("schema directory contains no JSON files")
    manifest = {
        "schemaVersion": 1,
        "toolSha256": digest(Path(__file__).read_bytes()),
        "status": "planned",
        "rules": str(args.rules.resolve()),
        "rulesSha256": digest(rules_bytes),
        "schemas": schemas,
        "sources": sources,
        "axes": dict(axes),
        "caseCount": len(cases),
        "timeoutSeconds": args.timeout,
        "command": [
            str(args.driver.resolve()),
            str(args.schemas.resolve()),
            str(args.rules.resolve()),
        ],
        "cases": [
            {key: value for key, value in case.items() if key != "input"}
            for case in cases
        ],
    }
    args.output.mkdir(parents=True, exist_ok=False)
    manifest_path = args.output / "manifest.json"

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n")

    save()
    if args.dry_run:
        return manifest
    try:
        manifest["driverSha256"] = digest(args.driver.read_bytes())
        manifest["status"] = "running"
        save()
        rows = []
        for case in cases:
            result = subprocess.run(
                manifest["command"],
                input=case["input"],
                capture_output=True,
                timeout=args.timeout,
                check=False,
            )
            if result.returncode != 0:
                raise ReportError(
                    f"case {case['id']}: adapter exited {result.returncode}: "
                    f"{result.stderr[:2000].decode('utf-8', errors='replace')}"
                )
            if len(result.stdout) > 4 * MAX_BYTES:
                raise ReportError("adapter response exceeds 32 MiB")
            rows.append(
                summarize(
                    case, strict_json(result.stdout.decode()), manifest["rulesSha256"]
                )
            )
        manifest["summary"] = write_reports(args.output, rows)
        manifest["artifacts"] = {
            name: digest((args.output / name).read_bytes())
            for name in ("results.json", "results.csv", "report.md")
        }
        manifest["status"] = "completed"
        save()
    except Exception as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        save()
        raise
    return manifest


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("definitions", type=Path, nargs="+")
    parser.add_argument(
        "--driver", type=Path, default=Path("build/shipyard-analysis/shipyard-validate")
    )
    parser.add_argument("--schemas", type=Path, default=ROOT / "shipyard/schemas")
    parser.add_argument(
        "--rules", type=Path, default=ROOT / "shipyard/rules/apsis-rules-v4.json"
    )
    parser.add_argument(
        "--axis", action="append", default=[], metavar="POINTER=[VALUES]"
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="new output directory"
    )
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="plan without launching/requiring the adapter",
    )
    args = parser.parse_args(argv)
    try:
        if not 0 < args.timeout <= 300:
            raise ReportError("timeout must be in (0, 300] seconds")
        manifest = execute(args)
        print(f"{manifest['status']}: {manifest['caseCount']} cases in {args.output}")
        return 0
    except (OSError, ValueError, RecursionError, subprocess.TimeoutExpired) as error:
        print(f"shipyard-report: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
