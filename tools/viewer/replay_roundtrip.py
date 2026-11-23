#!/usr/bin/env python3
"""Replay a viewer export through its native adapter and compare recorded evidence.

No simulation settings are changed. The current exporter resolves its own default
settings; equality of the complete run record detects settings/source drift.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MAX_BYTES = 32 * 1024 * 1024
EVIDENCE = (
    "run",
    "frames",
    "timeline",
    "finalCheckpoint",
    "finalStateHash",
    "framesHash",
    "timelineHash",
)
INT64 = re.compile(r"(?:0|-?[1-9][0-9]*)\Z")


def bounded(value, low, high):
    return type(value) is int and low <= value <= high


def int64(value):
    return (
        isinstance(value, str)
        and len(value) <= 20
        and INT64.fullmatch(value) is not None
        and -(2**63) <= int(value) < 2**63
    )


def recipe(artifact):
    """Recover the adapter's exact bounded input recipe, excluding injected START."""
    if not isinstance(artifact, dict) or artifact.get("schema") != "apsis-viewer-v1":
        raise ValueError("expected an apsis-viewer-v1 export")
    run = artifact.get("run")
    if not isinstance(run, dict) or set(run) != {
        "seed",
        "majors",
        "startDay",
        "horizon",
        "station",
        "settings",
        "inputs",
    }:
        raise ValueError("invalid complete run record")
    bounds = {
        "seed": (0, 2**32 - 1),
        "majors": (3, 5),
        "startDay": (0, 36000),
        "horizon": (1, 3600),
        "station": (0, 2**31 - 1),
    }
    if any(not bounded(run[key], *limits) for key, limits in bounds.items()):
        raise ValueError("run fields exceed adapter bounds")
    inputs = run["inputs"]
    if not isinstance(inputs, list) or not 1 <= len(inputs) <= 4096:
        raise ValueError("expected START and at most 4095 timed inputs")
    start = {
        "day": run["startDay"],
        "kind": 0,
        "fields": [str(run["station"])] + ["0"] * 5,
    }
    if (
        inputs[0] != start
        or type(inputs[0].get("kind")) is not int
        or type(inputs[0].get("day")) is not int
    ):
        raise ValueError("first input must be the adapter's injected START")
    rows = [" ".join(str(run[key]) for key in bounds)]
    previous = None
    for entry in inputs[1:]:
        if (
            not isinstance(entry, dict)
            or set(entry) != {"day", "kind", "fields"}
            or not bounded(
                entry["day"], run["startDay"], run["startDay"] + run["horizon"] - 1
            )
            or not bounded(entry["kind"], 1, 10)
            or not isinstance(entry["fields"], list)
            or len(entry["fields"]) != 6
            or not all(int64(field) for field in entry["fields"])
        ):
            raise ValueError("invalid timed input")
        key = (entry["day"], entry["kind"], *map(int, entry["fields"]))
        if previous is not None and key < previous:
            raise ValueError("timed inputs must be in canonical day/action order")
        previous = key
        rows.append(" ".join(map(str, (entry["day"], entry["kind"], *entry["fields"]))))
    return "\n".join(rows) + "\n"


def reject_constant(value):
    raise ValueError(f"nonfinite JSON constant {value}")


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON field {key}")
        value[key] = item
    return value


def decode(data):
    return json.loads(
        data, parse_constant=reject_constant, object_pairs_hook=unique_object
    )


def roundtrip(artifact, exporter, timeout=60):
    source = recipe(artifact)
    if any(key not in artifact for key in EVIDENCE):
        raise ValueError("export is missing recorded evidence")
    exporter = Path(exporter).resolve(strict=True)
    # Run in a fresh owned directory; the adapter is stdin/stdout only. Strip
    # inherited experiment settings even though the current adapter ignores them.
    environment = {
        key: value for key, value in os.environ.items() if not key.startswith("APSIS_")
    }
    with tempfile.TemporaryDirectory(prefix="apsis-replay-") as directory:
        result = subprocess.run(
            [str(exporter)],
            input=source,
            encoding="utf-8",
            capture_output=True,
            timeout=timeout,
            cwd=directory,
            env=environment,
            check=False,
        )
    if result.returncode:
        raise ValueError(
            f"exporter exited {result.returncode}: {result.stderr.strip()[:500]}"
        )
    if len(result.stdout.encode("utf-8")) > MAX_BYTES:
        raise ValueError("replayed export exceeds 32 MiB")
    replayed = decode(result.stdout)
    if not isinstance(replayed, dict) or replayed.get("schema") != "apsis-viewer-v1":
        raise ValueError("exporter did not emit apsis-viewer-v1 JSON")

    # JSON encodings distinguish booleans from integers (unlike Python equality),
    # while sorting keys permits harmless object-property order changes.
    def canonical(value):
        return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)

    mismatches = [
        key
        for key in EVIDENCE
        if canonical(replayed.get(key)) != canonical(artifact[key])
    ]
    if mismatches:
        raise ValueError("replay differs in: " + ", ".join(mismatches))
    return source, {
        "schema": "apsis-viewer-roundtrip-v1",
        "matched": list(EVIDENCE),
        "finalStateHash": replayed["finalStateHash"],
        "frames": len(replayed["frames"]),
        "exporterSha256": hashlib.sha256(exporter.read_bytes()).hexdigest(),
        "metadataCompared": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export", type=Path, help="recorded apsis-viewer-v1 JSON")
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument(
        "--recipe-output", type=Path, help="write recovered recipe to a new file"
    )
    parser.add_argument("--timeout", type=int, default=60, help="seconds, 1..300")
    args = parser.parse_args(argv)
    try:
        if not 1 <= args.timeout <= 300:
            raise ValueError("timeout must be 1..300 seconds")
        with args.export.open("rb") as stream:
            data = stream.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES:
            raise ValueError("recorded export exceeds 32 MiB")
        source, summary = roundtrip(decode(data), args.exporter, args.timeout)
        summary["recordedSha256"] = hashlib.sha256(data).hexdigest()
        if args.recipe_output:
            with args.recipe_output.open("x", encoding="utf-8") as stream:
                stream.write(source)
        print(json.dumps(summary, sort_keys=True))
    except (OSError, ValueError, subprocess.TimeoutExpired, RecursionError) as error:
        print(f"replay_roundtrip: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
