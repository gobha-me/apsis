#!/usr/bin/env python3
"""Run Mull over existing CTest commands, then enforce the measured baseline."""

import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "shipyard/mull-baseline.json"
KILLED = {1, 3, 4, 5, 7}  # Mull: failed, timed out, crashed, abnormal exit, fail-fast.


def run(*command, **kwargs):
    return subprocess.run(command, check=True, text=True, **kwargs)


def summarize(database):
    mutants = {}
    with sqlite3.connect(database) as connection:
        rows = connection.execute(
            "SELECT mutant_id, mutator, filename, line_number, column_number, "
            "end_line_number, end_column_number, execution_status, "
            "mutation_replacement FROM mutant"
        )
        for row in rows:
            (
                key,
                operator,
                filename,
                line,
                column,
                end_line,
                end_column,
                status,
                repl,
            ) = row
            if status not in KILLED | {2}:
                raise RuntimeError(f"Unexpected Mull execution status {status}: {key}")
            mutant = mutants.setdefault(
                key,
                {
                    "file": str(Path(filename).resolve().relative_to(ROOT)),
                    "line": line,
                    "column": column,
                    "end_line": end_line,
                    "end_column": end_column,
                    "operator": operator,
                    "replacement": repl,
                    "killed": False,
                },
            )
            mutant["killed"] |= status in KILLED
    if not mutants:
        raise RuntimeError("Mull reported no mutants; refusing an empty gate")
    survivors = []
    for mutant in mutants.values():
        if not mutant.pop("killed"):
            survivors.append(mutant)
    survivors.sort(key=identity)
    return {
        "total": len(mutants),
        "killed": len(mutants) - len(survivors),
        "survivors": survivors,
    }


def identity(mutant):
    return tuple(
        mutant[key]
        for key in ("file", "line", "column", "end_line", "end_column", "operator")
    )


def check_baseline(measured, baseline):
    allowed = {identity(mutant) for mutant in baseline["survivors"]}
    new_survivors = {identity(mutant) for mutant in measured["survivors"]} - allowed
    if (
        measured["total"] < baseline["total"]
        or measured["killed"] * baseline["total"]
        < baseline["killed"] * measured["total"]
        or len(measured["survivors"]) > len(baseline["survivors"])
        or new_survivors
    ):
        raise RuntimeError(
            "Mutation baseline regressed (score, scope, survivor count, or identity). "
            "See shipyard/tests/README.md; do not lower the gate."
        )


def prior_kills(database):
    if not database.exists():
        return []
    with sqlite3.connect(database) as connection:
        rows = connection.execute("SELECT mutant_id, execution_status FROM mutant")
        killed = set()
        for key, status in rows:
            if status not in KILLED | {2}:
                raise RuntimeError(f"Unexpected Mull execution status {status}: {key}")
            if status in KILLED:
                killed.add(key)
    return sorted(killed)


def run_test(snapshot, command):
    # Only earlier actual kills from this fresh run may bypass execution.
    # A bypass returns success; the earlier failure remains in the OR summary.
    killed = json.loads(snapshot.read_text())
    if any(os.environ.get(key) == "1" for key in killed):
        return
    os.execv(command[0], command)


def main():
    if sys.argv[1:2] == ["--run-test"]:
        run_test(Path(sys.argv[2]), sys.argv[3:])
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--record-baseline", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    os.environ["MULL_CONFIG"] = str(ROOT / "shipyard/mull.yml")
    version = run("mull-runner", "--version", capture_output=True).stdout
    if not version.startswith("mull-runner 0.37.0\n"):
        raise RuntimeError(f"Expected Mull 0.37.0, got {version}")
    tests = json.loads(
        run("ctest", "--show-only=json-v1", cwd=build, capture_output=True).stdout
    )["tests"]
    if not tests:
        raise RuntimeError("CTest discovered no tests")
    database = build / "shipyard-mull.sqlite"
    database.unlink(missing_ok=True)
    snapshot = build / "shipyard-mull-prior-kills.json"
    for test in tests:
        print(f"Mutation testing CTest entry: {test['name']}", flush=True)
        killed = prior_kills(database)
        snapshot.write_text(json.dumps(killed) + "\n")
        print(f"Reusing {len(killed)} earlier actual kills", flush=True)
        timeout = next(
            (p["value"] for p in test["properties"] if p["name"] == "TIMEOUT"),
            120,
        )
        if not 0 < timeout <= 120:
            raise RuntimeError(f"Invalid CTest timeout for {test['name']}: {timeout}")
        # Python CTest adapters execute the instrumented public-API driver.
        # Discover mutations from that ELF while retaining the full CTest command.
        command = test["command"]
        target = command[command.index("--driver") + 1] if "--driver" in command else command[0]
        run(
            "mull-runner",
            "--workers",
            "2",
            "--timeout",
            str(int(timeout * 1000)),
            "--include-not-covered",
            "--allow-surviving",
            "--reporters",
            "SQLite",
            "--report-dir",
            str(build),
            "--report-name",
            "shipyard-mull",
            "--test-program",
            sys.executable,
            target,
            "--",
            str(Path(__file__).resolve()),
            "--run-test",
            str(snapshot),
            *test["command"],
            cwd=build,
        )
    measured = summarize(database)
    total, killed = measured["total"], measured["killed"]
    print(
        f"Measured mutation score: {killed}/{total} = {100 * killed / total:.9f}%; "
        f"{len(measured['survivors'])} survivors",
        flush=True,
    )
    for survivor in measured["survivors"]:
        print(json.dumps(survivor, sort_keys=True), flush=True)
    if args.record_baseline:
        previous = json.loads(BASELINE.read_text())
        if (
            total < previous["total"]
            or killed * previous["total"] < previous["killed"] * total
            or len(measured["survivors"]) > len(previous["survivors"])
        ):
            raise RuntimeError("Refusing to record a worse mutation baseline")
        BASELINE.write_text(json.dumps(measured, indent=2) + "\n")
        run("mull-reporter", "--allow-surviving", str(database), cwd=build)
        return
    baseline = json.loads(BASELINE.read_text())
    run(
        "mull-reporter",
        "--mutation-score-threshold",
        str(100 * baseline["killed"] // baseline["total"]),
        str(database),
        cwd=build,
    )
    check_baseline(measured, baseline)
    print("Mutation baseline gate passed", flush=True)


if __name__ == "__main__":
    main()
