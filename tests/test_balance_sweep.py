"""Planning, evidence rejection, and native measurement checks for sweep tooling."""

import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools.sim import balance_sweep as sweep

ROOT = Path(__file__).resolve().parents[1]


def args(**changes):
    values = dict(
        axis=None,
        method="grid",
        samples=4,
        design_seed=5,
        seeds=[333860, 110478],
        styles=["hauler", "crisis"],
        majors=4,
        start_day=0,
        days=30,
        sample_every=10,
        max_runs=24,
        max_sim_days=100000,
    )
    return argparse.Namespace(**(values | changes))


def record(manifest, case, seed=333860, style="hauler", ended=False):
    elapsed = 15 if ended else manifest["days"]
    times = list(range(0, elapsed + 1, manifest["sampleEvery"]))
    if times[-1] != elapsed:
        times.append(elapsed)
    return {
        "schemaVersion": 1,
        "seed": seed,
        "majors": manifest["majors"],
        "startDay": manifest["startDay"],
        "days": manifest["days"],
        "sampleEvery": manifest["sampleEvery"],
        "style": sweep.STYLES.index(style),
        "resolvedParams": case["resolvedParams"],
        "rules": manifest["rules"],
        "ledger": True,
        "alive": not ended,
        "endState": 2 if ended else 0,
        "elapsedDays": elapsed,
        "outcomeDay": elapsed if ended else None,
        "firstJobDay": 5,
        "curve": [
            {
                "elapsedDays": day,
                "wallet": str(100 + day),
                "debt": "0",
                "fuel": "6",
                "worth": str(100 + day),
                "completedJobs": "0" if day == 0 else "1",
            }
            for day in times
        ],
    }


