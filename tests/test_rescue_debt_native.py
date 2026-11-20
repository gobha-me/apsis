"""Optional short smokes of the real seed adapter and sampler CLI.

Set APSIS_RESCUE_DEBT_DRIVER to a built rescue_debt_seed executable to run.
Native runs must be launched under the documented heavy-workload locks.
The fixture-driven CLI tests run without a native build in ordinary CI.
"""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
DRIVER = os.environ.get("APSIS_RESCUE_DEBT_DRIVER")
OVERRIDES = ("APSIS_SET", "APSIS_OFF", "APSIS_DRIVE", "APSIS_MSPACE", "APSIS_MMASS")


@unittest.skipUnless(DRIVER, "set APSIS_RESCUE_DEBT_DRIVER for locked native smokes")
class RescueDebtNativeTests(unittest.TestCase):
    def setUp(self):
        self.driver = str(Path(DRIVER).resolve())
        self.assertTrue(Path(self.driver).is_file(), self.driver)
        self.assertTrue(os.access(self.driver, os.X_OK), self.driver)
        self.env = {
            key: value for key, value in os.environ.items() if key not in OVERRIDES
        }
        self.directory = tempfile.TemporaryDirectory(prefix="rescue-debt-native-")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name) / "sample"

    def run_sampler(self, years, target):
        return subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "tools" / "rescue_debt_sampler.py"),
                "--seed-driver",
                self.driver,
                "--output",
                str(self.output),
                "--max-seeds",
                "1",
                "--years",
                str(years),
                "--target-debts",
                str(target),
                "--seed-timeout",
                "120",
            ],
            cwd=self.directory.name,
            env=self.env,
            capture_output=True,
            text=True,
            timeout=130,
            check=False,
        )

    def read_sample(self):
        summary = json.loads((self.output / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["completedSeeds"], 1)
        self.assertEqual(summary["nextSeedIndex"], 1)
        self.assertEqual(len(summary["manifest"]), 1)
        manifest = summary["manifest"][0]
        self.assertEqual(manifest["seedIndex"], 0)
        self.assertEqual(manifest["start"], 20)
        self.assertEqual(manifest["majors"], 4)
        self.assertEqual(manifest["tanks"], [3, 4, 6])
        self.assertEqual(manifest["horizonYears"], summary["horizonYears"])
        seed = json.loads((self.output / manifest["file"]).read_text(encoding="utf-8"))
        self.assertEqual(len(seed["careers"]), 9)
        self.assertEqual(
            {(career["style"], career["tank"]) for career in seed["careers"]},
            {
                (style, tank)
                for style in ("hauler", "crisis", "pathfinder")
                for tank in (3, 4, 6)
            },
        )
        self.assertEqual(len(summary["groups"]), 12)
        self.assertEqual(
            sorted(path.name for path in self.output.glob("seed-*.json")),
            ["seed-0000000000.json"],
        )
        report = (self.output / "report.md").read_text(encoding="utf-8")
        self.assertIn("lower bounds at career observation end", report)
        return summary, seed, report

    def test_one_seed_one_year_retains_empty_groups_and_exits_max_seeds(self):
        result = self.run_sampler(years=1, target=100)
        self.assertEqual(result.returncode, 2, result.stderr)
        summary, seed, report = self.read_sample()
        self.assertEqual(summary["outcome"], "max_seeds")
        self.assertEqual(summary["horizonYears"], 1)
        self.assertEqual(summary["actualDebts"], 0)
        self.assertEqual(summary["overshoot"], 0)
        self.assertIn("Outcome: max_seeds", result.stdout)
        self.assertEqual(result.stdout, report)
        for career in seed["careers"]:
            self.assertEqual(career["debtClear"], [])
            self.assertEqual(career["debtOpen"], -1)
        for group in summary["groups"]:
            with self.subTest(style=group["style"], tank=group["tank"]):
                self.assertEqual(group["careers"], 3 if group["tank"] == "all" else 1)
                self.assertEqual(group["episodes"], 0)
                self.assertEqual(
                    group["observedDurationsAndLowerBounds"],
                    {"count": 0, "p50Years": None, "p90Years": None},
                )

    def test_one_seed_forty_years_reaches_one_episode_target(self):
        result = self.run_sampler(years=40, target=1)
        self.assertEqual(result.returncode, 0, result.stderr)
        summary, seed, report = self.read_sample()
        self.assertEqual(summary["outcome"], "target_reached")
        self.assertEqual(summary["horizonYears"], 40)
        self.assertEqual(summary["actualDebts"], 1)
        self.assertEqual(summary["overshoot"], 0)
        self.assertIn("Outcome: target_reached", result.stdout)
        self.assertEqual(result.stdout, report)
        episodes = [
            career
            for career in seed["careers"]
            if career["debtClear"] or career["debtOpen"] >= 0
        ]
        self.assertEqual(len(episodes), 1)
        career = episodes[0]
        self.assertEqual((career["style"], career["tank"]), ("pathfinder", 6))
        self.assertEqual(len(career["debtClear"]), 1)
        self.assertEqual(career["debtOpen"], -1)
        duration = career["debtClear"][0] / 360
        self.assertEqual(f"{duration:.2f}", "3.83")
        pathfinder = next(
            row
            for row in summary["groups"]
            if row["style"] == "pathfinder" and row["tank"] == 6
        )
        self.assertEqual(
            pathfinder["observedDurationsAndLowerBounds"],
            {"count": 1, "p50Years": duration, "p90Years": duration},
        )

    def test_native_cli_rejects_invalid_arguments_without_partial_json(self):
        for arguments in (
            [],
            ["1"],
            ["0", "0"],
            ["1000001", "0"],
            ["1", "-1"],
            ["1", str(2**32)],
            ["1x", "0"],
            ["1", "0", "extra"],
        ):
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [self.driver, *arguments],
                    cwd=self.directory.name,
                    env=self.env,
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn("rescue_debt_seed:", result.stderr)

    def test_native_cli_rejects_even_empty_simulation_overrides(self):
        for override in OVERRIDES:
            with self.subTest(override=override):
                result = subprocess.run(
                    [self.driver, "1", "0"],
                    cwd=self.directory.name,
                    env=self.env | {override: ""},
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn(f"unset override {override}", result.stderr)


if __name__ == "__main__":
    unittest.main()
