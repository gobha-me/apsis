"""Drive the real world wrapper with tooling executables, never native campaigns."""

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


WRAPPER = Path(__file__).resolve().parents[1] / "tools" / "determinism.py"
YEARS = 11
VALID_WORLD = {
    "intents": {
        "threadsOk": 7,
        "poolRuns": 0,
        "deciderDays": 12,
        "maxDeciders": 3,
    },
    "charMs": {"years": YEARS * 7},
}


class DeterminismWorldTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="determinism-world-tests-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build_dir = self.root / "fixture build"
        self.report_dir = self.root / "fixture reports"
        self.build_dir.mkdir()
        self.report_dir.mkdir()
        self.artifact_dir = self.report_dir / "apsis-determinism"
        self.executable = self.build_dir / "world_gates"

    def fixture(self, body):
        self.executable.write_text(
            f"#!{sys.executable}\n" + body + "\n", encoding="utf-8"
        )
        self.executable.chmod(0o700)

    def report_fixture(self, raw):
        self.fixture(
            "from pathlib import Path\n"
            f"Path('world_l2.json').write_bytes({raw!r})"
        )

    def run_world(self, years=YEARS):
        environment = os.environ.copy()
        environment.pop("GITHUB_STEP_SUMMARY", None)
        return subprocess.run(
            [
                sys.executable,
                str(WRAPPER),
                "world",
                "--build-dir",
                str(self.build_dir),
                "--report-dir",
                str(self.report_dir),
                "--years",
                str(years),
            ],
            capture_output=True,
            encoding="utf-8",
            env=environment,
            timeout=10,
            check=False,
        )

    def record(self):
        return json.loads(
            (self.artifact_dir / "world-run.json").read_text(encoding="utf-8")
        )

    def assert_failure(self, result, diagnostic, command_returncode=0):
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("world comparison failed:", result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        record = self.record()
        self.assertEqual(record["returncode"], command_returncode)
        self.assertIn(diagnostic, record["error"])
        self.assertEqual(record["years"], YEARS)
        return record

    def test_valid_fixture_records_command_horizon_and_combined_log(self):
        self.fixture(
            "import sys\n"
            "from pathlib import Path\n"
            f"Path('world_l2.json').write_text({json.dumps(VALID_WORLD)!r})\n"
            "print('fixture stdout: ' + repr(sys.argv[1:]))\n"
            "print('fixture stderr', file=sys.stderr)"
        )
        result = self.run_world()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(
            self.record(),
            {
                "command": [str(self.executable.resolve()), str(YEARS)],
                "years": YEARS,
                "returncode": 0,
                "error": None,
            },
        )
        log = (self.artifact_dir / "world.log").read_text(encoding="utf-8")
        self.assertIn(f"fixture stdout: ['{YEARS}']", log)
        self.assertIn("fixture stderr", log)
        self.assertEqual(
            json.loads((self.artifact_dir / "world_l2.json").read_text()),
            VALID_WORLD,
        )

    def test_thread_mismatch_fails_even_when_fixture_command_succeeds(self):
        # The native gate reports how many of its seven seed comparisons matched.
        for value in (0, 6, 8):
            with self.subTest(threadsOk=value):
                report = copy.deepcopy(VALID_WORLD)
                report["intents"]["threadsOk"] = value
                self.report_fixture(json.dumps(report).encode())
                self.assert_failure(
                    self.run_world(), f"threadsOk must equal 7, got {value}"
                )

    def test_missing_or_malformed_threads_ok_is_rejected(self):
        for value in (None, True, False, 7.0, "7", [], {}, -1):
            with self.subTest(threadsOk=value):
                report = copy.deepcopy(VALID_WORLD)
                report["intents"]["threadsOk"] = value
                self.report_fixture(json.dumps(report).encode())
                self.assert_failure(
                    self.run_world(), "intents.threadsOk must be a nonnegative integer"
                )
        report = copy.deepcopy(VALID_WORLD)
        del report["intents"]["threadsOk"]
        self.report_fixture(json.dumps(report).encode())
        self.assert_failure(
            self.run_world(), "intents.threadsOk must be a nonnegative integer"
        )

    def test_wrong_report_and_intents_shapes_are_rejected(self):
        for report, diagnostic in (
            ([], "world report must be an object"),
            (None, "world report must be an object"),
            ({}, "intents must be an object"),
            ({"intents": []}, "intents must be an object"),
        ):
            with self.subTest(report=report):
                self.report_fixture(json.dumps(report).encode())
                self.assert_failure(self.run_world(), diagnostic)

    def test_invalid_pool_and_decision_counters_are_rejected(self):
        for field in ("poolRuns", "deciderDays", "maxDeciders"):
            for value in (None, True, -1, 1.5, "0"):
                with self.subTest(field=field, value=value):
                    report = copy.deepcopy(VALID_WORLD)
                    report["intents"][field] = value
                    self.report_fixture(json.dumps(report).encode())
                    self.assert_failure(
                        self.run_world(),
                        f"intents.{field} must be a nonnegative integer",
                    )

    def test_wrong_horizon_and_malformed_timing_are_rejected(self):
        for timing, diagnostic in (
            (None, "charMs must be an object"),
            ({}, "charMs.years must be a nonnegative integer"),
            ({"years": True}, "charMs.years must be a nonnegative integer"),
            ({"years": YEARS}, "charMs.years must equal the requested horizon times 7"),
        ):
            with self.subTest(timing=timing):
                report = copy.deepcopy(VALID_WORLD)
                report["charMs"] = timing
                self.report_fixture(json.dumps(report).encode())
                self.assert_failure(self.run_world(), diagnostic)

    def test_missing_report_after_zero_exit_is_rejected(self):
        self.fixture("pass")
        self.assert_failure(self.run_world(), "No such file or directory")

    def test_stale_passing_evidence_cannot_mask_missing_fresh_output(self):
        self.artifact_dir.mkdir()
        for name, content in (
            ("world_l2.json", json.dumps(VALID_WORLD)),
            ("world-run.json", '{"returncode": 0, "error": null}'),
            ("world.log", "old successful run"),
        ):
            (self.artifact_dir / name).write_text(content, encoding="utf-8")
        self.fixture("print('fresh fixture produced no report')")
        self.assert_failure(self.run_world(), "No such file or directory")
        self.assertFalse((self.artifact_dir / "world_l2.json").exists())
        self.assertEqual(
            (self.artifact_dir / "world.log").read_text(encoding="utf-8"),
            "fresh fixture produced no report\n",
        )

    def test_invalid_json_duplicate_keys_and_nonfinite_values_are_rejected(self):
        valid = json.dumps(VALID_WORLD)
        cases = (
            (b"", "Expecting value"),
            (b"{", "Expecting property name"),
            (valid.encode() + b" trailing", "Extra data"),
            (
                valid.replace(
                    '"threadsOk": 7', '"threadsOk": 6, "threadsOk": 7'
                ).encode(),
                "duplicate JSON key: threadsOk",
            ),
        )
        for raw, diagnostic in cases:
            with self.subTest(raw=raw):
                self.report_fixture(raw)
                self.assert_failure(self.run_world(), diagnostic)
        for constant in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(constant=constant):
                self.report_fixture(
                    valid.replace('"poolRuns": 0', f'"poolRuns": {constant}').encode()
                )
                self.assert_failure(
                    self.run_world(), f"non-finite JSON value: {constant}"
                )

    def test_non_utf8_report_is_rejected(self):
        self.report_fixture(b"\xff\xfe")
        self.assert_failure(self.run_world(), "utf-8")

    def test_report_directory_is_rejected_as_unreadable(self):
        self.fixture("from pathlib import Path\nPath('world_l2.json').mkdir()")
        self.assert_failure(self.run_world(), "Is a directory")

    def test_permission_denied_report_is_rejected(self):
        probe = self.root / "permission-probe"
        probe.write_text("probe", encoding="utf-8")
        probe.chmod(0)
        self.addCleanup(probe.chmod, 0o600)
        if os.access(probe, os.R_OK):
            self.skipTest("process can read permission-denied files")
        self.fixture(
            "from pathlib import Path\n"
            f"Path('world_l2.json').write_text({json.dumps(VALID_WORLD)!r})\n"
            "Path('world_l2.json').chmod(0)"
        )
        try:
            self.assert_failure(self.run_world(), "Permission denied")
        finally:
            report = self.artifact_dir / "world_l2.json"
            if report.exists():
                report.chmod(0o600)

    def test_nonzero_command_fails_despite_passing_report(self):
        self.fixture(
            "import sys\n"
            "from pathlib import Path\n"
            f"Path('world_l2.json').write_text({json.dumps(VALID_WORLD)!r})\n"
            "print('fixture command failed', file=sys.stderr)\n"
            "sys.exit(23)"
        )
        self.assert_failure(
            self.run_world(), "world_gates exited with status 23", command_returncode=23
        )
        self.assertIn(
            "fixture command failed",
            (self.artifact_dir / "world.log").read_text(encoding="utf-8"),
        )

    def test_missing_executable_is_surfaced_with_no_command_exit_status(self):
        record = self.assert_failure(
            self.run_world(), "No such file or directory", command_returncode=None
        )
        self.assertEqual(record["command"], [str(self.executable), str(YEARS)])
        self.assertIn(str(self.executable), record["error"])

    def test_non_executable_fixture_is_surfaced(self):
        self.fixture("pass")
        self.executable.chmod(0o600)
        self.assert_failure(
            self.run_world(), "Permission denied", command_returncode=None
        )


if __name__ == "__main__":
    unittest.main()
