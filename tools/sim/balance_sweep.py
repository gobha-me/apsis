"""Bounded, matched-seed parameter experiments; never edits simulation inputs."""

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STYLES = (
    "hauler",
    "crisis",
    "pathfinder",
    "careful",
    "bold",
    "reckless",
    "payer",
    "forgetful",
    "double",
)
BOUNDS = {
    "stockTank": (1, 12),
    "mediumMass": (20, 120),
    "heavyMass": (40, 200),
    "mediumSpace": (60, 240),
    "stockDrivePm": (500, 3000),
}
DRIVES = (500, 1000, 1500, 2000, 3000)
RECIPE_BOUNDS = {
    "majors": (3, 5),
    "startDay": (0, 36000),
    "days": (1, 7200),
    "sampleEvery": (1, 7200),
}
MAX_CURVE_SAMPLES = 1001
RESOURCES = ("wallet", "debt", "fuel", "worth", "completedJobs")
ENDS = ("horizon", "lost", "hazard", "bankrupt")


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_json(path, value):
    path.write_text(
        json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8"
    )


def load_json(path):
    def reject(value):
        raise ValueError(f"nonfinite JSON constant: {value}")

    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    return json.loads(
        path.read_text(encoding="utf-8"),
        parse_constant=reject,
        object_pairs_hook=unique,
    )


def defaults():
    text = (ROOT / "sim/src/core/params.def").read_text(encoding="utf-8")
    params = dict(re.findall(r"^PARAM\(\s*(\w+),\s*(-?\d+),", text, re.M))
    rules = {
        name: value in ("true", "1")
        for name, value in re.findall(
            r"^RULE\(\s*(\w+),\s*(true|false|0|1),", text, re.M
        )
    }
    if not params or not rules:
        raise ValueError("could not read simulation defaults")
    return params, rules


