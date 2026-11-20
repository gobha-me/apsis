"""Run a fresh paired world_wide sample and preserve the layer-2 war gate."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time


REPO = Path(__file__).resolve().parents[1]
Z = 1.96  # Preserve sim/gates_l2.sh's existing 95% Wilson calculation.


def wilson_interval(passed: int, completed: int) -> dict:
    """Use PR #3's interval object shape, with the legacy gate's fixed z."""
    p = passed / completed
    d = 1 + Z * Z / completed
    center = (p + Z * Z / (2 * completed)) / d
    half = Z * math.sqrt(p * (1 - p) / completed + Z * Z / (4 * completed**2)) / d
    return {
        "method": "wilson",
        "confidence": 0.95,
        "z": Z,
        "lower": max(0.0, center - half),
        "upper": min(1.0, center + half),
    }


def unique_object(pairs: list) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value: str) -> None:
    raise ValueError(f"non-finite JSON value: {value}")


def read_report(path: Path, seeds: int) -> dict:
    raw = json.loads(
        path.read_text(encoding="utf-8"),
        object_pairs_hook=unique_object,
        parse_constant=reject_constant,
    )
    if not isinstance(raw, dict) or set(raw) != {"off", "on"}:
        raise ValueError("report must contain exactly the off and on arms")
    fields = (
        "n",
        "war",
        "episodes",
        "fed",
        "slow",
        "slowLowMeanPm",
        "slowLowP10Pm",
        "slowEmpty",
        "slowDaysMean",
    )
    for arm, data in raw.items():
        if not isinstance(data, dict):
            raise ValueError(f"{arm} must be an object")
        for field in fields:
            value = data.get(field)
            if type(value) is not int or value < 0:
                raise ValueError(f"{arm}.{field} must be a nonnegative integer")
        if data["n"] != seeds or data["war"] > seeds:
            raise ValueError(f"{arm}: seed denominator or war count is invalid")
        if data["fed"] + data["slow"] > data["episodes"]:
            raise ValueError(f"{arm}: fed + slow exceeds episodes")
        if data["slowEmpty"] > data["slow"]:
            raise ValueError(f"{arm}: slowEmpty exceeds slow")
        if any(data[field] > 1000 for field in ("slowLowMeanPm", "slowLowP10Pm")):
            raise ValueError(f"{arm}: capacity per-mille value exceeds 1000")
        if data["slow"] and not 31 <= data["slowDaysMean"] <= 40 * 360:
            raise ValueError(f"{arm}: slow mean duration is outside the window")
        if not data["slow"] and any(
            data[field]
            for field in ("slowEmpty", "slowLowMeanPm", "slowLowP10Pm", "slowDaysMean")
        ):
            raise ValueError(f"{arm}: slow metrics without slow observations")
    return raw


def rate(count: int, total: int) -> float | None:
    return count / total if total else None


def evaluate(raw: dict) -> dict:
    arms = {}
    for arm, data in raw.items():
        slow = data["slow"]
        arms[arm] = {
            "war": {
                "count": data["war"],
                "denominator": data["n"],
                "rate": rate(data["war"], data["n"]),
                "interval": wilson_interval(data["war"], data["n"]),
            },
            "dips": {
                "episodes": data["episodes"],
                "fed": data["fed"],
                "recovery_within_30_days_rate": rate(data["fed"], data["episodes"]),
                "slow": slow,
                "slow_share": rate(slow, data["episodes"]),
                "slowLowMeanPm": data["slowLowMeanPm"],
                "slowLowP10Pm": data["slowLowP10Pm"],
                "slow_low_mean_capacity_percent": (
                    data["slowLowMeanPm"] / 10 if slow else None
                ),
                "slow_low_p10_capacity_percent": (
                    data["slowLowP10Pm"] / 10 if slow else None
                ),
                "slowEmpty": data["slowEmpty"],
                "slow_empty_share": rate(data["slowEmpty"], slow),
                "slowDaysMean": data["slowDaysMean"] if slow else None,
                "horizon_open_at_most_30_days": data["episodes"] - data["fed"] - slow,
                "status": "descriptive",
            },
        }
    off, on = arms["off"]["war"], arms["on"]["war"]
    ratio = on["rate"] / off["rate"] if off["rate"] else None
    ratio_ok = ratio is not None and 0.75 <= ratio <= 1.25
    overlap = (
        on["interval"]["lower"] <= off["interval"]["upper"]
        and off["interval"]["lower"] <= on["interval"]["upper"]
    )
    return {
        "arms": arms,
        "war_gate": {
            "status": "pass" if ratio_ok or overlap else "fail",
            "rule": "0.75 <= on/off rate ratio <= 1.25 OR 95% Wilson intervals overlap",
            "on_off_rate_ratio": ratio,
            "ratio_undefined_reason": (
                "off war count is zero" if ratio is None else None
            ),
            "ratio_in_range": ratio_ok,
            "intervals_overlap": overlap,
        },
    }


def percent(value: float | None) -> str:
    return "n/a" if value is None else f"{100 * value:.2f}%"


def capacity(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1f}% capacity"


