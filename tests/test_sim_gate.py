"""Exercise runner failure handling with executables, plus opt-in real smoke tests."""

import copy
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/sim_gate.py"
SPEC = importlib.util.spec_from_file_location("sim_gate", SCRIPT)
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def valid_report():
    return {
        "schema_version": 1,
        "seed": 7,
        "scenario": "world",
        "requested_days": 3,
        "simulated_days": 3,
        "final_day": 3,
        "states_checked": 4,
        "peak_entities": 24,
        "first_failure_day": -1,
        "end_state": 0,
        "deliveries": 0,
        "numeric_domain": "integer_checkpoint",
        "state_hash": "0123456789abcdef",
        "checks": dict.fromkeys(GATE.CHECKS, True),
    }


class SimGateTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sim-gate-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.driver = self.root / "fixture driver"
        self.output = self.root / "reports with spaces"
        self.report = self.root / "report.json"

    def fixture(self, body):
        self.driver.write_text(f"#!{sys.executable}\n{body}\n", encoding="utf-8")
        self.driver.chmod(0o700)

    def report_fixture(self, report=None, status=0):
        report = valid_report() if report is None else report
        self.fixture(f"import sys\nprint({json.dumps(report)!r})\nsys.exit({status})")

    def run_gate(self, *extra):
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--driver",
                str(self.driver),
                "--output-dir",
                str(self.output),
                "--seeds",
                "7",
                "--scenarios",
                "world",
                "--days",
                "3",
                *extra,
            ],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

    def summary(self):
        return json.loads((self.output / "summary.json").read_text())

    def test_passing_matrix_saves_machine_and_human_summaries(self):
        self.report_fixture()
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = self.summary()
        self.assertEqual(summary["counts"], {"pass": 1, "fail": 0, "error": 0})
        self.assertEqual(summary["cases"][0]["report"], valid_report())
        self.assertRegex(summary["driver_sha256"], r"^[0-9a-f]{64}$")
        self.assertIn("Simulation matrix: PASS", result.stdout)
        self.assertEqual(result.stdout, (self.output / "summary.txt").read_text())

    def test_failed_invariant_is_not_an_execution_error(self):
        report = valid_report()
        report["checks"]["resource_conservation"] = False
        report["first_failure_day"] = 3
        self.report_fixture(report, 1)
        self.assertEqual(self.run_gate().returncode, 1)
        self.assertEqual(self.summary()["counts"]["fail"], 1)

    def test_exit_failure_cannot_be_hidden_by_passing_json(self):
        for status in (1, 7, -15):
            with self.subTest(status=status):
                self.output = self.root / f"exit-{status}"
                self.report_fixture(status=status)
                self.assertEqual(self.run_gate().returncode, 2)
                self.assertEqual(self.summary()["status"], "error")

    def test_failed_report_cannot_be_hidden_by_success_exit(self):
        report = valid_report()
        report["checks"]["outcome_reachable"] = False
        self.report_fixture(report)
        self.assertEqual(self.run_gate().returncode, 2)
        self.assertIn("disagrees", self.summary()["cases"][0]["error"])

    def test_timeout_is_recorded_and_later_cases_still_run(self):
        self.fixture("import time\nprint('partial', flush=True)\ntime.sleep(2)")
        result = self.run_gate("--timeout-seconds", "0.03", "--seeds", "7,8")
        self.assertEqual(result.returncode, 2, result.stderr)
        summary = self.summary()
        self.assertEqual(summary["counts"]["error"], 2)
        self.assertEqual([case["seed"] for case in summary["cases"]], [7, 8])
        self.assertTrue((self.output / "7-world/stderr.txt").exists())

    def test_malformed_missing_and_oversized_reports_are_errors(self):
        for index, raw in enumerate(("", "garbage", "[]", "{}", "x" * 65537)):
            with self.subTest(index=index):
                self.output = self.root / f"malformed-{index}"
                self.fixture(f"print({raw!r})")
                self.assertEqual(self.run_gate().returncode, 2)
                self.assertEqual(self.summary()["counts"]["error"], 1)

    def test_report_rejects_duplicate_keys_and_nonfinite_constants(self):
        raw = json.dumps(valid_report())
        for addition in (
            '"seed": 7',
            '"extra": NaN',
            '"extra": Infinity',
            '"extra": 1e999',
        ):
            with self.subTest(addition=addition):
                self.report.write_text(raw[:-1] + "," + addition + "}")
                with self.assertRaises(ValueError):
                    GATE.validate_report(self.report, 7, "world", 3, 100000)

    def test_invalid_types_provenance_and_incomplete_passes_are_rejected(self):
        changes = {
            "schema_version": True,
            "seed": 8,
            "scenario": "hauler",
            "requested_days": 4,
            "simulated_days": 2,
            "final_day": 2,
            "states_checked": 0,
            "peak_entities": 100001,
            "first_failure_day": 0,
            "end_state": 4,
            "deliveries": -1,
            "numeric_domain": "float",
            "state_hash": "xyz",
            "checks": {},
        }
        for field, value in changes.items():
            with self.subTest(field=field):
                report = valid_report()
                report[field] = value
                self.report.write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    GATE.validate_report(self.report, 7, "world", 3, 100000)
        report = copy.deepcopy(valid_report())
        report["checks"]["resource_conservation"] = 1
        self.report.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "booleans"):
            GATE.validate_report(self.report, 7, "world", 3, 100000)

    def test_existing_output_is_never_reused_or_overwritten(self):
        self.report_fixture()
        self.output.mkdir()
        marker = self.output / "summary.json"
        marker.write_text("old evidence")
        self.assertEqual(self.run_gate().returncode, 2)
        self.assertEqual(marker.read_text(), "old evidence")

    def test_missing_driver_fails_before_creating_output(self):
        self.assertEqual(self.run_gate().returncode, 2)
        self.assertFalse(self.output.exists())

    def test_environment_overrides_are_removed(self):
        self.fixture(
            "import os\n"
            "assert not any(key.startswith('APSIS_') for key in os.environ)\n"
            f"print({json.dumps(valid_report())!r})"
        )
        from unittest.mock import patch

        with patch.dict(os.environ, {"APSIS_SET": "untrusted override"}):
            self.assertEqual(self.run_gate().returncode, 0)

    def test_cli_rejects_invalid_bounds_and_duplicate_matrix_entries(self):
        self.report_fixture()
        for option, value in (
            ("--seeds", "-1"),
            ("--seeds", "4294967296"),
            ("--seeds", "7,7"),
            ("--scenarios", "world,world"),
            ("--scenarios", "unknown"),
            ("--days", "0"),
            ("--days", "36001"),
            ("--max-entities", "0"),
            ("--timeout-seconds", "nan"),
            ("--timeout-seconds", "inf"),
        ):
            with self.subTest(option=option, value=value):
                result = self.run_gate(option, value)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(self.output.exists())


@unittest.skipUnless(
    os.environ.get("APSIS_SIM_INVARIANT_DRIVER"),
    "set APSIS_SIM_INVARIANT_DRIVER for native smoke",
)
class NativeSimGateTests(unittest.TestCase):
    def test_all_scenarios_and_entity_limit_failure(self):
        with tempfile.TemporaryDirectory(prefix="sim-gate-native-") as directory:
            for limit, expected in ((100000, 0), (1, 1)):
                output = Path(directory) / str(limit)
                result = subprocess.run(
                    [
                        sys.executable,
                        str(SCRIPT),
                        "--driver",
                        os.environ["APSIS_SIM_INVARIANT_DRIVER"],
                        "--output-dir",
                        str(output),
                        "--seeds",
                        "333860,697804",
                        "--days",
                        "30",
                        "--max-entities",
                        str(limit),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(
                    result.returncode, expected, result.stdout + result.stderr
                )
                summary = json.loads((output / "summary.json").read_text())
                self.assertEqual(
                    summary["counts"]["pass" if expected == 0 else "fail"], 12
                )


if __name__ == "__main__":
    unittest.main()
