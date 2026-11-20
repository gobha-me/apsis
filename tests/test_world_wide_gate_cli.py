"""Run the real gate CLI against a small world_wide dependency executable."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
HARNESS = REPO_ROOT / "tools" / "world_wide_gate.py"


def aggregate(seeds=2, off_war=1, on_war=1):
    def arm(wars):
        return {
            "n": seeds,
            "war": wars,
            "episodes": 10,
            "fed": 6,
            "slow": 3,
            "slowLowMeanPm": 145,
            "slowLowP10Pm": 24,
            "slowEmpty": 1,
            "slowDaysMean": 75,
        }

    return {"off": arm(off_war), "on": arm(on_war)}


class WorldWideGateCliTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="world-wide-gate-cli-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.binary = self.root / "fixture world_wide"
        self.output = self.root / "fresh sample"
        self.marker = self.root / "executed"

    def write_binary(self, report=None, *, seeds=2, extra="", returncode=0):
        # This executable replaces only the dependency; the harness is unmodified.
        source = (
            f"#!{sys.executable}\n"
            "from pathlib import Path\n"
            "import sys\n"
            f"Path({str(self.marker)!r}).write_text('executed', encoding='utf-8')\n"
            f"if sys.argv[1:] != [{str(seeds)!r}]:\n"
            "    sys.exit(97)\n"
            "print('fixture stdout', flush=True)\n"
            "print('fixture stderr', file=sys.stderr, flush=True)\n"
        )
        if report is not None:
            source += (
                f"Path('world_wide_l2.json').write_text({report!r}, "
                "encoding='utf-8')\n"
            )
        source += extra + "\n" + f"sys.exit({returncode})\n"
        self.binary.write_text(source, encoding="utf-8")
        self.binary.chmod(0o755)

    def run_cli(self, *extra, seeds=2, binary=None):
        return subprocess.run(
            [
                sys.executable,
                str(HARNESS),
                "--world-wide",
                str(binary or self.binary),
                "--output-dir",
                str(self.output),
                "--seeds",
                str(seeds),
                *extra,
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

    def assert_saved_result(self, completed, exit_code, status):
        self.assertEqual(completed.returncode, exit_code, completed.stderr)
        self.assertEqual(completed.stderr, "")
        self.assertNotIn("Traceback", completed.stdout)
        result = json.loads((self.output / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(result["status"], status)
        self.assertTrue(completed.stdout.startswith(status.upper() + ":"))
        self.assertEqual(
            (self.output / "summary.txt").read_text(encoding="utf-8"),
            completed.stdout,
        )
        self.assertGreaterEqual(result["elapsed_seconds"], 0)
        return result

    def assert_runtime_error(self, completed, diagnostic):
        result = self.assert_saved_result(completed, 2, "error")
        self.assertEqual(result["report_check"], "error")
        self.assertIn(diagnostic, result["error"])
        self.assertNotIn("war_gate", result)
        self.assertNotIn("War gate PASS", completed.stdout)
        return result

    def test_two_seed_fixture_pass_retains_artifacts_and_provenance(self):
        report = json.dumps(aggregate())
        self.write_binary(report)
        completed = self.run_cli()
        result = self.assert_saved_result(completed, 0, "pass")
        self.assertEqual(result["report_check"], "pass")
        self.assertEqual(result["returncode"], 0)
        self.assertEqual(result["command"], [str(self.binary), "2"])
        self.assertEqual(result["output_directory"], str(self.output))
        self.assertEqual(result["sample"]["seeds_per_arm"], 2)
        self.assertEqual(result["sample"]["kind"], "smoke_or_custom")
        self.assertEqual(result["sample"]["years"], [20, 60])
        self.assertEqual(result["sample"]["warmup_days"], 7200)
        self.assertEqual(result["sample"]["measurement_days"], 14400)
        self.assertEqual(
            result["sample"]["seed_recipe"],
            "apsis::hashN(20261118, i), i in [0, N); paired off/on",
        )
        self.assertEqual(
            result["sample"]["completion_evidence"],
            "aggregate n per arm; per-seed records unavailable",
        )
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True
        ).strip()
        self.assertEqual(result["revision"], revision)
        self.assertIsInstance(result["source_dirty"], bool)
        self.assertEqual(
            result["binary_sha256"],
            hashlib.sha256(self.binary.read_bytes()).hexdigest(),
        )
        self.assertFalse(result["binary_revision_verified"])
        self.assertEqual(
            result["port_gate"],
            {"status": "unavailable", "reason": "frozen v0.20 absent"},
        )
        self.assertEqual(
            (self.output / "world_wide_l2.json").read_text(encoding="utf-8"), report
        )
        self.assertEqual(
            (self.output / "stdout.txt").read_text(encoding="utf-8"), "fixture stdout\n"
        )
        self.assertEqual(
            (self.output / "stderr.txt").read_text(encoding="utf-8"), "fixture stderr\n"
        )
        self.assertEqual(result["arms"]["off"]["war"]["denominator"], 2)
        self.assertEqual(result["arms"]["on"]["war"]["count"], 1)
        self.assertIn("War gate PASS", completed.stdout)

    def test_separated_war_intervals_exit_one_with_valid_report(self):
        self.write_binary(json.dumps(aggregate(20, 2, 18)), seeds=20)
        completed = self.run_cli(seeds=20)
        result = self.assert_saved_result(completed, 1, "fail")
        self.assertEqual(result["report_check"], "pass")
        self.assertEqual(result["war_gate"]["status"], "fail")
        self.assertFalse(result["war_gate"]["ratio_in_range"])
        self.assertFalse(result["war_gate"]["intervals_overlap"])
        self.assertIn("War gate FAIL", completed.stdout)
        self.assertNotIn("error", result)

    def test_two_seed_plan_does_not_execute_or_create_output(self):
        self.write_binary(json.dumps(aggregate()))
        completed = self.run_cli("--dry-run")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        plan = json.loads(completed.stdout)
        self.assertEqual(plan["status"], "dry_run")
        self.assertEqual(plan["command"], [str(self.binary), "2"])
        self.assertEqual(plan["sample"]["seeds_per_arm"], 2)
        self.assertEqual(plan["sample"]["kind"], "smoke_or_custom")
        self.assertEqual(plan["timeout_seconds"], 3600)
        self.assertNotIn("war_gate", plan)
        self.assertNotIn("PASS", completed.stdout)
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.output.exists())

    def test_dry_run_accepts_missing_binary_without_claiming_pass(self):
        completed = self.run_cli("--dry-run")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["status"], "dry_run")
        self.assertFalse(self.output.exists())

    def test_missing_binary_exits_two_and_writes_error_artifacts(self):
        completed = self.run_cli()
        self.assert_runtime_error(completed, "FileNotFoundError")
        self.assertFalse((self.output / "world_wide_l2.json").exists())

    def test_nonexecutable_binary_exits_two_without_running_it(self):
        self.write_binary(json.dumps(aggregate()))
        self.binary.chmod(0o644)
        self.assert_runtime_error(self.run_cli(), "binary is not executable")
        self.assertFalse(self.marker.exists())

    def test_successful_dependency_without_report_exits_two(self):
        self.write_binary()
        result = self.assert_runtime_error(self.run_cli(), "world_wide_l2.json")
        self.assertEqual(result["returncode"], 0)
        self.assertTrue((self.output / "stdout.txt").is_file())
        self.assertTrue((self.output / "stderr.txt").is_file())

    def test_malformed_report_exits_two_and_preserves_raw_evidence(self):
        self.write_binary("{broken JSON")
        result = self.assert_runtime_error(self.run_cli(), "JSONDecodeError")
        self.assertEqual(result["returncode"], 0)
        self.assertEqual(
            (self.output / "world_wide_l2.json").read_text(encoding="utf-8"),
            "{broken JSON",
        )

    def test_nonzero_dependency_exit_cannot_be_masked_by_valid_report(self):
        self.write_binary(json.dumps(aggregate()), returncode=7)
        result = self.assert_runtime_error(self.run_cli(), "world_wide exited 7")
        self.assertEqual(result["returncode"], 7)
        self.assertTrue((self.output / "world_wide_l2.json").is_file())
        self.assertEqual(
            (self.output / "stderr.txt").read_text(encoding="utf-8"), "fixture stderr\n"
        )

    def test_timeout_exits_two_even_if_dependency_already_wrote_report(self):
        self.write_binary(json.dumps(aggregate()), extra="import time\ntime.sleep(30)")
        result = self.assert_runtime_error(
            self.run_cli("--timeout-seconds", "1"), "TimeoutExpired"
        )
        self.assertEqual(result["timeout_seconds"], 1)
        self.assertNotIn("returncode", result)
        self.assertTrue((self.output / "world_wide_l2.json").is_file())
        self.assertIn(
            "fixture stdout", (self.output / "stdout.txt").read_text(encoding="utf-8")
        )

    def test_existing_output_is_never_overwritten_or_executed(self):
        self.write_binary(json.dumps(aggregate()))
        self.output.mkdir()
        sentinel = self.output / "result.json"
        sentinel.write_text("previous result", encoding="utf-8")
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("cannot create fresh output directory", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "previous result")
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertFalse(self.marker.exists())

    def test_result_write_error_returns_two_without_success_summary(self):
        self.write_binary(json.dumps(aggregate()), extra="Path('result.json').mkdir()")
        completed = self.run_cli()
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("cannot write harness result", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)
        self.assertTrue((self.output / "world_wide_l2.json").is_file())
        self.assertFalse((self.output / "summary.txt").exists())

    def test_cli_rejects_invalid_seed_and_timeout_values_before_writing(self):
        for option in ("--seeds", "--timeout-seconds"):
            for value in ("0", "-1", "2147483648", "1.5", "not-an-integer"):
                with self.subTest(option=option, value=value):
                    completed = self.run_cli(option, value)
                    self.assertEqual(completed.returncode, 2)
                    self.assertEqual(completed.stdout, "")
                    self.assertIn("usage:", completed.stderr)
                    self.assertNotIn("Traceback", completed.stderr)
                    self.assertFalse(self.output.exists())

    def test_missing_required_arguments_exit_two(self):
        completed = subprocess.run(
            [sys.executable, str(HARNESS)],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("--world-wide", completed.stderr)
        self.assertIn("--output-dir", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