def summary(result: dict) -> str:
    lines = [
        f"{result['status'].upper()}: paired world_wide sample, "
        f"{result['sample']['seeds_per_arm']} seeds per arm",
    ]
    if "error" in result:
        lines.append(result["error"])
    if "war_gate" in result:
        gate = result["war_gate"]
        lines.append(
            f"War gate {gate['status'].upper()}: on/off ratio "
            f"{gate['on_off_rate_ratio']}; "
            f"intervals overlap={gate['intervals_overlap']}"
        )
        for arm, data in result["arms"].items():
            war, dips = data["war"], data["dips"]
            ci = war["interval"]
            lines.append(
                f"Intents {arm}: war {war['count']}/{war['denominator']} "
                f"({percent(war['rate'])}), 95% Wilson "
                f"[{percent(ci['lower'])}, {percent(ci['upper'])}]"
            )
            lines.append(
                f"  Dip recovery <=30 days: {dips['fed']}/{dips['episodes']} "
                f"({percent(dips['recovery_within_30_days_rate'])}) | "
                f"slow {dips['slow']}/{dips['episodes']} "
                f"({percent(dips['slow_share'])}) | lowest stock mean "
                f"{capacity(dips['slow_low_mean_capacity_percent'])}, "
                f"p10 {capacity(dips['slow_low_p10_capacity_percent'])} | "
                f"empty {dips['slowEmpty']}/{dips['slow']} "
                f"({percent(dips['slow_empty_share'])}) | "
                f"mean days below {dips['slowDaysMean'] if dips['slow'] else 'n/a'}"
            )
            lines.append(
                f"  Horizon-open dips aged <=30 days: "
                f"{dips['horizon_open_at_most_30_days']} (retained in episodes)"
            )
    lines.extend(
        [
            "Recovery/depth are descriptive dip-level figures; "
            "no pass thresholds or CIs.",
            "Slow includes late recoveries and horizon-open dips aged >30 days.",
            "Port gate UNAVAILABLE: frozen v0.20 absent; legacy script not invoked.",
        ]
    )
    return "\n".join(lines) + "\n"


def positive_int(value: str) -> int:
    number = int(value)
    if not 1 <= number <= 2**31 - 1:
        raise argparse.ArgumentTypeError("must be in [1, 2147483647]")
    return number


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--world-wide", type=Path, required=True, help="existing binary"
    )
    parser.add_argument("--output-dir", type=Path, required=True, help="must not exist")
    parser.add_argument("--seeds", type=positive_int, default=1000)
    parser.add_argument("--timeout-seconds", type=positive_int, default=3600)
    parser.add_argument(
        "--dry-run", action="store_true", help="print plan; do not run/write"
    )
    args = parser.parse_args()
    try:
        binary, output = args.world_wide.resolve(), args.output_dir.resolve()
    except (OSError, RuntimeError) as error:
        parser.error(f"cannot resolve invocation paths: {error}")
    result = {
        "schema_version": 1,
        "status": "dry_run" if args.dry_run else "error",
        "sample": {
            "seeds_per_arm": args.seeds,
            "kind": "campaign" if args.seeds == 1000 else "smoke_or_custom",
            "seed_recipe": "apsis::hashN(20261118, i), i in [0, N); paired off/on",
            "majors_recipe": "5 if i % 5 == 4, otherwise 4",
            "warmup_days": 20 * 360,
            "measurement_days": 40 * 360,
            "years": [20, 60],
            "days_per_year": 360,
            "completion_evidence": "aggregate n per arm; per-seed records unavailable",
        },
        "command": [str(binary), str(args.seeds)],
        "output_directory": str(output),
        "timeout_seconds": args.timeout_seconds,
        "port_gate": {"status": "unavailable", "reason": "frozen v0.20 absent"},
    }
    if args.dry_run:
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0
    try:
        output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(f"cannot create fresh output directory: {error}")
    start = time.monotonic()
    result["started_at"] = datetime.now(timezone.utc).isoformat()
    try:
        result["revision"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True
        ).strip()
        result["source_dirty"] = bool(
            subprocess.check_output(
                ["git", "status", "--porcelain"], cwd=REPO, text=True
            ).strip()
        )
        with binary.open("rb") as executable:
            result["binary_sha256"] = hashlib.file_digest(
                executable, "sha256"
            ).hexdigest()
        result["binary_revision_verified"] = False
        if not os.access(binary, os.X_OK):
            raise ValueError(f"binary is not executable: {binary}")
        with (
            (output / "stdout.txt").open("w") as stdout,
            (output / "stderr.txt").open("w") as stderr,
        ):
            completed = subprocess.run(
                result["command"],
                cwd=output,
                stdout=stdout,
                stderr=stderr,
                timeout=args.timeout_seconds,
                check=False,
            )
        result["returncode"] = completed.returncode
        if completed.returncode != 0:
            raise ValueError(
                f"world_wide exited {completed.returncode}; see stderr.txt"
            )
        result.update(evaluate(read_report(output / "world_wide_l2.json", args.seeds)))
        result["status"] = result["war_gate"]["status"]
        result["report_check"] = "pass"
    except (OSError, UnicodeError, ValueError, subprocess.SubprocessError) as error:
        result["error"] = f"{type(error).__name__}: {error}"
        result["report_check"] = "error"
    result["elapsed_seconds"] = time.monotonic() - start
    rendered = summary(result)
    try:
        (output / "result.json").write_text(
            json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8"
        )
        (output / "summary.txt").write_text(rendered, encoding="utf-8")
    except OSError as error:
        print(f"cannot write harness result: {error}", file=sys.stderr)
        return 2
    print(rendered, end="")
    return {"pass": 0, "fail": 1, "error": 2}[result["status"]]


if __name__ == "__main__":
    sys.exit(main())
