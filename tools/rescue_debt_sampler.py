"""Run fresh deterministic seed sweeps until a bounded rescue-debt target is met."""

import argparse
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys


STYLES = ("hauler", "crisis", "pathfinder")
TANKS = (3, 4, 6)
INDEX_LIMIT = 2**32
OVERRIDES = ("APSIS_SET", "APSIS_OFF", "APSIS_DRIVE", "APSIS_MSPACE", "APSIS_MMASS")


def integer(value: object, minimum: int, maximum: int) -> bool:
    return type(value) is int and minimum <= value <= maximum


def validate_seed(data: object, index: int, years: int) -> dict:
    """Accept only a complete nine-career sweep before counting any episodes."""
    if not isinstance(data, dict):
        raise ValueError("seed output must be an object")
    expected = {
        "schemaVersion": 1,
        "seedIndex": index,
        "start": 20 + 60 * (index % 3),
        "majors": 5 if index % 5 == 4 else 4,
        "horizonYears": years,
        "tanks": list(TANKS),
    }
    if any(data.get(key) != value for key, value in expected.items()):
        raise ValueError("seed output metadata does not match the requested sweep")
    if not integer(data.get("seed"), 0, INDEX_LIMIT - 1):
        raise ValueError("seed output must contain a uint32 seed")
    careers = data.get("careers")
    if not isinstance(careers, list) or len(careers) != 9:
        raise ValueError("seed output must contain exactly nine careers")
    groups = set()
    for career in careers:
        if not isinstance(career, dict):
            raise ValueError("career must be an object")
        style, tank = career.get("style"), career.get("tank")
        if style not in STYLES or not integer(tank, 3, 6) or tank not in TANKS:
            raise ValueError("unexpected career style/tank")
        if (style, tank) in groups:
            raise ValueError("duplicate career style/tank")
        groups.add((style, tank))
        if career.get("seed") != data["seed"] or career.get("start") != data["start"]:
            raise ValueError("career seed/start does not match seed manifest")
        cleared = career.get("debtClear")
        if not isinstance(cleared, list) or any(
            not integer(days, 0, years * 360 + 1) for days in cleared
        ):
            raise ValueError("invalid completed debt durations")
        if not integer(career.get("debtOpen"), -1, years * 360 + 1):
            raise ValueError("invalid open debt age")
    return data


def quantiles(days: list[int]) -> dict:
    """Match aggwide.py: median p50 and nearest-rank p90, 360 days/year."""
    values = sorted(day / 360 for day in days)
    return {
        "count": len(values),
        "p50Years": statistics.median(values) if values else None,
        "p90Years": values[math.ceil(0.9 * len(values)) - 1] if values else None,
    }


def aggregate(careers: list[dict]) -> list[dict]:
    """Adapt aggwide's mixed distribution, including zero-day open episodes."""
    rows = []
    for style in STYLES:
        for tank in (*TANKS, "all"):
            group = [
                career
                for career in careers
                if career["style"] == style
                and (tank == "all" or career["tank"] == tank)
            ]
            cleared = [days for career in group for days in career["debtClear"]]
            opened = [career["debtOpen"] for career in group if career["debtOpen"] >= 0]
            rows.append(
                {
                    "style": style,
                    "tank": tank,
                    "pooling": (
                        "tanks 3/4/6 pooled by episode" if tank == "all" else "none"
                    ),
                    "careers": len(group),
                    "episodes": len(cleared) + len(opened),
                    "completed": len(cleared),
                    "openAtObservationEnd": len(opened),
                    "observedDurationsAndLowerBounds": quantiles(cleared + opened),
                    "completedDurationsOnly": quantiles(cleared),
                }
            )
    return rows