def axis(text):
    name, sep, values = text.partition("=")
    if not sep or name not in BOUNDS:
        raise argparse.ArgumentTypeError(
            "axis must be supported NAME=INTEGER,INTEGER,..."
        )
    try:
        numbers = [int(value) for value in values.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("axis values must be integers") from error
    if not valid_axis_values(name, numbers):
        raise argparse.ArgumentTypeError(
            f"invalid or duplicate values for {name}; see docs/balance-tooling.md"
        )
    return name, sorted(numbers)


def valid_axis_values(name, values):
    low, high = BOUNDS[name]
    return (
        bool(values)
        and all(type(value) is int and low <= value <= high for value in values)
        and len(values) == len(set(values))
        and (name != "stockDrivePm" or all(value in DRIVES for value in values))
    )


def design(axes, method, samples, seed):
    """Discrete LHS: stratify [0,1), then map its quantiles to each finite domain."""
    names = sorted(axes)
    if method == "grid":
        rows = itertools.product(*(axes[name] for name in names))
    else:
        rng = random.Random(seed)
        columns = []
        for name in names:
            values = axes[name]
            column = [
                values[
                    min(
                        len(values) - 1, int((i + rng.random()) * len(values) / samples)
                    )
                ]
                for i in range(samples)
            ]
            rng.shuffle(column)
            columns.append(column)
        rows = zip(*columns)
    return [dict(zip(names, row, strict=True)) for row in rows]


def unique_points(candidates):
    return list({tuple(sorted(point.items())): point for point in candidates}.values())


def integer_list(text):
    try:
        values = [int(value) for value in text.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected comma-separated integer seeds"
        ) from error
    if (
        not values
        or len(set(values)) != len(values)
        or any(not 0 <= value < 2**32 for value in values)
    ):
        raise argparse.ArgumentTypeError("seeds must be unique uint32 integers")
    return values


def style_list(text):
    values = text.split(",")
    if (
        not values
        or len(set(values)) != len(values)
        or any(value not in STYLES for value in values)
    ):
        raise argparse.ArgumentTypeError(
            "styles must be unique names: " + ",".join(STYLES)
        )
    return values


def bounded(low, high):
    def parse(text):
        value = int(text)
        if not low <= value <= high:
            raise argparse.ArgumentTypeError(f"must be between {low} and {high}")
        return value

    return parse


def validate_sampling(days, sample_every):
    if math.ceil(days / sample_every) + 1 > MAX_CURVE_SAMPLES:
        raise ValueError(
            f"at most {MAX_CURVE_SAMPLES} curve samples per run; increase --sample-every"
        )


def plan(args):
    axes = dict(args.axis or [("stockTank", [3, 6])])
    if args.axis and len(axes) != len(args.axis):
        raise ValueError("an axis may only be supplied once")
    points = (
        math.prod(len(values) for values in axes.values())
        if args.method == "grid"
        else args.samples
    )
    # Bound the plan before materializing products or strata.
    if points > 10000:
        raise ValueError("design exceeds 10000 points; split the experiment")
    validate_sampling(args.days, args.sample_every)
    candidates = design(axes, args.method, args.samples, args.design_seed)
    unique = unique_points(candidates)
    total = len(unique) * len(args.seeds) * len(args.styles)
    if total > args.max_runs:
        raise ValueError(f"{total} runs exceed --max-runs {args.max_runs}")
    days = total * (args.start_day + args.days + 1)
    if days > args.max_sim_days:
        raise ValueError(
            f"{days} simulated days exceed --max-sim-days {args.max_sim_days}"
        )
    parameters, rules = defaults()
    cases = []
    for index, changes in enumerate(unique):
        resolved = parameters | {name: str(value) for name, value in changes.items()}
        cases.append(
            {
                "id": f"case-{index:04d}",
                "overrides": changes,
                "resolvedParams": resolved,
            }
        )
    return {
        "schemaVersion": 1,
        "status": "planned",
        "method": args.method,
        "axes": axes,
        "designSeed": args.design_seed,
        "designCandidates": candidates,
        "uniquePoints": len(unique),
        "runCount": total,
        "simulatedDayBudget": days,
        "seeds": args.seeds,
        "styles": args.styles,
        "majors": args.majors,
        "startDay": args.start_day,
        "days": args.days,
        "sampleEvery": args.sample_every,
        "cases": cases,
        "rules": rules,
        "runs": [],
        "interpretation": "No global win state; horizon is right-censored. Discrete LHS duplicates run once.",
    }


def require_int(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"invalid {label}")


def is_int64_string(value):
    return (
        isinstance(value, str)
        and re.fullmatch(r"0|-?[1-9][0-9]{0,18}", value) is not None
        and -(2**63) <= int(value) < 2**63
    )


def validate(record, manifest, case, seed, style):
    if not isinstance(record, dict):
        raise ValueError("driver result must be an object")
    expected = {
        "schemaVersion": 1,
        "seed": seed,
        "majors": manifest["majors"],
        "startDay": manifest["startDay"],
        "days": manifest["days"],
        "sampleEvery": manifest["sampleEvery"],
        "style": STYLES.index(style),
    }
    for name, value in expected.items():
        if type(record.get(name)) is not int or record[name] != value:
            raise ValueError(f"driver recipe mismatch: {name}")
    if (
        record.get("resolvedParams") != case["resolvedParams"]
        or record.get("rules") != manifest["rules"]
    ):
        raise ValueError("driver resolved settings mismatch")
    if any(type(value) is not bool for value in record["rules"].values()):
        raise ValueError("driver rule values must be boolean")
    if record.get("ledger") is not True:
        raise ValueError("driver reported failed ledger")
    require_int(record.get("endState"), 0, 3, "end state")
    ended = record["endState"] != 0
    if type(record.get("alive")) is not bool or record["alive"] == ended:
        raise ValueError("inconsistent alive/end state")
    elapsed = record.get("elapsedDays")
    require_int(elapsed, 1, manifest["days"], "elapsed days")
    if not ended and elapsed != manifest["days"]:
        raise ValueError("surviving run did not reach horizon")
    if record.get("outcomeDay") != (elapsed if ended else None) or (
        ended and type(record["outcomeDay"]) is not int
    ):
        raise ValueError("inconsistent outcome day")
    first = record.get("firstJobDay")
    if first is not None:
        require_int(first, 1, elapsed, "first job completion day")
    curve = record.get("curve")
    if not isinstance(curve, list) or not 2 <= len(curve) <= MAX_CURVE_SAMPLES:
        raise ValueError("invalid resource curve")
    expected_days = list(range(0, elapsed + 1, manifest["sampleEvery"]))
    if expected_days[-1] != elapsed:
        expected_days.append(elapsed)
    if len(curve) != len(expected_days):
        raise ValueError("missing or extra resource samples")
    previous_completed_jobs = 0
    for point, day in zip(curve, expected_days, strict=True):
        if (
            not isinstance(point, dict)
            or type(point.get("elapsedDays")) is not int
            or point["elapsedDays"] != day
        ):
            raise ValueError("resource sample day mismatch")
        for resource in RESOURCES:
            value = point.get(resource)
            if not is_int64_string(value):
                raise ValueError(f"invalid exact resource: {resource}")
            if resource != "worth" and int(value) < 0:
                raise ValueError(f"negative {resource}")
        if int(point["completedJobs"]) < previous_completed_jobs:
            raise ValueError("job completion counter decreased")
        previous_completed_jobs = int(point["completedJobs"])
    if int(curve[0]["completedJobs"]) != 0:
        raise ValueError("job completion counter must start at zero")
    if (first is None) != (int(curve[-1]["completedJobs"]) == 0):
        raise ValueError("job completion observation mismatch")
    if first is not None:
        last_zero = max(
            point["elapsedDays"] for point in curve if int(point["completedJobs"]) == 0
        )
        first_positive = min(
            point["elapsedDays"] for point in curve if int(point["completedJobs"]) > 0
        )
        if not last_zero < first <= first_positive:
            raise ValueError("first job completion timing contradicts sampled counters")
    return record


def csv_file(path, fields, rows):
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def validate_design(manifest):
    require_int(manifest["schemaVersion"], 1, 1, "manifest schema version")
    method, axes = manifest["method"], manifest["axes"]
    if method not in ("grid", "latin") or not isinstance(axes, dict) or not axes:
        raise ValueError("invalid manifest design")
    for name, values in axes.items():
        if (
            name not in BOUNDS
            or not isinstance(values, list)
            or not valid_axis_values(name, values)
        ):
            raise ValueError("invalid manifest axis")
    candidates = manifest["designCandidates"]
    if not isinstance(candidates, list) or not 1 <= len(candidates) <= 10000:
        raise ValueError("invalid manifest design candidates")
    seed = manifest["designSeed"]
    require_int(seed, 0, 2**32 - 1, "manifest design seed")
    if method == "grid" and math.prod(map(len, axes.values())) != len(candidates):
        raise ValueError("manifest design candidate count mismatch")
    expected = design(axes, method, len(candidates), seed)
    if candidates != expected or any(
        type(value) is not int for point in candidates for value in point.values()
    ):
        raise ValueError("manifest design candidates mismatch")

    points = unique_points(expected)
    require_int(
        manifest["uniquePoints"], len(points), len(points), "unique point count"
    )
    cases = manifest["cases"]
    if not isinstance(cases, list) or len(cases) != len(points):
        raise ValueError("manifest case count mismatch")
    # The schema requires every setting name. Recorded fixed values may differ from defaults.
    params, default_rules = defaults()
    rules = manifest["rules"]
    if (
        not isinstance(rules, dict)
        or rules.keys() != default_rules.keys()
        or any(type(value) is not bool for value in rules.values())
    ):
        raise ValueError("invalid manifest rules")
    cases_by_id = {}
    baseline_params = None
    for index, (case, point) in enumerate(zip(cases, points, strict=True)):
        case_id = f"case-{index:04d}"
        if not isinstance(case, dict) or case.get("id") != case_id:
            raise ValueError("invalid manifest case ID")
        overrides = case["overrides"]
        if overrides != point or any(
            type(value) is not int for value in overrides.values()
        ):
            raise ValueError("manifest case overrides do not match design")
        resolved = case["resolvedParams"]
        if not isinstance(resolved, dict) or resolved.keys() != params.keys():
            raise ValueError("invalid manifest resolved parameter names")
        if any(resolved[name] != str(value) for name, value in point.items()):
            raise ValueError("manifest case overrides do not match resolved settings")
        fixed_params = {
            name: value for name, value in resolved.items() if name not in axes
        }
        if any(not is_int64_string(value) for value in fixed_params.values()):
            raise ValueError("invalid non-axis resolved parameter value")
        if baseline_params is None:
            baseline_params = fixed_params
        elif fixed_params != baseline_params:
            raise ValueError("manifest cases disagree on non-axis resolved parameters")
        cases_by_id[case_id] = case
    return cases_by_id


def summarize(output, manifest):
    if manifest["status"] != "complete":
        raise ValueError("campaign is incomplete; partial evidence is not summarized")
    for name, (low, high) in RECIPE_BOUNDS.items():
        require_int(manifest[name], low, high, f"manifest {name}")
    validate_sampling(manifest["days"], manifest["sampleEvery"])
    cases = validate_design(manifest)
    seeds, styles = manifest["seeds"], manifest["styles"]
    if not isinstance(seeds, list) or not seeds:
        raise ValueError("invalid manifest seeds")
    for seed in seeds:
        require_int(seed, 0, 2**32 - 1, "manifest seed")
    if len(seeds) != len(set(seeds)):
        raise ValueError("duplicate manifest seeds")
    if (
        not isinstance(styles, list)
        or not styles
        or any(style not in STYLES for style in styles)
        or len(styles) != len(set(styles))
    ):
        raise ValueError("invalid manifest styles")
    seed_set, style_set = set(seeds), set(styles)
    run_count = len(cases) * len(seeds) * len(styles)
    require_int(manifest["runCount"], run_count, run_count, "manifest run count")
    runs = manifest["runs"]
    if not isinstance(runs, list):
        raise ValueError("invalid manifest runs")
    seen = set()
    for run in runs:
        if not isinstance(run, dict):
            raise ValueError("invalid manifest run")
        case_id, seed, style = run["case"], run["seed"], run["style"]
        key = (case_id, seed, style)
        if (
            not isinstance(case_id, str)
            or case_id not in cases
            or type(seed) is not int
            or seed not in seed_set
            or not isinstance(style, str)
            or style not in style_set
            or key in seen
        ):
            raise ValueError("unexpected or duplicate run evidence")
        seen.add(key)
    if len(seen) != run_count:
        raise ValueError("campaign has missing run evidence")

    runs_dir = output.resolve() / "runs"
    if runs_dir.is_symlink():
        raise ValueError("runs directory must not be a symlink")
    records = []
    for run in runs:
        # Artifact names are derived from the recipe, never taken from stored paths.
        name = f"{run['case']}-{run['seed']}-{run['style']}.json"
        path = (runs_dir / name).resolve()
        if not path.is_relative_to(runs_dir):
            raise ValueError("run artifact path escapes runs directory")
        if sha256(path) != run["sha256"]:
            raise ValueError("run artifact digest mismatch")
        case = cases[run["case"]]
        record = validate(load_json(path), manifest, case, run["seed"], run["style"])
        records.append((run, record))
    summaries, curves, rows = [], [], []
    for case in manifest["cases"]:
        for style in manifest["styles"]:
            group = [
                record
                for run, record in records
                if run["case"] == case["id"] and run["style"] == style
            ]
            outcomes = [
                record["outcomeDay"]
                for record in group
                if record["outcomeDay"] is not None
            ]
            final = [record["curve"][-1] for record in group]
            row = {
                "case": case["id"],
                "style": style,
                "runs": len(group),
                "survivalRate": sum(record["alive"] for record in group) / len(group),
                "terminalCount": len(outcomes),
                "censoredCount": len(group) - len(outcomes),
                "medianTerminalDays": statistics.median(outcomes) if outcomes else None,
                "winRate": None,
            }
            for index, name in enumerate(ENDS[1:], 1):
                row[name] = sum(record["endState"] == index for record in group)
            for resource in RESOURCES:
                row[f"medianFinal{resource[0].upper() + resource[1:]}"] = (
                    statistics.median(int(point[resource]) for point in final)
                )
            summaries.append(row)
            by_day = {}
            for record in group:
                for point in record["curve"]:
                    by_day.setdefault(point["elapsedDays"], []).append(point)
            for day, points in sorted(by_day.items()):
                curves.append(
                    {
                        "case": case["id"],
                        "style": style,
                        "elapsedDays": day,
                        "observedRuns": len(points),
                        **{
                            f"mean{r[0].upper() + r[1:]}": statistics.mean(
                                int(p[r]) for p in points
                            )
                            for r in RESOURCES
                        },
                    }
                )
    for run, record in records:
        rows.append(
            {
                "case": run["case"],
                "seed": run["seed"],
                "style": run["style"],
                "elapsedDays": record["elapsedDays"],
                "outcome": ENDS[record["endState"]],
                "outcomeDay": record["outcomeDay"],
                "firstJobDay": record["firstJobDay"],
                **{r: record["curve"][-1][r] for r in RESOURCES},
            }
        )
    lines = [
        "# Balance experiment observations",
        "",
        "No global win state exists: win rate is unavailable. Horizon observations are right-censored.",
        "Terminal-day medians include ended runs only; final resources use each run's last observation.",
        "Curve means include only runs observed on that exact day (see observedRuns); no carry-forward.",
        "Matched seeds are paired measurements, not evidence of population-wide balance. No rules or shipped values were edited.",
        "",
        "| Case | Overrides |",
        "| --- | --- |",
    ]
    lines.extend(
        f"| {c['id']} | {json.dumps(c['overrides'], sort_keys=True)} |"
        for c in manifest["cases"]
    )
    lines += [
        "",
        "| Case | Style | N | Survived | Lost / hazard / bankrupt | Ended / censored | Median terminal days | Median completed jobs | Median wallet | Median debt |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in summaries:
        duration = (
            "n/a"
            if row["medianTerminalDays"] is None
            else str(row["medianTerminalDays"])
        )
        lines.append(
            f"| {row['case']} | {row['style']} | {row['runs']} | {row['survivalRate']:.1%} | {row['lost']} / {row['hazard']} / {row['bankrupt']} | {row['terminalCount']} / {row['censoredCount']} | {duration} | {row['medianFinalCompletedJobs']} | {row['medianFinalWallet']} | {row['medianFinalDebt']} |"
        )
    # Replace existing report entries without writing through their links.
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        staged = Path(temporary)
        write_json(
            staged / "summary.json",
            {"schemaVersion": 1, "summaries": summaries, "resourceCurves": curves},
        )
        csv_file(staged / "summary.csv", list(summaries[0]), summaries)
        csv_file(staged / "curves.csv", list(curves[0]), curves)
        csv_file(staged / "runs.csv", list(rows[0]), rows)
        (staged / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
        for report in staged.iterdir():
            report.replace(output / report.name)
    return summaries


def run(args):
    manifest = plan(args)
    driver = args.driver.resolve()
    if not args.dry_run and (not driver.is_file() or not os.access(driver, os.X_OK)):
        raise ValueError("--driver must name an executable balance_seed")
    try:
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
        ).strip()
        dirty = bool(
            subprocess.check_output(
                ["git", "status", "--porcelain"], cwd=ROOT, text=True
            )
        )
    except (OSError, subprocess.CalledProcessError):
        revision, dirty = None, None
    sources = sorted((ROOT / "sim/src").rglob("*"))
    manifest["provenance"] = {
        "revision": revision,
        "dirty": dirty,
        "driver": str(driver),
        "driverSha256": sha256(driver) if not args.dry_run else None,
        "simSources": {
            str(p.relative_to(ROOT)): sha256(p) for p in sources if p.is_file()
        },
        "adapterSha256": sha256(Path(__file__).with_name("balance_seed.cpp")),
        "controllerSha256": sha256(Path(__file__)),
    }
    # Remove every APSIS_* variable from child processes, including future hooks.
    manifest["strippedEnvironmentNames"] = sorted(
        name for name in os.environ if name.startswith("APSIS_")
    )
    env = {
        name: value
        for name, value in os.environ.items()
        if not name.startswith("APSIS_")
    }
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / "manifest.json", manifest)
    if args.dry_run:
        print(f"planned {manifest['runCount']} runs in {output}")
        return
    (output / "runs").mkdir()
    manifest["status"] = "running"
    write_json(output / "manifest.json", manifest)
    try:
        for case in manifest["cases"]:
            for seed in manifest["seeds"]:
                for style in manifest["styles"]:
                    name = f"{case['id']}-{seed}-{style}"
                    command = [
                        str(driver),
                        str(seed),
                        str(args.majors),
                        str(args.start_day),
                        str(args.days),
                        str(args.sample_every),
                        str(STYLES.index(style)),
                        *(
                            f"{key}={value}"
                            for key, value in sorted(case["overrides"].items())
                        ),
                    ]
                    # A fresh cwd prevents the simulator's fixed-name artifacts from
                    # touching source files even if a wrong driver is selected.
                    with (output / "runs" / f"{name}.stderr.log").open(
                        "w", encoding="utf-8"
                    ) as log:
                        result = subprocess.run(
                            command,
                            cwd=output / "runs",
                            env=env,
                            stdout=subprocess.PIPE,
                            stderr=log,
                            timeout=args.timeout,
                            check=True,
                            text=True,
                        )
                    path = output / "runs" / f"{name}.json"
                    path.write_text(result.stdout, encoding="utf-8")
                    validate(load_json(path), manifest, case, seed, style)
                    manifest["runs"].append(
                        {
                            "case": case["id"],
                            "seed": seed,
                            "style": style,
                            "command": command,
                            "sha256": sha256(path),
                        }
                    )
                    write_json(output / "manifest.json", manifest)
        manifest["status"] = "complete"
        write_json(output / "manifest.json", manifest)
        summarize(output, manifest)
    except (
        ValueError,
        KeyError,
        TypeError,
        OSError,
        subprocess.SubprocessError,
    ) as error:
        manifest["status"] = "failed"
        manifest["error"] = str(error)
        write_json(output / "manifest.json", manifest)
        raise
    print(f"completed {manifest['runCount']} runs; {output / 'summary.md'}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    execute = commands.add_parser("run")
    execute.add_argument(
        "--driver", type=Path, default=ROOT / "tools/sim/build/balance_seed"
    )
    execute.add_argument("--output", type=Path, required=True)
    execute.add_argument("--axis", type=axis, action="append")
    execute.add_argument("--method", choices=("grid", "latin"), default="grid")
    execute.add_argument("--samples", type=bounded(1, 10000), default=4)
    execute.add_argument("--design-seed", type=bounded(0, 2**32 - 1), default=0)
    execute.add_argument("--seeds", type=integer_list, default=[333860, 110478])
    execute.add_argument(
        "--styles", type=style_list, default=["hauler", "crisis", "pathfinder"]
    )
    execute.add_argument("--majors", type=bounded(*RECIPE_BOUNDS["majors"]), default=4)
    execute.add_argument(
        "--start-day", type=bounded(*RECIPE_BOUNDS["startDay"]), default=0
    )
    execute.add_argument("--days", type=bounded(*RECIPE_BOUNDS["days"]), default=180)
    execute.add_argument(
        "--sample-every", type=bounded(*RECIPE_BOUNDS["sampleEvery"]), default=30
    )
    execute.add_argument("--max-runs", type=bounded(1, 100000), default=24)
    execute.add_argument("--max-sim-days", type=bounded(1, 10**9), default=100000)
    execute.add_argument("--timeout", type=bounded(1, 3600), default=60)
    execute.add_argument("--dry-run", action="store_true")
    report = commands.add_parser("report")
    report.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "run":
            run(args)
        else:
            summarize(args.output, load_json(args.output / "manifest.json"))
    except (
        ValueError,
        KeyError,
        TypeError,
        OSError,
        subprocess.SubprocessError,
    ) as error:
        parser.exit(1, f"balance_sweep: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
