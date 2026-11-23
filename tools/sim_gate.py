#!/usr/bin/env python3
"""Run a bounded seed/scenario matrix against the native invariant test adapter."""

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
from pathlib import Path

SCENARIOS = ("world", "hauler", "crisis", "pathfinder", "repayment", "bankruptcy")
CHECKS = (
    "resource_conservation",
    "bounded_entities",
    "day_progression",
    "outcome_reachable",
)
MAX_REPORT_BYTES = 65536


def integer(value, name, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError(f"{name} must be an integer >= {minimum}")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate report key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"nonfinite report value: {value}")


def finite_float(value):
    result = float(value)
    if not math.isfinite(result):
        reject_constant(value)
    return result


def validate_report(path, seed, scenario, days, limit):
    if path.stat().st_size > MAX_REPORT_BYTES:
        raise ValueError("driver report exceeds 64 KiB")
    report = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=unique_object,
        parse_constant=reject_constant,
        parse_float=finite_float,
    )
    if not isinstance(report, dict):
        raise ValueError("driver report must be an object")  # noqa: TRY004 - wire data
    for key, expected in (
        ("schema_version", 1),
        ("seed", seed),
        ("requested_days", days),
    ):
        if integer(report.get(key), key) != expected:
            raise ValueError(f"{key} does not match invocation")
    if report.get("scenario") != scenario:
        raise ValueError("scenario does not match invocation")
    if report.get("numeric_domain") != "integer_checkpoint":
        raise ValueError("unsupported numeric domain")
    final = integer(report.get("final_day"), "final_day")
    checked = integer(report.get("states_checked"), "states_checked", 1)
    simulated = integer(report.get("simulated_days"), "simulated_days")
    peak = integer(report.get("peak_entities"), "peak_entities")
    first_failure = report.get("first_failure_day")
    if type(first_failure) is not int or not -1 <= first_failure <= final:
        raise ValueError("invalid first_failure_day")
    if checked != final + 1 or simulated > days or final < simulated:
        raise ValueError("inconsistent simulation horizon/check count")
    if integer(report.get("end_state"), "end_state") > 3:
        raise ValueError("unknown end_state")
    integer(report.get("deliveries"), "deliveries")
    if not isinstance(report.get("state_hash"), str) or not re.fullmatch(
        r"[0-9a-f]{16}", report["state_hash"]
    ):
        raise ValueError("invalid full-checkpoint state_hash")
    checks = report.get("checks")
    if not isinstance(checks, dict) or set(checks) != set(CHECKS):
        raise ValueError("missing or unknown invariant checks")
    if any(type(value) is not bool for value in checks.values()):
        raise ValueError("invariant checks must be booleans")
    if checks["bounded_entities"] != (peak <= limit):
        raise ValueError("entity count disagrees with bounded_entities")
    if all(checks.values()) and (simulated != days or first_failure != -1):
        raise ValueError("passing report did not complete the requested horizon")
    if (
        scenario == "bankruptcy"
        and checks["outcome_reachable"]
        and report["end_state"] != 3
    ):
        raise ValueError("bankruptcy probe did not reach bankruptcy")
    return report


