"""Enforce the existing world thread comparison and collect native CI evidence."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

NATIVE_REPORTS = (
    "gates_l1.json",
    "world_core_l2.json",
    "pilot_gates_l3.json",
    "pilot_switches_l4.json",
    "pilot_endstates_l4.json",
    "pilot_law_l4.json",
)
WORLD_REPORTS = ("world_l2.json", "world-run.json", "world.log")


def reject_constant(value):
    raise ValueError(f"non-finite JSON value: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(
        path.read_text(encoding="utf-8"),
        parse_constant=reject_constant,
        object_pairs_hook=unique_object,
    )


def integer(value, label):
    if type(value) is not int or value < 0:
        raise ValueError(f"{label} must be a nonnegative integer")
    return value


def object_value(value, label):
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def validate_world(data, years):
    data = object_value(data, "world report")
    intents = object_value(data.get("intents"), "intents")
    for field in ("threadsOk", "poolRuns", "deciderDays", "maxDeciders"):
        integer(intents.get(field), f"intents.{field}")
    if intents["threadsOk"] != 7:
        raise ValueError(f"threadsOk must equal 7, got {intents['threadsOk']}")
    timing = object_value(data.get("charMs"), "charMs")
    if integer(timing.get("years"), "charMs.years") != years * 7:
        raise ValueError("charMs.years must equal the requested horizon times 7")
    return intents


def prepare(report_dir):
    """Remove only known outputs so imported snapshots cannot satisfy this run."""
    artifact_dir = report_dir / "apsis-determinism"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    for name in NATIVE_REPORTS:
        (report_dir / name).unlink(missing_ok=True)
    for name in (*NATIVE_REPORTS, *WORLD_REPORTS, "report.md", "summary.json"):
        (artifact_dir / name).unlink(missing_ok=True)


def run_world(build_dir, report_dir, years):
    artifact_dir = report_dir / "apsis-determinism"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    for name in WORLD_REPORTS:
        (artifact_dir / name).unlink(missing_ok=True)
    command = [str(build_dir.resolve() / "world_gates"), str(years)]
    record = {"command": command, "years": years, "returncode": None, "error": None}
    try:
        with (artifact_dir / "world.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(
                command, cwd=artifact_dir, stdout=log, stderr=subprocess.STDOUT
            )
        record["returncode"] = result.returncode
        if result.returncode != 0:
            raise ValueError(f"world_gates exited with status {result.returncode}")
        validate_world(read_json(artifact_dir / "world_l2.json"), years)
    except (OSError, UnicodeError, ValueError) as error:
        record["error"] = str(error)
        print(f"world comparison failed: {error}", file=sys.stderr)
    (artifact_dir / "world-run.json").write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8"
    )
    return int(record["error"] is not None)


def count_pair(data, key, total, enforce=True):
    value = data.get(key)
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{key} must be a two-integer count pair")
    ok, count = (integer(item, key) for item in value)
    if count != total or ok > count or (enforce and ok != count):
        raise ValueError(f"{key}: got {ok}/{count}, expected {total}/{total}")
    return f"{ok}/{count}"


def create_report(report_dir, steps):
    artifact_dir = report_dir / "apsis-determinism"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    failures = []

    def row(label, result):
        rows.append(f"| {label} | {result} |")

    for name, status in steps.items():
        row(f"CI step: {name}", status)
        if status != "success":
            failures.append(f"{name}: {status}")

    def collect(name):
        source = report_dir / name
        shutil.copyfile(source, artifact_dir / name)
        return read_json(source)

    def check(label, operation):
        try:
            operation()
        except (OSError, UnicodeError, ValueError) as error:
            failures.append(f"{label}: {error}")
            row(label, "FAIL (see diagnostics below)")

    def layer1():
        data = collect("gates_l1.json")
        if not isinstance(data, list) or not data:
            raise ValueError("layer-1 report must be a nonempty array")
        for gate in data:
            gate = object_value(gate, "layer-1 gate")
            ok = integer(gate.get("ok"), "layer-1 ok")
            total = integer(gate.get("n"), "layer-1 n")
            if ok != total:
                raise ValueError(f"layer-1 gate failed: {gate.get('gate')}")
        row("Layer-1 standing gates", f"{len(data)}/{len(data)} gates passed")

    def pairs(filename, fields):
        data = object_value(collect(filename), filename)
        for label, key, total, enforce in fields:
            row(label, count_pair(data, key, total, enforce))

    check("Layer-1", layer1)
    check(
        "World checkpoint gates",
        lambda: pairs(
            "world_core_l2.json",
            (
                ("World resume (intents off/on)", "checkpointResume", 14, True),
                ("World byte-exact round trip", "roundTrip", 14, True),
                (
                    "World corruption: refused or different checkpoint",
                    "teeth",
                    20,
                    True,
                ),
            ),
        ),
    )
    check(
        "Pilot checkpoint gates",
        lambda: pairs(
            "pilot_gates_l3.json",
            (
                ("Pilot timed-input replay", "replay", 5, True),
                ("Pilot midpoint resume", "resume", 5, True),
                ("Pilot byte-exact round trip", "roundTrip", 5, True),
                ("Pilot canonical input order", "order", 5, True),
                ("Pilot teeth (informational)", "teeth", 5, False),
            ),
        ),
    )
    for filename in NATIVE_REPORTS[3:]:
        check(filename, lambda name=filename: object_value(collect(name), name))

    def world():
        record = object_value(read_json(artifact_dir / "world-run.json"), "world run")
        years = integer(record.get("years"), "world horizon")
        if years == 0:
            raise ValueError("world horizon must be positive")
        row("World horizon per seed (seven seeds)", f"{years} years")
        if type(record.get("returncode")) is not int or record["returncode"] != 0:
            raise ValueError(f"world command failed: {record.get('returncode')}")
        if record.get("error") is not None:
            raise ValueError(f"world wrapper failed: {record['error']}")
        intents = validate_world(read_json(artifact_dir / "world_l2.json"), years)
        row("World final hashes at threads 1/2/8", "7/7")
        row("World pool-use days (2/8 runs combined)", str(intents["poolRuns"]))
        row("World decision days (1-thread runs)", str(intents["deciderDays"]))
        row("World maximum deciders on one day", str(intents["maxDeciders"]))

    check("World threads", world)
    status = "FAIL" if failures else "PASS"
    lines = [
        f"# Native determinism: {status}",
        "",
        "| Check | Fresh result |",
        "| --- | --- |",
        *rows,
        "",
        "World thread settings alone do not prove parallel decisions were exercised; "
        "pool-use days are the evidence (zero is reported without adding a gate).",
        "World corruption means restore refuses OR the resulting checkpoint differs; "
        "it does not mean every corruption is authenticated and rejected.",
        "WASM parity remains deferred. Existing native runs are reused.",
    ]
    if failures:
        lines.extend(["", "## Diagnostics", "", *[f"- {item}" for item in failures]])
    markdown = "\n".join(lines) + "\n"
    (artifact_dir / "report.md").write_text(markdown, encoding="utf-8")
    (artifact_dir / "summary.json").write_text(
        json.dumps(
            {"passed": not failures, "steps": steps, "failures": failures}, indent=2
        )
        + "\n",
        encoding="utf-8",
    )
    print(markdown)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as out:
            out.write(markdown)
    return int(bool(failures))


def positive_years(value):
    years = int(value)
    if years <= 0:
        raise argparse.ArgumentTypeError("years must be positive")
    return years


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("prepare", "world", "report"):
        command = commands.add_parser(name)
        command.add_argument("--report-dir", type=Path, required=True)
        if name == "world":
            command.add_argument("--build-dir", type=Path, required=True)
            command.add_argument("--years", type=positive_years, default=300)
        if name == "report":
            for step in ("build", "prepare", "layer1", "standing", "world"):
                command.add_argument(f"--{step}-status", required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args.report_dir)
            return 0
        if args.command == "world":
            return run_world(args.build_dir, args.report_dir, args.years)
        steps = {
            step: getattr(args, f"{step}_status")
            for step in ("build", "prepare", "layer1", "standing", "world")
        }
        return create_report(args.report_dir, steps)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"determinism tooling failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
