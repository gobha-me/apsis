"""Small native-output fixtures; the sweep driver always runs unchanged."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
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


def aggregate_fixture(case_index=0, years=8):
    """Use unequal leg counts and censored events to expose aggregation errors."""
    careers, ranges = [], []
    samples = [(seed, start) for seed, _, starts in FIXTURE for start in starts]
    for tank in TANKS:
        for index, (seed, start) in enumerate(samples):
            ranges.append(
                {
                    "tank": tank,
                    "seed": seed,
                    "start": start,
                    "homes": 2,
                    "eff": 1,
                    "push": 2,
                    "roundTrip": index % 2,
                }
            )
            for style_index, style in enumerate(STYLES):
                careers.append(
                    {
                        "style": style,
                        "tank": tank,
                        "seed": seed,
                        "start": start,
                        "net": case_index * 1000
                        + tank * 100
                        + style_index * 10
                        + index,
                        "step": (
                            [360 * (index + 1), 720 * (index + 1), -1]
                            if index < 3
                            else [-1, -1, -1]
                        ),
                        "legs": index + 1,
                        "strand": 1 if index == 0 else 2 if index == 20 else 0,
                        "firstSurvey": index * 10 if index < 3 else -1,
                        "firstDark": [0, 359, 360, 720][index] if index < 4 else -1,
                    }
                )
    return {"careers": careers, "range": ranges, "pilotYears": len(careers) * years}


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def write_case(output, name, data):
    """Refresh a fixture's provenance so validation reaches its intended defect."""
    source = output / name / "pilot_l3.json"
    write_json(source, data)
    manifest_path = output / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["cases"][name]["pilot_l3_sha256"] = hashlib.sha256(
        source.read_bytes()
    ).hexdigest()
    write_json(manifest_path, manifest)


class LeverSweepTestCase(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="lever-sweeps-tests-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.output = self.work / "results"
        self.binary = self.work / "fixture pilot_exp"

    def cli(self, *arguments, environment=None, root=REPO_ROOT, cwd=None):
        clean = os.environ.copy()
        for hook in ENV_HOOKS:
            clean.pop(hook, None)
        clean.update(environment or {})
        return subprocess.run(
            [sys.executable, str(root / "tools/lever_sweeps.py"), *map(str, arguments)],
            capture_output=True,
            encoding="utf-8",
            cwd=cwd or self.work,
            env=clean,
            timeout=15,
            check=False,
        )

    def run_arguments(
        self,
        output=None,
        binary=None,
        mass_param="mediumMass",
        mass=45,
        drive=2000,
        space=130,
        years=8,
        tank=6,
    ):
        return [
            "--pilot-exp",
            str(binary or self.binary),
            "--output",
            str(output or self.output),
            "--mass-param",
            mass_param,
            "--mass",
            str(mass),
            "--drive",
            str(drive),
            "--space",
            str(space),
            "--years",
            str(years),
            "--tank",
            str(tank),
        ]

    def plan(self, **kwargs):
        return self.cli("run", *self.run_arguments(**kwargs), "--dry-run")

    def manifest(self, output=None):
        return json.loads(
            ((output or self.output) / "manifest.json").read_text(encoding="utf-8")
        )

    def assert_cli_error(self, result, diagnostic):
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def completed_fixture(self, output=None, years=8, tank=6):
        output = output or self.output
        result = self.plan(output=output, years=years, tank=tank)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest(output)
        manifest["status"] = "complete"
        write_json(output / "manifest.json", manifest)
        for index, name in enumerate(("baseline", "mass", "drive", "space")):
            write_case(output, name, aggregate_fixture(index, years))
        return self.manifest(output)

    def fixture_binary(self, years=8):
        """Stand in for pilot_exp, recording its real cwd, argv and environment."""
        inputs = self.work / "fixture inputs"
        inputs.mkdir()
        for index, name in enumerate(("baseline", "mass", "drive", "space")):
            write_json(inputs / f"{name}.json", aggregate_fixture(index, years))
        self.binary.write_text(
            f"#!{sys.executable}\n"
            "import json, os, pathlib, sys\n"
            f"inputs = pathlib.Path({str(inputs)!r})\n"
            "cwd = pathlib.Path.cwd()\n"
            "print(json.dumps({'cwd': str(cwd), 'argv': sys.argv[1:],\n"
            " 'hooks': {k: v for k, v in os.environ.items() if k.startswith('APSIS_')}}))\n"
            "(cwd / 'pilot_l3.json').write_bytes((inputs / (cwd.name + '.json')).read_bytes())\n",
            encoding="utf-8",
        )
        self.binary.chmod(0o755)
        return self.binary