def bounded_int(maximum):
    def parse(value):
        try:
            parsed = int(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected an integer") from error
        if not 1 <= parsed <= maximum:
            raise argparse.ArgumentTypeError(f"expected an integer in 1..{maximum}")
        return parsed

    return parse


def seeds_arg(value):
    try:
        seeds = [int(item) for item in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected comma-separated uint32 seeds"
        ) from error
    if (
        not 1 <= len(seeds) <= 256
        or any(not 0 <= seed <= 0xFFFFFFFF for seed in seeds)
        or len(set(seeds)) != len(seeds)
    ):
        raise argparse.ArgumentTypeError("expected 1..256 distinct uint32 seeds")
    return seeds


def scenarios_arg(value):
    scenarios = value.split(",")
    if len(set(scenarios)) != len(scenarios) or any(
        item not in SCENARIOS for item in scenarios
    ):
        raise argparse.ArgumentTypeError(
            "expected distinct scenarios: " + ",".join(SCENARIOS)
        )
    return scenarios


def timeout_arg(value):
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected finite positive timeout") from error
    if not math.isfinite(parsed) or not 0 < parsed <= 3600:
        raise argparse.ArgumentTypeError("expected timeout in (0, 3600]")
    return parsed


def run(args):
    driver = args.driver.resolve()
    if not driver.is_file() or not os.access(driver, os.X_OK):
        raise ValueError(f"driver is not executable: {driver}")
    # Fresh output prevents stale passing reports from being reused.
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    with driver.open("rb") as binary:
        driver_sha256 = hashlib.file_digest(binary, "sha256").hexdigest()
    environment = {k: v for k, v in os.environ.items() if not k.startswith("APSIS_")}
    cases = []
    for seed in args.seeds:
        for scenario in args.scenarios:
            directory = output / f"{seed}-{scenario}"
            directory.mkdir()
            command = [
                str(driver),
                str(seed),
                scenario,
                str(args.days),
                str(args.max_entities),
            ]
            case = {
                "seed": seed,
                "scenario": scenario,
                "command": command,
                "status": "error",
            }
            started = time.monotonic()
            try:
                with (
                    (directory / "stdout.json").open("wb") as stdout,
                    (directory / "stderr.txt").open("wb") as stderr,
                ):
                    process = subprocess.run(
                        command,
                        cwd=directory,
                        stdout=stdout,
                        stderr=stderr,
                        env=environment,
                        timeout=args.timeout_seconds,
                        check=False,
                    )
                case["returncode"] = process.returncode
                if process.returncode not in (0, 1):
                    raise ValueError(f"driver exited with status {process.returncode}")
                report = validate_report(
                    directory / "stdout.json",
                    seed,
                    scenario,
                    args.days,
                    args.max_entities,
                )
                case["report"] = report
                passed = all(report["checks"].values())
                if process.returncode != int(not passed):
                    raise ValueError(
                        "driver exit status disagrees with invariant checks"
                    )
                case["status"] = "pass" if passed else "fail"
            except (
                OSError,
                UnicodeError,
                ValueError,
                subprocess.TimeoutExpired,
            ) as error:
                case["error"] = str(error)
            case["elapsed_seconds"] = round(time.monotonic() - started, 6)
            cases.append(case)
    counts = {
        status: sum(case["status"] == status for case in cases)
        for status in ("pass", "fail", "error")
    }
    status = "error" if counts["error"] else "fail" if counts["fail"] else "pass"
    summary = {
        "schema_version": 1,
        "status": status,
        "driver_sha256": driver_sha256,
        "days": args.days,
        "max_entities": args.max_entities,
        "timeout_seconds": args.timeout_seconds,
        "counts": counts,
        "win_state": "unavailable_in_layer4a",
        "cases": cases,
    }
    human = [
        f"Simulation matrix: {status.upper()}",
        f"{len(args.seeds)} seeds x {len(args.scenarios)} scenarios; {args.days} days per case",
        f"{counts['pass']} passed, {counts['fail']} failed, {counts['error']} errors",
        "Numeric state: integral checkpoint fields (NaN/Inf not representable).",
        "No global win state; repayment/bankruptcy are explicit fixture probes.",
    ]
    for case in cases:
        if case["status"] != "pass":
            detail = case["error"] if "error" in case else case["report"]["checks"]
            human.append(f"{case['seed']}/{case['scenario']}: {detail}")
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )
    (output / "summary.txt").write_text("\n".join(human) + "\n", encoding="utf-8")
    print("\n".join(human))
    return {"pass": 0, "fail": 1, "error": 2}[status]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--seeds", type=seeds_arg, default=seeds_arg("333860,697804"))
    parser.add_argument("--scenarios", type=scenarios_arg, default=list(SCENARIOS))
    parser.add_argument("--days", type=bounded_int(36000), default=360)
    parser.add_argument("--max-entities", type=bounded_int(10000000), default=100000)
    parser.add_argument("--timeout-seconds", type=timeout_arg, default=30.0)
    args = parser.parse_args(argv)
    try:
        return run(args)
    except (OSError, ValueError) as error:
        print(f"sim gate: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