class PlanTests(unittest.TestCase):
    def test_grid_matches_seeds_and_uses_only_existing_knobs(self):
        plan = sweep.plan(
            args(axis=[("stockTank", [3, 6]), ("mediumMass", [35, 40, 45])])
        )
        self.assertEqual(plan["runCount"], 24)
        self.assertEqual(plan["uniquePoints"], 6)
        self.assertEqual(
            {c["resolvedParams"]["mediumMass"] for c in plan["cases"]},
            {"35", "40", "45"},
        )
        self.assertEqual(plan["cases"][0]["resolvedParams"]["stockTank"], "3")
        self.assertEqual(sweep.defaults()[0]["stockTank"], "6")
        for invalid in (
            "newKnob=1",
            "mediumMass=0",
            "stockDrivePm=1600",
            "stockTank=3,3",
            "stockTank=three",
        ):
            with (
                self.subTest(invalid=invalid),
                self.assertRaises(argparse.ArgumentTypeError),
            ):
                sweep.axis(invalid)

    def test_latin_strata_and_reproducibility(self):
        axes = {"mediumMass": list(range(20, 120)), "stockTank": [3, 4, 6]}
        first = sweep.design(axes, "latin", 10, 19)
        self.assertEqual(first, sweep.design(axes, "latin", 10, 19))
        self.assertNotEqual(first, sweep.design(axes, "latin", 10, 20))
        self.assertEqual(
            sorted((p["mediumMass"] - 20) // 10 for p in first), list(range(10))
        )
        counts = [sum(p["stockTank"] == tank for p in first) for tank in [3, 4, 6]]
        # Discrete projection may produce unequal category counts at boundaries.
        self.assertEqual(sum(counts), 10)
        self.assertTrue(all(2 <= count <= 4 for count in counts))

    def test_discrete_duplicates_are_not_independent_replicates(self):
        plan = sweep.plan(args(axis=[("stockTank", [6])], method="latin", samples=8))
        self.assertEqual(len(plan["designCandidates"]), 8)
        self.assertEqual(plan["uniquePoints"], 1)
        self.assertEqual(plan["runCount"], 4)

    def test_budgets_precede_execution(self):
        for changes in (
            {"max_runs": 1},
            {"max_sim_days": 10},
            {"days": 7200, "sample_every": 1},
            {"axis": [("stockTank", [3]), ("stockTank", [6])]},
            {
                "axis": [
                    ("mediumMass", list(range(101))),
                    ("mediumSpace", list(range(101))),
                ]
            },
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                sweep.plan(args(**changes))

    def test_dry_run_does_not_need_native_binary_or_modify_sources(self):
        before = sweep.sha256(ROOT / "sim/src/core/params.def")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "plan"
            with (
                mock.patch.object(
                    sweep.subprocess, "check_output", return_value="test\n"
                ),
                mock.patch.object(
                    sweep.subprocess, "run", side_effect=AssertionError("native launch")
                ),
            ):
                sweep.main(
                    [
                        "run",
                        "--output",
                        str(output),
                        "--driver",
                        "/missing/balance_seed",
                        "--dry-run",
                    ]
                )
            self.assertEqual(
                sweep.load_json(output / "manifest.json")["status"], "planned"
            )
            with self.assertRaises(SystemExit):
                sweep.main(["run", "--output", str(output), "--dry-run"])
        self.assertEqual(before, sweep.sha256(ROOT / "sim/src/core/params.def"))


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.manifest = sweep.plan(args(axis=[("stockTank", [6])], styles=["hauler"]))
        self.case = self.manifest["cases"][0]
        self.record = record(self.manifest, self.case)

    def test_untrustworthy_driver_evidence_is_rejected(self):
        changes = [
            {"seed": 1},
            {"ledger": False},
            {"elapsedDays": 1},
            {"alive": 1},
            {"outcomeDay": 3},
            {"resolvedParams": {}},
            {"firstJobDay": True},
            {"firstJobDay": 30},
            {"rules": {k: int(v) for k, v in self.manifest["rules"].items()}},
            {"curve": self.record["curve"][:-1]},
        ]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                sweep.validate(
                    self.record | change, self.manifest, self.case, 333860, "hauler"
                )
        for value in ("NaN", "1e4", "01", "-0", str(2**63), 10):
            broken = deepcopy(self.record)
            broken["curve"][0]["wallet"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                sweep.validate(broken, self.manifest, self.case, 333860, "hauler")

    def test_nonobject_and_ambiguous_json_fail_closed(self):
        for value in (None, [], 1):
            with self.subTest(value=value), self.assertRaises(ValueError):
                sweep.validate(value, self.manifest, self.case, 333860, "hauler")
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bad.json"
            for text in ('{"seed": 1, "seed": 2}', '{"wallet": NaN}'):
                path.write_text(text)
                with self.assertRaises(ValueError):
                    sweep.load_json(path)

    def evidence(self, directory):
        (directory / "runs").mkdir()
        self.manifest["status"] = "complete"
        for seed in self.manifest["seeds"]:
            item = record(self.manifest, self.case, seed, ended=seed == 110478)
            path = directory / "runs" / f"case-0000-{seed}-hauler.json"
            sweep.write_json(path, item)
            self.manifest["runs"].append(
                {
                    "case": "case-0000",
                    "seed": seed,
                    "style": "hauler",
                    "sha256": sweep.sha256(path),
                }
            )

    def test_censored_outcomes_and_curve_denominators(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.evidence(directory)
            rows = sweep.summarize(directory, self.manifest)
            self.assertEqual(rows[0]["survivalRate"], 0.5)
            self.assertEqual(rows[0]["censoredCount"], 1)
            self.assertEqual(rows[0]["medianTerminalDays"], 15)
            self.assertIsNone(rows[0]["winRate"])
            curves = sweep.load_json(directory / "summary.json")["resourceCurves"]
            self.assertEqual(
                {p["elapsedDays"]: p["observedRuns"] for p in curves},
                {0: 2, 10: 2, 15: 1, 20: 1, 30: 1},
            )
            self.assertIn("right-censored", (directory / "summary.md").read_text())
            self.assertTrue((directory / "runs.csv").is_file())

    def test_incomplete_changed_or_duplicate_artifacts_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaisesRegex(ValueError, "incomplete"):
                sweep.summarize(directory, self.manifest)
            self.evidence(directory)
            self.manifest["runs"].append(self.manifest["runs"][0])
            with self.assertRaisesRegex(ValueError, "duplicate"):
                sweep.summarize(directory, self.manifest)
            self.manifest["runs"].pop()
            last = self.manifest["runs"].pop()
            with self.assertRaisesRegex(ValueError, "missing"):
                sweep.summarize(directory, self.manifest)
            self.manifest["runs"].append(last)
            path = directory / "runs" / "case-0000-333860-hauler.json"
            path.write_text(path.read_text() + " ")
            with self.assertRaisesRegex(ValueError, "digest"):
                sweep.summarize(directory, self.manifest)

    def test_child_failure_and_timeout_mark_campaign_failed(self):
        for error in (
            subprocess.CalledProcessError(7, ["driver"]),
            subprocess.TimeoutExpired(["driver"], 1),
        ):
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "out"
                with (
                    mock.patch.object(
                        sweep.subprocess, "check_output", return_value="test\n"
                    ),
                    mock.patch.object(sweep.subprocess, "run", side_effect=error),
                    self.assertRaises(SystemExit),
                ):
                    sweep.main(
                        ["run", "--output", str(output), "--driver", sys.executable]
                    )
                manifest = sweep.load_json(output / "manifest.json")
                self.assertEqual(manifest["status"], "failed")
                self.assertEqual(manifest["runs"], [])
                self.assertFalse((output / "summary.md").exists())

    def test_all_apsis_environment_hooks_are_removed(self):
        captured = {}

        def launch(*unused, **kwargs):
            captured.update(kwargs["env"])
            raise subprocess.CalledProcessError(1, ["driver"])

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "out"
            with (
                mock.patch.object(
                    sweep.subprocess, "check_output", return_value="test\n"
                ),
                mock.patch.dict(
                    os.environ,
                    {"APSIS_SET": "stockTank=9", "APSIS_FUTURE_HOOK": "enabled"},
                ),
                mock.patch.object(sweep.subprocess, "run", side_effect=launch),
                self.assertRaises(SystemExit),
            ):
                sweep.main(["run", "--output", str(output), "--driver", sys.executable])
            self.assertFalse(any(name.startswith("APSIS_") for name in captured))
            manifest = sweep.load_json(output / "manifest.json")
            self.assertIn("APSIS_FUTURE_HOOK", manifest["strippedEnvironmentNames"])


@unittest.skipUnless(
    os.environ.get("APSIS_BALANCE_DRIVER"),
    "set APSIS_BALANCE_DRIVER for native integration",
)
class NativeTests(unittest.TestCase):
    def test_real_grid_and_offline_report_are_reproducible(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "grid"
            code = sweep.main(
                [
                    "run",
                    "--driver",
                    os.environ["APSIS_BALANCE_DRIVER"],
                    "--output",
                    str(output),
                    "--axis",
                    "stockTank=3,6",
                    "--axis",
                    "mediumMass=35,40",
                    "--seeds",
                    "333860",
                    "--styles",
                    "hauler",
                    "--days",
                    "30",
                    "--sample-every",
                    "10",
                ]
            )
            self.assertEqual(code, 0)
            manifest = sweep.load_json(output / "manifest.json")
            self.assertEqual(manifest["status"], "complete")
            self.assertEqual(len(manifest["runs"]), 4)
            before = (output / "summary.json").read_bytes()
            sweep.main(["report", str(output)])
            self.assertEqual(before, (output / "summary.json").read_bytes())
            for run in manifest["runs"]:
                repeated = subprocess.check_output(
                    run["command"],
                    env={
                        k: v
                        for k, v in os.environ.items()
                        if not k.startswith("APSIS_")
                    },
                )
                self.assertEqual(
                    json.loads(repeated),
                    sweep.load_json(
                        output
                        / "runs"
                        / f"{run['case']}-{run['seed']}-{run['style']}.json"
                    ),
                )

    def test_native_rejects_unsafe_direct_inputs(self):
        driver = os.environ["APSIS_BALANCE_DRIVER"]
        clean = {k: v for k, v in os.environ.items() if not k.startswith("APSIS_")}
        base = [driver, "333860", "4", "0", "10", "5", "0"]
        for tail in (
            ["stockTank=0"],
            ["stockTank=6", "stockTank=3"],
            ["stockDrivePm=1600"],
            ["unknown=1"],
        ):
            result = subprocess.run(base + tail, capture_output=True, env=clean)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
        result = subprocess.run(
            base, capture_output=True, env=clean | {"APSIS_SET": "stockTank=3"}
        )
        self.assertNotEqual(result.returncode, 0)
        result = subprocess.run(base, capture_output=True, env=clean)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(json.loads(result.stdout)["resolvedParams"]["stockTank"], "6")


if __name__ == "__main__":
    unittest.main()
