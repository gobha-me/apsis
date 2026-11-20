"""Run matched pilot_exp balance comparisons and render four compact tables."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
STYLES = ("hauler", "crisis", "pathfinder")
TANKS = (3, 4, 6)
ENV_HOOKS = ("APSIS_SET", "APSIS_DRIVE", "APSIS_MSPACE", "APSIS_MMASS", "APSIS_OFF")
FIXTURE = (
    (333860, 4, (25, 95, 160)),
    (110478, 4, (10, 80, 160)),
    (697804, 4, (30, 90, 180)),
    (57206, 4, (53, 64, 75)),
    (632322, 4, (10, 90, 165)),
    (517908, 4, (80, 160, 230)),
    (552723, 5, (75, 120, 200)),
)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def positive(value):
    number = int(value)
    # Keep experiment inputs and native i64 arithmetic comfortably in range.
    if not 0 < number <= 2**31 - 1:
        raise argparse.ArgumentTypeError("must be an integer from 1 to 2147483647")
    return number


def parameter_defaults():
    source = ROOT / "sim/src/core/params.def"
    entries = re.findall(
        r'^PARAM\(\s*(\w+),\s*(-?\d+),\s*"([^"]*)",',
        source.read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not entries:
        raise ValueError("cannot read parameter defaults from params.def")
    return {name: {"value": int(value), "unit": unit} for name, value, unit in entries}


def run(args):
    inherited = [name for name in ENV_HOOKS if os.environ.get(name)]
    if inherited:
        raise ValueError(
            "unset inherited overrides before running: " + ", ".join(inherited)
        )
    defaults = parameter_defaults()
    changes = {
        "baseline": {},
        "mass": {args.mass_param: args.mass},
        "drive": {"stockDrivePm": args.drive},
        "space": {"mediumSpace": args.space},
    }
    for name, overrides in changes.items():
        for parameter, value in overrides.items():
            if value == defaults[parameter]["value"]:
                raise ValueError(
                    f"{name} alternative must differ from {parameter}'s baseline"
                )
    binary = args.pilot_exp.expanduser().resolve()
    if not args.dry_run and (not binary.is_file() or not os.access(binary, os.X_OK)):
        raise ValueError(f"pilot_exp is not executable: {binary}")
    output = args.output.expanduser().resolve()
    # Refuse reuse, including a directory containing imported fixed-name artifacts.
    output.mkdir(parents=True, exist_ok=False)
    cases = {}
    for name, overrides in changes.items():
        directory = output / name
        directory.mkdir()
        parameters = {key: dict(value) for key, value in defaults.items()}
        for parameter, value in overrides.items():
            parameters[parameter]["value"] = value
        cases[name] = {
            "directory": name,
            "parameters": parameters,
            "overrides": overrides,
            "environment": {
                "APSIS_SET": ",".join(
                    f"{k}={v['value']}" for k, v in parameters.items()
                )
            },
            "command": [str(binary), str(args.years)],
        }
    manifest = {
        "version": 1,
        "status": "planned",
        "years": args.years,
        "comparison_tank": args.tank,
        "styles": list(STYLES),
        "tanks": list(TANKS),
        "career_overrides": {"stockTank": list(TANKS)},
        "fixture": [
            {"seed": seed, "majors": majors, "starts": starts}
            for seed, majors, starts in FIXTURE
        ],
        "source_revision": subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
        ).strip(),
        "params_sha256": digest(ROOT / "sim/src/core/params.def"),
        "pilot_exp_sha256": None if args.dry_run else digest(binary),
        "cases": cases,
    }
    manifest_path = output / "manifest.json"
    write_json(manifest_path, manifest)
    if args.dry_run:
        print(f"Planned four isolated runs in {manifest_path}; no simulation executed.")
        return
    manifest["status"] = "running"
    write_json(manifest_path, manifest)
    for name, case in cases.items():
        directory = output / case["directory"]
        environment = os.environ.copy()
        for hook in ENV_HOOKS:
            environment.pop(hook, None)
        environment.update(case["environment"])
        print(f"Running {name}: {case['overrides']} in {directory}", flush=True)
        with (directory / "stdout.log").open("w") as stdout:
            with (directory / "stderr.log").open("w") as stderr:
                subprocess.run(
                    case["command"],
                    cwd=directory,
                    env=environment,
                    stdout=stdout,
                    stderr=stderr,
                    check=True,
                )
        case["pilot_l3_sha256"] = digest(directory / "pilot_l3.json")
        write_json(manifest_path, manifest)
    manifest["status"] = "complete"
    write_json(manifest_path, manifest)
    report(output)


def keyed(records, fields):
    result = {}
    for record in records:
        key = tuple(record[field] for field in fields)
        if key in result:
            raise ValueError(f"duplicate sample: {key}")
        result[key] = record
    return result


def load_runs(output, manifest):
    runs = {}
    career_keys = range_keys = None
    for name, case in manifest["cases"].items():
        source = output / case["directory"] / "pilot_l3.json"
        if digest(source) != case["pilot_l3_sha256"]:
            raise ValueError(f"artifact changed since run: {source}")
        data = json.loads(source.read_text(encoding="utf-8"))
        careers = keyed(data["careers"], ("style", "tank", "seed", "start"))
        ranges = keyed(data["range"], ("tank", "seed", "start"))
        if data["pilotYears"] != len(careers) * manifest["years"]:
            raise ValueError(f"horizon does not match manifest: {name}")
        if career_keys is None:
            career_keys, range_keys = set(careers), set(ranges)
        elif set(careers) != career_keys or set(ranges) != range_keys:
            raise ValueError(f"unmatched styles/tanks/seeds/starts in {name}")
        # Every displayed group must cover the same map/start pairs as range.
        samples = {(seed, start) for seed, _, starts in FIXTURE for start in starts}
        for tank in TANKS:
            pairs = {(k[1], k[2]) for k in ranges if k[0] == tank}
            if pairs != samples:
                raise ValueError(
                    f"expected 21 matched curated map/starts: {name}/{tank}"
                )
            for style in STYLES:
                pairs_c = {(k[2], k[3]) for k in careers if k[:2] == (style, tank)}
                if pairs_c != pairs:
                    raise ValueError(f"incomplete group: {name}/{style}/{tank}")
        runs[name] = data
    return runs


def median_text(values, scale=1, places=1):
    return f"{statistics.median(values) / scale:.{places}f}" if values else "N/A"


def aggregate(data, style, tank):
    careers = [c for c in data["careers"] if c["style"] == style and c["tank"] == tank]
    ranges = [r for r in data["range"] if r["tank"] == tank]
    n = len(careers)
    refits = []
    for step, threshold in enumerate((2500, 7500, 15000)):
        values = [c["step"][step] for c in careers if c["step"][step] >= 0]
        refits.append(f"{threshold}: {median_text(values, 360)} y ({len(values)}/{n})")
    legs = sum(c["legs"] for c in careers)
    strands = sum(c["strand"] for c in careers)
    strand_rate = f"{1000 * strands / legs:.2f}" if legs else "N/A"
    survey = [c["firstSurvey"] for c in careers if c["firstSurvey"] >= 0]
    first_dark = [c["firstDark"] for c in careers if c["firstDark"] >= 0]
    dark = sum(0 <= c["firstDark"] < 360 for c in careers)
    homes = sum(r["homes"] for r in ranges)
    coverage = "/".join(
        str(sum(r[key] for r in ranges)) for key in ("eff", "push", "roundTrip")
    )
    return (
        f"{median_text([c['net'] for c in careers])} money/y (n={n})",
        "; ".join(refits),
        f"{strand_rate} ({strands}/{legs} legs)",
        f"survey {len(survey)}/{n}, median {median_text(survey)} d; "
        f"dark {len(first_dark)}/{n}, median {median_text(first_dark)} d, "
        f"Y1 {dark}/{n}",
        f"{coverage} / {homes} homes (eff/push/return; common to styles)",
    )


def report(output):
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest["version"] != 1 or manifest["status"] != "complete":
        raise ValueError(
            "report requires a completed version-1 run, not a dry-run plan"
        )
    runs = load_runs(output, manifest)
    tank = manifest["comparison_tank"]
    cases = manifest["cases"]
    reports = output / "reports"
    reports.mkdir(exist_ok=True)
    sections = [
        "# Balance-lever comparisons",
        f"Horizon: {manifest['years']} years; "
        "21 matched curated map/starts per style/tank. "
        "Exact parameter values and units: manifest.json. "
        "No preferred alternative is selected.",
        "Pay is median net worth change / requested years (including early deaths). "
        "Refits show median years to cumulative spend 2500/7500/15000 and reached n/N. "
        "Unreached events are censored; zero-leg rates are N/A. Range counts are "
        "style-independent stock-ship diagnostics, repeated for each style.",
    ]
    metrics = ("pay", "refit timing", "strandings / 1,000 legs", "opening", "range")
    for lever in ("tank", "mass", "drive", "space"):
        comparisons = [("baseline", tank)]
        columns = ["Style / metric"]
        if lever == "tank":
            comparisons += [("baseline", t) for t in TANKS if t != tank]
            columns += [
                f"{'Baseline' if i == 0 else 'Alternative'}: tank {t} jumps"
                for i, (_, t) in enumerate(comparisons)
            ]
            description = (
                "Built-in career tanks; all other parameters fixed. "
                f"Baseline tank: {tank}."
            )
        else:
            comparisons.append((lever, tank))
            parameter, value = next(iter(cases[lever]["overrides"].items()))
            baseline = cases["baseline"]["parameters"][parameter]
            columns += [
                f"Baseline: {parameter}={baseline['value']} {baseline['unit']}",
                f"Alternative: {parameter}={value} {baseline['unit']}",
            ]
            description = f"Only {parameter} varies; career tank fixed at {tank} jumps."
        rows = []
        for style in STYLES:
            values = [aggregate(runs[name], style, t) for name, t in comparisons]
            rows.extend(
                [f"{style}: {metric}"] + [v[i] for v in values]
                for i, metric in enumerate(metrics)
            )
        table_path = reports / f"{lever}.table.json"
        write_json(table_path, {"columns": columns, "rows": rows})
        rendered = subprocess.check_output(
            [sys.executable, str(ROOT / "tools/report_table.py"), str(table_path)],
            text=True,
        )
        (reports / f"{lever}.md").write_text(rendered, encoding="utf-8")
        sections.extend([f"## {lever.title()}", description, rendered.rstrip()])
    (output / "report.md").write_text("\n\n".join(sections) + "\n", encoding="utf-8")
    print(f"Wrote four comparison tables and {output / 'report.md'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    runner = commands.add_parser(
        "run", help="plan or run four isolated pilot_exp cases"
    )
    runner.add_argument("--pilot-exp", type=Path, required=True)
    runner.add_argument(
        "--output", type=Path, required=True, help="new output directory"
    )
    runner.add_argument(
        "--mass-param", choices=("mediumMass", "heavyMass"), required=True
    )
    runner.add_argument("--mass", type=positive, required=True)
    runner.add_argument(
        "--drive", type=int, choices=(500, 1000, 1500, 2000, 3000), required=True
    )
    runner.add_argument(
        "--space", type=positive, required=True, help="medium hull space budget"
    )
    runner.add_argument("--years", type=positive, default=40)
    runner.add_argument("--tank", type=int, choices=TANKS, default=6)
    runner.add_argument(
        "--dry-run", action="store_true", help="write plan without running pilot_exp"
    )
    reporter = commands.add_parser(
        "report", help="regenerate tables from completed run artifacts"
    )
    reporter.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "run":
            run(args)
        else:
            report(args.output.expanduser().resolve())
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        subprocess.SubprocessError,
    ) as error:
        parser.exit(2, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