def render_report(summary: dict) -> str:
    def display(value: float | None) -> str:
        return "n/a" if value is None else f"{value:.2f}"

    lines = [
        "# Rescue-debt sample",
        "",
        f"Outcome: {summary['outcome']}. Target: {summary['targetDebts']}; "
        f"actual episodes: {summary['actualDebts']}; "
        f"overshoot: {summary['overshoot']}.",
        f"Completed seeds: {summary['completedSeeds']}/{summary['maxSeeds']}; "
        f"next seed index: {summary['nextSeedIndex']}; "
        f"requested horizon: {summary['horizonYears']} years per career.",
        "",
        "Observed p50/p90 combine completed durations and open ages so far. "
        "Open ages are lower bounds at career observation end, which can precede "
        "the requested horizon at death or bankruptcy. These are not estimates "
        "of completed clearing times. Units: 360 days/year; p50: median; "
        "p90: nearest rank. No gameplay gate is applied.",
        "",
        "| Style | Tank | Careers | Episodes | Completed | Open | "
        "Observed p50 y | Observed p90 y |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in summary["groups"]:
        stats = row["observedDurationsAndLowerBounds"]
        tank = "all (3/4/6 pooled)" if row["tank"] == "all" else row["tank"]
        lines.append(
            f"| {row['style'].title()} | {tank} | {row['careers']} | "
            f"{row['episodes']} | "
            f"{row['completed']} | {row['openAtObservationEnd']} | "
            f"{display(stats['p50Years'])} | {display(stats['p90Years'])} |"
        )
    lines.extend(
        [
            "",
            "Empty distributions are n/a (JSON null). Pooled rows repeat the tank "
            "strata and are not added again to the stopping count. summary.json "
            "also provides completed-duration-only quantiles and the seed manifest.",
            "",
        ]
    )
    if summary.get("error"):
        lines.extend([f"Error: {summary['error']}", ""])
    return "\n".join(lines)


def write_atomic(path: Path, content: str) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def checkpoint(output: Path, summary: dict, careers: list[dict]) -> None:
    summary["groups"] = aggregate(careers)
    summary["actualDebts"] = sum(
        row["episodes"] for row in summary["groups"] if row["tank"] != "all"
    )
    summary["overshoot"] = max(0, summary["actualDebts"] - summary["targetDebts"])
    write_atomic(output / "summary.json", json.dumps(summary, indent=2) + "\n")
    write_atomic(output / "report.md", render_report(summary))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-driver", type=Path, required=True)
    parser.add_argument(
        "--output", type=Path, required=True, help="new output directory"
    )
    parser.add_argument("--target-debts", type=int, default=100)
    parser.add_argument("--max-seeds", type=int, default=300)
    parser.add_argument("--start-index", type=int, default=0)
    parser.add_argument("--years", type=int, default=40)
    parser.add_argument(
        "--seed-timeout", type=int, default=300, help="seconds per seed"
    )
    args = parser.parse_args()
    if args.target_debts < 1 or args.max_seeds < 1 or args.seed_timeout < 1:
        parser.error("target-debts, max-seeds and seed-timeout must be positive")
    if not 1 <= args.years <= 1000000:
        parser.error("years must be in [1, 1000000]")
    if (
        not 0 <= args.start_index < INDEX_LIMIT
        or args.start_index + args.max_seeds > INDEX_LIMIT
    ):
        parser.error("seed index range must stay within [0, 2**32)")
    overrides = [name for name in OVERRIDES if name in os.environ]
    if overrides:
        parser.error("unset simulation overrides: " + ", ".join(overrides))
    driver = args.seed_driver.resolve()
    if not driver.is_file() or not os.access(driver, os.X_OK):
        parser.error("seed-driver must be an executable file")
    output = args.output.resolve()
    try:
        output.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        parser.error(str(error))
    careers = []
    summary = {
        "schemaVersion": 1,
        "outcome": "running",
        "targetDebts": args.target_debts,
        "maxSeeds": args.max_seeds,
        "horizonYears": args.years,
        "seedTimeoutSeconds": args.seed_timeout,
        "seedDriver": str(driver),
        "seedFormula": "hashN(20261120, seedIndex), uint32 indices without wrap",
        "styles": list(STYLES),
        "tanks": list(TANKS),
        "overrides": {},
        "completedSeeds": 0,
        "nextSeedIndex": args.start_index,
        "manifest": [],
    }
    checkpoint(output, summary, careers)
    try:
        for index in range(args.start_index, args.start_index + args.max_seeds):
            result = subprocess.run(
                [str(driver), str(args.years), str(index)],
                cwd=output,
                capture_output=True,
                text=True,
                timeout=args.seed_timeout,
                check=True,
            )
            seed = validate_seed(json.loads(result.stdout), index, args.years)
            if seed["seed"] in {entry["seed"] for entry in summary["manifest"]}:
                raise ValueError("duplicate seed; refusing to recount episodes")
            filename = f"seed-{index:010d}.json"
            write_atomic(output / filename, json.dumps(seed, indent=2) + "\n")
            careers.extend(seed["careers"])
            summary["manifest"].append(
                {key: value for key, value in seed.items() if key != "careers"}
                | {"file": filename}
            )
            summary["completedSeeds"] += 1
            summary["nextSeedIndex"] = index + 1
            actual = sum(len(c["debtClear"]) + (c["debtOpen"] >= 0) for c in careers)
            if actual >= args.target_debts:
                summary["outcome"] = "target_reached"
            elif summary["completedSeeds"] == args.max_seeds:
                summary["outcome"] = "max_seeds"
            checkpoint(output, summary, careers)
            print(
                f"seed index {index}: {actual}/{args.target_debts} debt episodes",
                file=sys.stderr,
            )
            if summary["outcome"] != "running":
                break
    except (
        OSError,
        ValueError,
        subprocess.SubprocessError,
        KeyboardInterrupt,
    ) as error:
        summary["outcome"] = (
            "interrupted" if isinstance(error, KeyboardInterrupt) else "error"
        )
        summary["error"] = f"{type(error).__name__}: {error}"
        if isinstance(error, subprocess.CalledProcessError):
            summary["error"] += f"; stderr: {error.stderr.strip()}"
        checkpoint(output, summary, careers)
        print(summary["error"], file=sys.stderr)
        return 130 if summary["outcome"] == "interrupted" else 1
    print(render_report(summary), end="")
    return 0 if summary["outcome"] == "target_reached" else 2


if __name__ == "__main__":
    sys.exit(main())
