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
                "wallet": str((150 if ended else 100) + day),
                "debt": str((2 if ended else 0) + day),
                "fuel": "3" if ended else "6",
                "worth": str((30 if ended else 100) + day),
                "completedJobs": str((2 if ended else 1) if day else 0),
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
        strata = [(p["mediumMass"] - 20) // 10 for p in first]
        self.assertCountEqual(strata, range(10))
        self.assertNotEqual(strata, sorted(strata))
        tanks = [p["stockTank"] for p in first]
        self.assertNotEqual(tanks, sorted(tanks))
        counts = [tanks.count(tank) for tank in [3, 4, 6]]
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

    def evidence(self, directory, *, curve_interval=None, all_ended=False):
        (directory / "runs").mkdir()
        self.manifest["status"] = "complete"
        recipe = self.manifest
        if curve_interval is not None:
            recipe = recipe | {"sampleEvery": curve_interval}
        for case in self.manifest["cases"]:
            for seed in self.manifest["seeds"]:
                item = record(recipe, case, seed, ended=all_ended or seed == 110478)
                item["sampleEvery"] = self.manifest["sampleEvery"]
                path = directory / "runs" / f"{case['id']}-{seed}-hauler.json"
                sweep.write_json(path, item)
                self.manifest["runs"].append(
                    {
                        "case": case["id"],
                        "seed": seed,
                        "style": "hauler",
                        "sha256": sweep.sha256(path),
                    }
                )

    def assert_report_rejected_before_artifacts(self, directory, message):
        sweep.write_json(directory / "manifest.json", self.manifest)
        with (
            mock.patch.object(Path, "read_bytes") as read_artifact,
            mock.patch.object(sys, "stderr") as stderr,
            self.assertRaises(SystemExit) as error,
        ):
            sweep.main(["report", str(directory)])
        self.assertEqual(error.exception.code, 1)
        self.assertIn(message, stderr.write.call_args.args[0])
        read_artifact.assert_not_called()
        self.assertFalse((directory / "summary.json").exists())

    def test_censored_outcomes_and_curve_denominators(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.evidence(directory)
            rows = sweep.summarize(directory, self.manifest)
            self.assertEqual(rows[0]["survivalRate"], 0.5)
            self.assertEqual(rows[0]["censoredCount"], 1)
            self.assertEqual(rows[0]["medianTerminalDays"], 15)
            self.assertIsNone(rows[0]["winRate"])
            resources = [r[0].upper() + r[1:] for r in sweep.RESOURCES]
            self.assertEqual(
                tuple(rows[0][f"medianFinal{r}"] for r in resources),
                (147.5, 23.5, 4.5, 87.5, 1.5),
            )
            curves = sweep.load_json(directory / "summary.json")["resourceCurves"]
            self.assertEqual(
                {
                    p["elapsedDays"]: (
                        p["observedRuns"],
                        *(p[f"mean{r}"] for r in resources),
                    )
                    for p in curves
                },
                {
                    0: (2, 125, 1, 4.5, 65, 0),
                    10: (2, 135, 11, 4.5, 75, 1.5),
                    15: (1, 165, 17, 3, 45, 2),
                    20: (1, 120, 20, 6, 120, 1),
                    30: (1, 130, 30, 6, 130, 1),
                },
            )
            self.assertIn("right-censored", (directory / "summary.md").read_text())
            self.assertTrue((directory / "runs.csv").is_file())

    def test_report_uses_verified_artifact_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()
            self.evidence(directory)
            raw = directory / "runs" / "case-0000-333860-hauler.json"
            changed_path = directory / "changed.json"
            changed_record = deepcopy(self.record)
            changed_record["curve"][-1]["wallet"] = "230"
            sweep.write_json(changed_path, changed_record)

            open_path = Path.open
            raw_reads = 0

            def open_artifact(path, *args, **kwargs):
                nonlocal raw_reads
                if path == raw:
                    raw_reads += 1
                    if raw_reads > 1:
                        path = changed_path
                return open_path(path, *args, **kwargs)

            with mock.patch.object(Path, "open", open_artifact):
                rows = sweep.summarize(directory, self.manifest)

            self.assertEqual(rows[0]["medianFinalWallet"], 147.5)
            self.assertEqual(raw_reads, 1)

    def test_report_regeneration_preserves_linked_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "campaign"
            output.mkdir()
            self.evidence(output)
            manifest = output / "manifest.json"
            sweep.write_json(manifest, self.manifest)
            raw = output / "runs" / "case-0000-333860-hauler.json"
            external = root / "sentinel"
            external.write_bytes(b"external file\n")
            inputs = {path: path.read_bytes() for path in (manifest, raw, external)}

            self.assertEqual(sweep.main(["report", str(output)]), 0)
            links = {
                output / "summary.json": raw,
                output / "summary.csv": manifest,
                output / "curves.csv": external,
                output / "runs.csv": raw,
                output / "summary.md": external,
            }
            reports = {path: path.read_bytes() for path in links}
            for path, target in links.items():
                path.unlink()
                if target == manifest:
                    path.hardlink_to(target)
                else:
                    path.symlink_to(target)

            self.assertEqual(sweep.main(["report", str(output)]), 0)
            for path, original in inputs.items():
                with self.subTest(path=path.name):
                    self.assertEqual(path.read_bytes(), original)
            for path, original in reports.items():
                with self.subTest(path=path.name):
                    self.assertFalse(path.is_symlink())
                    self.assertEqual(path.read_bytes(), original)

    def test_report_rejects_case_label_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.evidence(directory)
            self.case["overrides"] = {"stockTank": 12}
            path = directory / "manifest.json"
            sweep.write_json(path, self.manifest)
            with self.assertRaises(SystemExit) as error:
                sweep.main(["report", str(directory)])
            self.assertEqual(error.exception.code, 1)
            self.assertFalse((directory / "summary.md").exists())

            # Even a matching design must agree with the verified raw settings.
            self.manifest["axes"]["stockTank"] = [12]
            self.manifest["designCandidates"] = [{"stockTank": 12}]
            sweep.write_json(path, self.manifest)
            with self.assertRaises(SystemExit) as error:
                sweep.main(["report", str(directory)])
            self.assertEqual(error.exception.code, 1)
            self.assertFalse((directory / "summary.md").exists())

    def test_report_rejects_invalid_recipes(self):
        baseline = self.manifest
        for changes, message in (
            ({"majors": 6}, "majors"),
            ({"majors": 4.0}, "majors"),
            ({"startDay": -1}, "startDay"),
            ({"days": 7201}, "days"),
            ({"sampleEvery": 7201}, "sampleEvery"),
            ({"sampleEvery": 0}, "sampleEvery"),
            ({"sampleEvery": -1}, "sampleEvery"),
            ({"days": 6001, "sampleEvery": 6}, "1001 curve samples"),
        ):
            with self.subTest(changes=changes), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                self.manifest = deepcopy(baseline) | changes
                self.manifest["simulatedDayBudget"] = self.manifest["runCount"] * (
                    self.manifest["startDay"] + self.manifest["days"] + 1
                )
                # Nonpositive intervals cannot generate coordinates, so reuse the fixture curve.
                interval = (
                    baseline["sampleEvery"]
                    if self.manifest["sampleEvery"] <= 0
                    else None
                )
                # The request exceeds the cap even if every observed curve is short.
                self.evidence(directory, curve_interval=interval, all_ended=True)
                self.assert_report_rejected_before_artifacts(directory, message)

    def test_report_rejects_invalid_settings(self):
        baseline = self.manifest
        resolved = self.case["resolvedParams"]
        default_rules = baseline["rules"]
        for label, rules, params, message in (
            ("missing rules", {}, resolved, "invalid manifest rules"),
            (
                "missing params",
                default_rules,
                {"stockTank": "6"},
                "resolved parameter names",
            ),
            (
                "nonboolean rule",
                default_rules | {"rescue": 1},
                resolved,
                "invalid manifest rules",
            ),
            (
                "noncanonical param",
                default_rules,
                resolved | {"mediumMass": "01"},
                "invalid non-axis resolved parameter value",
            ),
        ):
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                self.manifest = deepcopy(baseline)
                self.manifest["rules"] = rules
                self.manifest["cases"][0]["resolvedParams"] = params
                self.evidence(directory)
                self.assert_report_rejected_before_artifacts(directory, message)

    def test_report_rejects_disagreement_on_non_axis_settings(self):
        self.manifest = sweep.plan(args(seeds=[333860], styles=["hauler"]))
        first, second = self.manifest["cases"]
        # Historical fixed values may differ from current defaults.
        rules = self.manifest["rules"]
        rules["rescue"] = not rules["rescue"]
        medium_mass = int(first["resolvedParams"]["mediumMass"])
        for case in (first, second):
            case["resolvedParams"]["mediumMass"] = str(medium_mass + 1)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.evidence(directory)
            path = directory / "manifest.json"
            sweep.write_json(path, self.manifest)
            self.assertEqual(sweep.main(["report", str(directory)]), 0)

            second["resolvedParams"]["mediumMass"] = str(medium_mass + 2)
            artifact = directory / "runs" / "case-0001-333860-hauler.json"
            sweep.write_json(artifact, record(self.manifest, second))
            self.manifest["runs"][1]["sha256"] = sweep.sha256(artifact)
            sweep.write_json(path, self.manifest)
            with self.assertRaises(SystemExit) as error:
                sweep.main(["report", str(directory)])
            self.assertEqual(error.exception.code, 1)

    def test_manifest_run_identifiers_are_checked_before_artifact_reads(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.evidence(directory)
            for field, value in (
                ("case", "case-invalid"),
                ("seed", False),
                ("seed", 333860.0),
                ("style", "invalid"),
            ):
                broken = deepcopy(self.manifest)
                broken["runs"][0][field] = value
                if field == "case":
                    broken["cases"][0]["id"] = value
                else:
                    broken[f"{field}s"][0] = value
                with (
                    self.subTest(field=field, value=value),
                    mock.patch.object(Path, "read_bytes") as read_artifact,
                    self.assertRaises(ValueError),
                ):
                    sweep.summarize(directory, broken)
                read_artifact.assert_not_called()

            # Python considers the float equal to the planned integer seed.
            self.manifest["runs"][-1]["seed"] = 110478.0
            with mock.patch.object(Path, "read_bytes") as read_artifact:
                with self.assertRaises(ValueError):
                    sweep.summarize(directory, self.manifest)
                read_artifact.assert_not_called()

    def test_report_rejects_artifacts_resolving_outside_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / "campaign"
            directory.mkdir()
            self.evidence(directory)
            runs = directory / "runs"
            artifact = runs / "case-0000-333860-hauler.json"
            outside = artifact.rename(directory / artifact.name)
            artifact.symlink_to(outside)
            with mock.patch.object(Path, "read_bytes") as read_artifact:
                with self.assertRaises(ValueError):
                    sweep.summarize(directory, self.manifest)
                read_artifact.assert_not_called()

            artifact.unlink()
            outside.rename(artifact)
            moved_runs = runs.rename(directory.parent / "external-runs")
            runs.symlink_to(moved_runs, target_is_directory=True)
            with mock.patch.object(Path, "read_bytes") as read_artifact:
                with self.assertRaises(ValueError):
                    sweep.summarize(directory, self.manifest)
                read_artifact.assert_not_called()

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
                    mock.patch.object(
                        sweep.subprocess, "run", side_effect=error
                    ) as launch,
                    self.assertRaises(SystemExit),
                ):
                    sweep.main(
                        [
                            "run",
                            "--output",
                            str(output),
                            "--driver",
                            sys.executable,
                            "--timeout",
                            "2",
                        ]
                    )
                launch.assert_called_once()
                self.assertEqual(launch.call_args.kwargs.get("timeout"), 2)
                self.assertIs(launch.call_args.kwargs.get("check"), True)
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
                    "333860,110478",
                    "--styles",
                    "hauler,crisis",
                    "--days",
                    "30",
                    "--sample-every",
                    "10",
                ]
            )
            self.assertEqual(code, 0)
            manifest = sweep.load_json(output / "manifest.json")
            self.assertEqual(manifest["status"], "complete")
            cases = {
                "case-0000": ("35", "3"),
                "case-0001": ("35", "6"),
                "case-0002": ("40", "3"),
                "case-0003": ("40", "6"),
            }
            self.assertCountEqual(
                [(run["case"], run["seed"], run["style"]) for run in manifest["runs"]],
                [
                    (case, seed, style)
                    for case in cases
                    for seed in (333860, 110478)
                    for style in ("hauler", "crisis")
                ],
            )
            before = (output / "summary.json").read_bytes()
            sweep.main(["report", str(output)])
            self.assertEqual(before, (output / "summary.json").read_bytes())
            for run in manifest["runs"]:
                repeated = subprocess.check_output(
                    run["command"],
                    cwd=output / "runs",
                    env={
                        k: v
                        for k, v in os.environ.items()
                        if not k.startswith("APSIS_")
                    },
                )
                raw = sweep.load_json(
                    output / "runs" / f"{run['case']}-{run['seed']}-{run['style']}.json"
                )
                self.assertEqual(json.loads(repeated), raw)
                mass, tank = cases[run["case"]]
                self.assertEqual(raw["resolvedParams"]["mediumMass"], mass)
                self.assertEqual(raw["resolvedParams"]["stockTank"], tank)
                self.assertEqual(raw["curve"][0]["elapsedDays"], 0)
                self.assertEqual(raw["curve"][0]["fuel"], tank)

    def test_measured_career_applies_bot_actions(self):
        driver = os.environ["APSIS_BALANCE_DRIVER"]
        clean = {k: v for k, v in os.environ.items() if not k.startswith("APSIS_")}
        raw = json.loads(
            subprocess.check_output(
                [driver, "333860", "4", "0", "180", "30", "0", "stockTank=3"],
                env=clean,
            )
        )
        final = raw["curve"][-1]
        # The hauler refits during this career, adding to worth beyond net cash.
        self.assertGreater(
            int(final["worth"]), int(final["wallet"]) - int(final["debt"])
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
        result = subprocess.run(
            base + ["mediumSpace=60"], capture_output=True, env=clean
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"exceeds mediumSpace=60", result.stderr)
        for overrides, capacity in (([], "120"), (["mediumSpace=96"], "96")):
            with self.subTest(overrides=overrides):
                result = subprocess.run(
                    base + overrides, capture_output=True, env=clean
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                resolved = json.loads(result.stdout)["resolvedParams"]
                self.assertEqual(resolved["stockTank"], "6")
                self.assertEqual(resolved["mediumSpace"], capacity)


if __name__ == "__main__":
    unittest.main()
