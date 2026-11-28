"""Run the real sampler CLI against small executable adapter fixtures."""

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
SAMPLER = REPO_ROOT / "tools" / "rescue_debt_sampler.py"
OVERRIDES = ("APSIS_SET", "APSIS_OFF", "APSIS_DRIVE", "APSIS_MSPACE", "APSIS_MMASS")


def seed_record(index, years=40, *, seed=None):
    """A complete fixture sweep, following the documented metadata schedule."""
    if seed is None:
        seed = 1000 + index
    start = 20 + 60 * (index % 3)
    return {
        "schemaVersion": 1,
        "seedIndex": index,
        "seed": seed,
        "start": start,
        "majors": 5 if index % 5 == 4 else 4,
        "horizonYears": years,
        "tanks": [3, 4, 6],
        "careers": [
            {
                "style": style,
                "tank": tank,
                "seed": seed,
                "start": start,
                "debtClear": [],
                "debtOpen": -1,
                "rescue": [],
                "ledger": True,
                "endState": 0,
                "endDay": (start + years) * 360,
            }
            for style in ("hauler", "crisis", "pathfinder")
            for tank in (3, 4, 6)
        ],
    }


class RescueDebtCliTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="rescue-debt-cli-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.driver = self.root / "fixture driver.py"
        self.output = self.root / "sample output"
        self.calls_file = self.root / "calls.jsonl"

    def make_driver(self, cases):
        (self.root / "cases.json").write_text(json.dumps(cases), encoding="utf-8")
        self.calls_file.unlink(missing_ok=True)
        self.driver.write_text(
            f"#!{sys.executable}\n"
            + textwrap.dedent(
                """\
                import json
                import os
                from pathlib import Path
                import signal
                import sys
                import time

                root = Path(__file__).resolve().parent
                years, index = map(int, sys.argv[1:])
                with (root / "calls.jsonl").open("a", encoding="utf-8") as calls:
                    calls.write(json.dumps([years, index]) + "\\n")
                case = json.loads((root / "cases.json").read_text())[str(index)]
                mode = case.get("mode", "record")
                if mode == "fail":
                    print("fixture adapter failed", file=sys.stderr)
                    sys.exit(7)
                if mode == "timeout":
                    print('{"schemaVersion":1}', flush=True)
                    time.sleep(60)
                if mode == "interrupt":
                    os.kill(os.getppid(), signal.SIGINT)
                    time.sleep(0.1)
                    sys.exit(7)
                if mode == "sigterm":
                    os.kill(os.getppid(), signal.SIGTERM)
                    time.sleep(0.1)
                    sys.exit(7)
                if mode == "raw":
                    print(case["text"])
                else:
                    print(json.dumps(case["record"]))
                """
            ),
            encoding="utf-8",
        )
        self.driver.chmod(0o755)

    def invoke(self, *arguments, output=None, driver=None, extra_env=None, cwd=None):
        env = {key: value for key, value in os.environ.items() if key not in OVERRIDES}
        env.update(extra_env or {})
        return subprocess.run(
            [
                sys.executable,
                str(SAMPLER),
                "--seed-driver",
                str(driver or self.driver),
                "--output",
                str(output or self.output),
                "--target-debts",
                "3",
                "--max-seeds",
                "3",
                "--years",
                "40",
                "--seed-timeout",
                "3",
                *arguments,
            ],
            capture_output=True,
            text=True,
            cwd=cwd or REPO_ROOT,
            env=env,
            timeout=10,
            check=False,
        )

    def read_summary(self, output=None):
        return json.loads((output or self.output).joinpath("summary.json").read_text())

    def calls(self):
        if not self.calls_file.exists():
            return []
        return [json.loads(line) for line in self.calls_file.read_text().splitlines()]

    def assert_usage_error(self, result, diagnostic, output=None):
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage:", result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse((output or self.output).exists())
        self.assertEqual(self.calls(), [])

    def test_target_is_checked_after_complete_seeds_and_reports_overshoot(self):
        first, second = seed_record(3), seed_record(4)
        first["careers"][0].update(debtClear=[360], debtOpen=0)
        second["careers"][-1]["debtClear"] = [720, 1080, 1440]
        self.make_driver({3: {"record": first}, 4: {"record": second}})

        result = self.invoke("--start-index", "3")

        self.assertEqual(result.returncode, 0, result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "target_reached")
        self.assertEqual(summary["actualDebts"], 5)
        self.assertEqual(summary["overshoot"], 2)
        self.assertEqual(summary["completedSeeds"], 2)
        self.assertEqual(summary["nextSeedIndex"], 5)
        self.assertEqual(self.calls(), [[40, 3], [40, 4]])
        self.assertEqual(
            [
                (entry["seedIndex"], entry["start"], entry["majors"])
                for entry in summary["manifest"]
            ],
            [(3, 20, 4), (4, 80, 5)],
        )
        for entry, expected in zip(summary["manifest"], (first, second), strict=True):
            self.assertEqual(entry["tanks"], [3, 4, 6])
            self.assertEqual(
                json.loads((self.output / entry["file"]).read_text()), expected
            )
            self.assertEqual(len(expected["careers"]), 9)
        self.assertEqual(len(list(self.output.glob("seed-*.json"))), 2)
        self.assertEqual(result.stdout, (self.output / "report.md").read_text())
        self.assertIn("actual episodes: 5; overshoot: 2", result.stdout)

    def test_rescue_prices_do_not_reach_target_and_max_seeds_is_explicit(self):
        first, second = seed_record(0), seed_record(1)
        first["careers"][0].update(debtClear=[360], debtOpen=0, rescue=[9] * 50)
        for career in second["careers"]:
            career["rescue"] = [0, 1, 1000000] * 20
        self.make_driver({0: {"record": first}, 1: {"record": second}})

        result = self.invoke("--max-seeds", "2")

        self.assertEqual(result.returncode, 3, result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "max_seeds")
        self.assertEqual(summary["actualDebts"], 2)
        self.assertEqual(summary["targetDebts"], 3)
        self.assertEqual(summary["overshoot"], 0)
        self.assertEqual(summary["completedSeeds"], 2)
        self.assertEqual(summary["nextSeedIndex"], 2)
        self.assertEqual(self.calls(), [[40, 0], [40, 1]])
        self.assertIn(
            "Outcome: max_seeds. Target: 3; actual episodes: 2", result.stdout
        )

    def test_zero_age_open_episode_reaches_target_without_any_rescue_prices(self):
        record = seed_record(0)
        record["careers"][-1]["debtOpen"] = 0
        self.make_driver({0: {"record": record}})

        result = self.invoke("--target-debts", "1")

        self.assertEqual(result.returncode, 0, result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["actualDebts"], 1)
        self.assertEqual(summary["completedSeeds"], 1)
        self.assertEqual(self.calls(), [[40, 0]])
        self.assertIn("lower bounds at career observation end", result.stdout)
        self.assertIn("| Pathfinder | 6 | 1 | 1 | 0 | 1 | 0.00 | 0.00 |", result.stdout)

    def test_target_at_seed_cap_on_last_uint32_index(self):
        last = 2**32 - 1
        first = seed_record(last - 1, years=2, seed=last - 1)
        second = seed_record(last, years=2, seed=last)
        first["careers"][0]["debtClear"] = [360]
        second["careers"][0]["debtOpen"] = 0
        second["careers"][1]["debtOpen"] = 720
        self.make_driver({last - 1: {"record": first}, last: {"record": second}})

        result = self.invoke(
            "--start-index",
            str(last - 1),
            "--max-seeds",
            "2",
            "--years",
            "2",
            cwd=self.root,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [[2, last - 1], [2, last]])
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "target_reached")
        self.assertEqual(summary["targetDebts"], 3)
        self.assertEqual(summary["actualDebts"], 3)
        self.assertEqual(summary["overshoot"], 0)
        self.assertEqual(summary["completedSeeds"], 2)
        self.assertEqual(summary["nextSeedIndex"], 2**32)
        self.assertEqual(summary["horizonYears"], 2)
        self.assertEqual(
            [
                (entry["seedIndex"], entry["seed"], entry["start"], entry["tanks"])
                for entry in summary["manifest"]
            ],
            [(last - 1, last - 1, 140, [3, 4, 6]), (last, last, 20, [3, 4, 6])],
        )
        pooled = next(
            row
            for row in summary["groups"]
            if (row["style"], row["tank"]) == ("hauler", "all")
        )
        self.assertEqual(pooled["openAtObservationEnd"], 2)
        self.assertEqual(
            pooled["observedDurationsAndLowerBounds"],
            {"count": 3, "p50Years": 1, "p90Years": 2},
        )
        self.assertEqual(
            pooled["completedDurationsOnly"],
            {"count": 1, "p50Years": 1, "p90Years": 1},
        )

    def test_failures_preserve_only_the_completed_seed_prefix(self):
        first = seed_record(0)
        first["careers"][0]["debtClear"] = [360]
        incomplete = seed_record(1)
        incomplete["careers"] = incomplete["careers"][:1]
        incomplete["careers"][0]["debtClear"] = [360] * 10
        duplicate = seed_record(1)
        duplicate["seed"] = first["seed"]
        for career in duplicate["careers"]:
            career["seed"] = first["seed"]
            career["debtClear"] = [360]
        cases = (
            ({"mode": "fail"}, "CalledProcessError", "fixture adapter failed"),
            ({"mode": "raw", "text": "{not JSON"}, "JSONDecodeError", "Expecting"),
            ({"record": incomplete}, "ValueError", "exactly nine careers"),
            ({"record": duplicate}, "ValueError", "duplicate seed"),
        )
        for number, (case, error_type, diagnostic) in enumerate(cases):
            with self.subTest(case=case):
                output = self.root / f"failed-{number}"
                self.make_driver({0: {"record": first}, 1: case})
                result = self.invoke(output=output)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                summary = self.read_summary(output)
                self.assertEqual(summary["outcome"], "error")
                self.assertEqual(summary["actualDebts"], 1)
                self.assertEqual(summary["completedSeeds"], 1)
                self.assertEqual(summary["nextSeedIndex"], 1)
                self.assertEqual(len(summary["manifest"]), 1)
                self.assertIn(error_type, summary["error"])
                self.assertIn(diagnostic, summary["error"])
                self.assertIn(diagnostic, result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual(self.calls(), [[40, 0], [40, 1]])
                self.assertEqual(
                    [path.name for path in output.glob("seed-*.json")],
                    ["seed-0000000000.json"],
                )
                self.assertEqual(
                    json.loads((output / "seed-0000000000.json").read_text()), first
                )
                self.assertIn("Outcome: error", (output / "report.md").read_text())

    def test_timeout_does_not_count_or_archive_partial_seed_output(self):
        first = seed_record(0)
        first["careers"][0]["debtClear"] = [360]
        self.make_driver({0: {"record": first}, 1: {"mode": "timeout"}})

        result = self.invoke("--seed-timeout", "1")

        self.assertEqual(result.returncode, 1, result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "error")
        self.assertEqual(summary["actualDebts"], 1)
        self.assertEqual(summary["completedSeeds"], 1)
        self.assertEqual(summary["nextSeedIndex"], 1)
        self.assertIn("TimeoutExpired", summary["error"])
        self.assertIn("TimeoutExpired", result.stderr)
        self.assertEqual(self.calls(), [[40, 0], [40, 1]])
        self.assertFalse((self.output / "seed-0000000001.json").exists())
        self.assertEqual(len(summary["manifest"]), 1)

    @unittest.skipUnless(os.name == "posix", "SIGINT fixture requires POSIX")
    def test_interruption_returns_130_and_keeps_completed_seed_checkpoint(self):
        first = seed_record(0)
        first["careers"][0]["debtClear"] = [360]
        self.make_driver({0: {"record": first}, 1: {"mode": "interrupt"}})

        result = self.invoke()

        self.assertEqual(result.returncode, 130, result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "interrupted")
        self.assertEqual(summary["actualDebts"], 1)
        self.assertEqual(summary["completedSeeds"], 1)
        self.assertEqual(summary["nextSeedIndex"], 1)
        self.assertIn("KeyboardInterrupt", summary["error"])
        self.assertEqual(len(summary["manifest"]), 1)
        self.assertFalse((self.output / "seed-0000000001.json").exists())

    def test_sigterm_returns_130_and_keeps_completed_seed_checkpoint(self):
        first = seed_record(0)
        first["careers"][0]["debtClear"] = [360]
        self.make_driver({0: {"record": first}, 1: {"mode": "sigterm"}})

        result = self.invoke()

        self.assertEqual(result.returncode, 130, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("KeyboardInterrupt: SIGTERM", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        summary = self.read_summary()
        self.assertEqual(summary["outcome"], "interrupted")
        self.assertEqual(summary["error"], "KeyboardInterrupt: SIGTERM")
        self.assertEqual(summary["actualDebts"], 1)
        self.assertEqual(summary["completedSeeds"], 1)
        self.assertEqual(summary["nextSeedIndex"], 1)
        self.assertEqual(self.calls(), [[40, 0], [40, 1]])
        self.assertEqual(len(summary["manifest"]), 1)
        self.assertEqual(summary["manifest"][0]["seedIndex"], 0)
        self.assertEqual(
            [path.name for path in self.output.glob("seed-*.json")],
            ["seed-0000000000.json"],
        )
        self.assertEqual(
            json.loads((self.output / summary["manifest"][0]["file"]).read_text()),
            first,
        )
        self.assertIn("Outcome: interrupted", (self.output / "report.md").read_text())

    def test_missing_and_nonexecutable_drivers_fail_before_creating_output(self):
        self.make_driver({})
        self.assert_usage_error(
            self.invoke(driver=self.root / "missing driver"),
            "seed-driver must be an executable file",
        )
        self.driver.chmod(0o644)
        self.assert_usage_error(self.invoke(), "seed-driver must be an executable file")

    def test_invalid_options_and_uint32_wrap_fail_before_a_campaign_starts(self):
        self.make_driver({})
        for arguments, diagnostic in (
            (("--target-debts", "0"), "must be positive"),
            (("--max-seeds", "0"), "must be positive"),
            (("--seed-timeout", "0"), "must be positive"),
            (("--years", "0"), "years must be in"),
            (("--years", "1000001"), "years must be in"),
            (("--start-index", "-1"), "seed index range"),
            (("--start-index", str(2**32)), "seed index range"),
            (("--start-index", str(2**32 - 1), "--max-seeds", "2"), "seed index range"),
        ):
            with self.subTest(arguments=arguments):
                self.assert_usage_error(self.invoke(*arguments), diagnostic)

    def test_even_empty_inherited_simulation_overrides_are_rejected(self):
        self.make_driver({})
        for name in OVERRIDES:
            with self.subTest(name=name):
                self.assert_usage_error(
                    self.invoke(extra_env={name: ""}),
                    f"unset simulation overrides: {name}",
                )

    def test_existing_output_directory_is_preserved_and_not_resumed(self):
        self.make_driver({})
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("previous campaign", encoding="utf-8")

        result = self.invoke()

        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("File exists", result.stderr)
        self.assertEqual(marker.read_text(), "previous campaign")
        self.assertEqual(list(self.output.iterdir()), [marker])
        self.assertEqual(self.calls(), [])

    def test_invalid_sweeps_cannot_satisfy_target_with_unvalidated_episodes(self):
        valid = seed_record(0)
        valid["careers"][0]["debtClear"] = [360] * 10
        invalid = []
        wrong_metadata = copy.deepcopy(valid)
        wrong_metadata["horizonYears"] = 1
        invalid.append((wrong_metadata, "metadata does not match"))
        duplicate_group = copy.deepcopy(valid)
        duplicate_group["careers"][-1] = copy.deepcopy(duplicate_group["careers"][0])
        invalid.append((duplicate_group, "duplicate career style/tank"))
        wrong_seed = copy.deepcopy(valid)
        wrong_seed["careers"][-1]["seed"] += 1
        invalid.append((wrong_seed, "career seed/start does not match"))
        boolean_duration = copy.deepcopy(valid)
        boolean_duration["careers"][-1]["debtClear"] = [True]
        invalid.append((boolean_duration, "invalid completed debt durations"))
        bad_open = copy.deepcopy(valid)
        bad_open["careers"][-1]["debtOpen"] = -2
        invalid.append((bad_open, "invalid open debt age"))
        for number, (record, diagnostic) in enumerate(invalid):
            with self.subTest(diagnostic=diagnostic):
                output = self.root / f"invalid-{number}"
                self.make_driver({0: {"record": record}})
                result = self.invoke(output=output)
                self.assertEqual(result.returncode, 1, result.stderr)
                summary = self.read_summary(output)
                self.assertEqual(summary["outcome"], "error")
                self.assertEqual(summary["actualDebts"], 0)
                self.assertEqual(summary["completedSeeds"], 0)
                self.assertEqual(summary["nextSeedIndex"], 0)
                self.assertEqual(summary["manifest"], [])
                self.assertEqual(list(output.glob("seed-*.json")), [])
                self.assertIn(diagnostic, result.stderr)


if __name__ == "__main__":
    unittest.main()
