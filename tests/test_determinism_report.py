"""Drive the determinism report CLI with tooling evidence, never native gates."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
STEPS = ("build", "prepare", "layer1", "standing", "world")
NATIVE_REPORTS = {
    "gates_l1.json": [{"gate": "fixture", "ok": 1, "n": 1}],
    "world_core_l2.json": {
        "checkpointResume": [14, 14],
        "roundTrip": [14, 14],
        "teeth": [20, 20],
    },
    "pilot_gates_l3.json": {
        "replay": [5, 5],
        "resume": [5, 5],
        "roundTrip": [5, 5],
        "order": [5, 5],
        "teeth": [4, 5],
    },
    "pilot_switches_l4.json": {},
    "pilot_endstates_l4.json": {},
    "pilot_law_l4.json": {},
}
WORLD_REPORT = {
    "intents": {"threadsOk": 7, "poolRuns": 0, "deciderDays": 11, "maxDeciders": 3},
    "charMs": {"years": 14},
}


class DeterminismReportTests(unittest.TestCase):
    def write_json(self, path, data):
        path.write_text(json.dumps(data), encoding="utf-8")

    def read_json(self, path):
        return json.loads(path.read_text(encoding="utf-8"))

    def fixture_reports(self):
        directory = tempfile.TemporaryDirectory(prefix="determinism-report-tests-")
        self.addCleanup(directory.cleanup)
        report_dir = Path(directory.name)
        artifact_dir = report_dir / "apsis-determinism"
        artifact_dir.mkdir()
        for name, data in NATIVE_REPORTS.items():
            self.write_json(report_dir / name, data)
        self.write_json(artifact_dir / "world_l2.json", WORLD_REPORT)
        self.write_json(
            artifact_dir / "world-run.json",
            {
                "command": ["fixture-world-gates", "2"],
                "years": 2,
                "returncode": 0,
                "error": None,
            },
        )
        return report_dir, artifact_dir

    def run_cli(self, report_dir, **statuses):
        arguments = ["report", "--report-dir", str(report_dir)]
        for step in STEPS:
            arguments.extend([f"--{step}-status", statuses.get(step, "success")])
        environment = os.environ.copy()
        environment.pop("GITHUB_STEP_SUMMARY", None)
        return subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools" / "determinism.py"), *arguments],
            cwd=report_dir,
            env=environment,
            capture_output=True,
            encoding="utf-8",
            timeout=10,
            check=False,
        )

    def assert_failure(self, report_dir, artifact_dir, diagnostic, **statuses):
        result = self.run_cli(report_dir, **statuses)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("# Native determinism: FAIL", result.stdout)
        summary = self.read_json(artifact_dir / "summary.json")
        self.assertIs(summary["passed"], False)
        self.assertTrue(
            any(diagnostic in failure for failure in summary["failures"]), summary
        )
        self.assertIn(diagnostic, result.stdout)
        self.assertEqual(
            (artifact_dir / "report.md").read_text(encoding="utf-8") + "\n",
            result.stdout,
        )
        return result

    def test_complete_fixture_passes_without_requiring_parallel_pool_use(self):
        report_dir, artifact_dir = self.fixture_reports()
        result = self.run_cli(report_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("# Native determinism: PASS", result.stdout)
        self.assertIn("| World final hashes at threads 1/2/8 | 7/7 |", result.stdout)
        self.assertIn("| World pool-use days (2/8 runs combined) | 0 |", result.stdout)
        self.assertIn("| Pilot teeth (informational) | 4/5 |", result.stdout)
        self.assertIn(
            "World thread settings alone do not prove parallel decisions were "
            "exercised",
            result.stdout,
        )
        self.assertIn(
            "World corruption means restore refuses OR the resulting checkpoint "
            "differs",
            result.stdout,
        )
        self.assertEqual(
            self.read_json(artifact_dir / "summary.json"),
            {
                "passed": True,
                "steps": dict.fromkeys(STEPS, "success"),
                "failures": [],
            },
        )
        for name in NATIVE_REPORTS:
            self.assertEqual(
                (artifact_dir / name).read_bytes(), (report_dir / name).read_bytes()
            )
        self.assertEqual(
            (artifact_dir / "report.md").read_text(encoding="utf-8") + "\n",
            result.stdout,
        )

    def test_thread_mismatch_fails_despite_successful_command_and_steps(self):
        report_dir, artifact_dir = self.fixture_reports()
        path = artifact_dir / "world_l2.json"
        data = self.read_json(path)
        data["intents"]["threadsOk"] = 6
        self.write_json(path, data)
        result = self.assert_failure(
            report_dir, artifact_dir, "threadsOk must equal 7, got 6"
        )
        self.assertNotIn("| World final hashes at threads 1/2/8 | 7/7 |", result.stdout)

    def test_report_rejects_malformed_threads_ok(self):
        for value in (True, "7", 7.0, None):
            with self.subTest(value=value):
                report_dir, artifact_dir = self.fixture_reports()
                path = artifact_dir / "world_l2.json"
                data = self.read_json(path)
                data["intents"]["threadsOk"] = value
                self.write_json(path, data)
                self.assert_failure(
                    report_dir,
                    artifact_dir,
                    "intents.threadsOk must be a nonnegative integer",
                )

    def test_missing_evidence_is_rejected_instead_of_reusing_copied_reports(self):
        for name in (*NATIVE_REPORTS, "world_l2.json", "world-run.json"):
            with self.subTest(name=name):
                report_dir, artifact_dir = self.fixture_reports()
                path = (report_dir if name in NATIVE_REPORTS else artifact_dir) / name
                if name in NATIVE_REPORTS:
                    (artifact_dir / name).write_bytes(path.read_bytes())
                path.unlink()
                self.assert_failure(report_dir, artifact_dir, name)

    def test_malformed_json_and_invalid_encoding_are_rejected(self):
        for name in ("gates_l1.json", "world_l2.json", "world-run.json"):
            for contents, diagnostic in (
                (b"{", "Expecting property name"),
                (b"\xff", "utf-8"),
            ):
                with self.subTest(name=name, contents=contents):
                    report_dir, artifact_dir = self.fixture_reports()
                    source_dir = report_dir if name in NATIVE_REPORTS else artifact_dir
                    path = source_dir / name
                    path.write_bytes(contents)
                    self.assert_failure(report_dir, artifact_dir, diagnostic)

    def test_directory_instead_of_readable_report_is_rejected(self):
        for name in ("gates_l1.json", "world_l2.json", "world-run.json"):
            with self.subTest(name=name):
                report_dir, artifact_dir = self.fixture_reports()
                path = (report_dir if name in NATIVE_REPORTS else artifact_dir) / name
                path.unlink()
                path.mkdir()
                self.assert_failure(report_dir, artifact_dir, "Is a directory")

    def test_unreadable_native_report_is_rejected(self):
        report_dir, artifact_dir = self.fixture_reports()
        path = report_dir / "gates_l1.json"
        path.chmod(0)
        try:
            if os.access(path, os.R_OK):
                self.skipTest(
                    "current user can read files with permission bits removed"
                )
            self.assert_failure(report_dir, artifact_dir, "Permission denied")
        finally:
            path.chmod(0o600)

    def test_failed_command_record_cannot_be_masked_by_passing_world_report(self):
        for returncode in (17, None, False, "0"):
            with self.subTest(returncode=returncode):
                report_dir, artifact_dir = self.fixture_reports()
                path = artifact_dir / "world-run.json"
                record = self.read_json(path)
                record["returncode"] = returncode
                self.write_json(path, record)
                self.assert_failure(report_dir, artifact_dir, "world command failed:")

    def test_missing_command_status_is_rejected(self):
        report_dir, artifact_dir = self.fixture_reports()
        path = artifact_dir / "world-run.json"
        record = self.read_json(path)
        del record["returncode"]
        self.write_json(path, record)
        self.assert_failure(report_dir, artifact_dir, "world command failed: None")

    def test_recorded_wrapper_error_is_rejected_even_with_zero_command_status(self):
        report_dir, artifact_dir = self.fixture_reports()
        path = artifact_dir / "world-run.json"
        record = self.read_json(path)
        record["error"] = "fixture validation failed"
        self.write_json(path, record)
        self.assert_failure(
            report_dir, artifact_dir, "world wrapper failed: fixture validation failed"
        )

    def test_failed_or_skipped_step_is_rejected_with_otherwise_passing_evidence(self):
        for step in STEPS:
            for status in ("failure", "skipped"):
                with self.subTest(step=step, status=status):
                    report_dir, artifact_dir = self.fixture_reports()
                    self.assert_failure(
                        report_dir, artifact_dir, f"{step}: {status}", **{step: status}
                    )
                    summary = self.read_json(artifact_dir / "summary.json")
                    self.assertEqual(summary["steps"][step], status)

    def test_record_horizon_must_be_positive_and_match_world_output(self):
        for years, diagnostic in (
            (0, "world horizon must be positive"),
            (3, "charMs.years must equal the requested horizon times 7"),
        ):
            with self.subTest(years=years):
                report_dir, artifact_dir = self.fixture_reports()
                path = artifact_dir / "world-run.json"
                record = self.read_json(path)
                record["years"] = years
                self.write_json(path, record)
                self.assert_failure(report_dir, artifact_dir, diagnostic)

    def test_informational_count_rejects_wrong_total(self):
        for count in (4, 6):
            with self.subTest(count=count):
                report_dir, artifact_dir = self.fixture_reports()
                path = report_dir / "pilot_gates_l3.json"
                data = self.read_json(path)
                data["teeth"] = [count, count]
                self.write_json(path, data)
                self.assert_failure(
                    report_dir,
                    artifact_dir,
                    f"teeth: got {count}/{count}, expected 5/5",
                )

    def test_layer1_gate_below_total_fails_the_report(self):
        report_dir, artifact_dir = self.fixture_reports()
        self.write_json(
            report_dir / "gates_l1.json", [{"gate": "fixture", "ok": 0, "n": 1}]
        )
        self.assert_failure(
            report_dir, artifact_dir, "Layer-1: layer-1 gate failed: fixture"
        )

    def test_empty_layer1_report_is_rejected(self):
        report_dir, artifact_dir = self.fixture_reports()
        self.write_json(report_dir / "gates_l1.json", [])
        self.assert_failure(
            report_dir, artifact_dir, "Layer-1: layer-1 report must be a nonempty array"
        )

    def test_existing_enforced_checkpoint_counts_still_fail_the_report(self):
        for name, field, value in (
            ("world_core_l2.json", "checkpointResume", [13, 14]),
            ("world_core_l2.json", "teeth", [19, 20]),
            ("pilot_gates_l3.json", "replay", [4, 5]),
        ):
            with self.subTest(name=name, field=field):
                report_dir, artifact_dir = self.fixture_reports()
                path = report_dir / name
                data = self.read_json(path)
                data[field] = value
                self.write_json(path, data)
                self.assert_failure(report_dir, artifact_dir, f"{field}: got")


if __name__ == "__main__":
    unittest.main()
