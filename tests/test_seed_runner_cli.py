"""Exercise the seed-runner command with actual importable adapter inputs."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
ADAPTER_SOURCE = """\
import sys

from tools.seed_runner import SeedOutcome

not_callable = 42

def outcome(seed):
    if seed == 20:
        return SeedOutcome("failed", {"score": -2}, "below threshold")
    if seed == 30:
        return SeedOutcome("skipped", note="not applicable")
    if seed == 40:
        raise RuntimeError("adapter-detail-must-stay-private")
    return SeedOutcome("passed", {"seed": seed, "score": 1.25}, "accepted")

def invalid_metric(seed):
    if seed == 1:
        return SeedOutcome("passed", {"invalid": float("nan")})
    return outcome(seed)

def exit_zero(seed):
    sys.exit(0)

def noisy_outcome(seed):
    print("adapter-hook-progress", seed)
    return outcome(seed)

def huge_int_metric(seed):
    # Exercise serialization failure even if the environment disables the limit.
    sys.set_int_max_str_digits(4300)
    return SeedOutcome("passed", {"huge": 10**5000})
"""


class SeedRunnerCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adapter_directory = tempfile.TemporaryDirectory(prefix="apsis-cli-")
        cls.addClassCleanup(cls.adapter_directory.cleanup)
        adapter_path = Path(cls.adapter_directory.name) / "apsis_cli_adapter.py"
        adapter_path.write_text(textwrap.dedent(ADAPTER_SOURCE), encoding="utf-8")
        extra_adapters = {
            "apsis_cli_syntax_adapter": "def outcome(:\n    pass\n",
            "apsis_cli_runtime_adapter": (
                'raise RuntimeError("broken adapter import")\n'
            ),
            "apsis_cli_noisy_adapter": (
                'print("adapter-import-progress")\n'
                "from apsis_cli_adapter import noisy_outcome as outcome\n"
            ),
        }
        for module, source in extra_adapters.items():
            path = Path(cls.adapter_directory.name) / f"{module}.py"
            path.write_text(source, encoding="utf-8")
        cls.environment = os.environ.copy()
        python_paths = [cls.adapter_directory.name, str(REPOSITORY)]
        if cls.environment.get("PYTHONPATH"):
            python_paths.append(cls.environment["PYTHONPATH"])
        cls.environment["PYTHONPATH"] = os.pathsep.join(python_paths)
        cls.environment["PYTHONDONTWRITEBYTECODE"] = "1"

    def run_cli(self, *arguments):
        return subprocess.run(
            [sys.executable, "-m", "tools.seed_runner", *arguments],
            cwd=REPOSITORY,
            env=self.environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=10,
            check=False,
        )

    def parse_report(self, process, expected_exit):
        self.assertEqual(process.returncode, expected_exit, process.stderr)
        self.assertEqual(process.stderr, "")
        report = json.loads(process.stdout)
        self.assertEqual(set(report), {"schema_version", "results", "summary"})
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(
            set(report["summary"]),
            {
                "status",
                "total",
                "completed",
                "passed",
                "failed",
                "skipped",
                "error",
                "pass_rate",
                "pass_rate_interval",
            },
        )
        for record in report["results"]:
            self.assertEqual(set(record), {"seed", "status", "metrics", "note"})
        return report

    def assert_usage_error(self, process, message):
        self.assertEqual(process.returncode, 2, process.stderr)
        self.assertEqual(process.stdout, "")
        self.assertIn("usage:", process.stderr)
        self.assertIn(message, process.stderr)
        self.assertNotIn("Traceback", process.stderr)

    def test_no_hook_reports_skips_and_preserves_distinct_signed_seed_order(self):
        process = self.run_cli("--seeds", "-7", "0", "-7", "8")
        report = self.parse_report(process, 2)
        self.assertEqual(
            report["results"],
            [
                {
                    "seed": seed,
                    "status": "skipped",
                    "metrics": {},
                    "note": "No simulation hook configured.",
                }
                for seed in (-7, 0, 8)
            ],
        )
        self.assertEqual(
            report["summary"],
            {
                "status": "no_data",
                "total": 3,
                "completed": 0,
                "passed": 0,
                "failed": 0,
                "skipped": 3,
                "error": 0,
                "pass_rate": None,
                "pass_rate_interval": {
                    "method": "wilson",
                    "confidence": 0.95,
                    "lower": None,
                    "upper": None,
                },
            },
        )

    def test_imported_seed_outcome_works_when_runner_executes_as_main(self):
        process = self.run_cli(
            "--seeds", "-9", "0", "--hook", "apsis_cli_adapter:outcome"
        )
        report = self.parse_report(process, 0)
        self.assertEqual(
            report["results"],
            [
                {
                    "seed": seed,
                    "status": "passed",
                    "metrics": {"seed": seed, "score": 1.25},
                    "note": "accepted",
                }
                for seed in (-9, 0)
            ],
        )
        self.assertEqual(report["summary"]["status"], "passed")
        self.assertEqual(report["summary"]["completed"], 2)
        self.assertEqual(report["summary"]["pass_rate"], 1.0)

    def test_exit_codes_and_summary_precedence_for_real_adapter_outcomes(self):
        cases = [
            (["10"], 0, "passed", (1, 0, 0, 0)),
            (["20"], 1, "failed", (0, 1, 0, 0)),
            (["30"], 2, "no_data", (0, 0, 1, 0)),
            (["40"], 1, "failed", (0, 0, 0, 1)),
            (["10", "30"], 2, "incomplete", (1, 0, 1, 0)),
            (["20", "30"], 1, "failed", (0, 1, 1, 0)),
            (["40", "30"], 1, "failed", (0, 0, 1, 1)),
        ]
        for seeds, exit_code, status, counts in cases:
            with self.subTest(seeds=seeds):
                process = self.run_cli(
                    "--seeds", *seeds, "--hook", "apsis_cli_adapter:outcome"
                )
                report = self.parse_report(process, exit_code)
                summary = report["summary"]
                self.assertEqual(summary["status"], status)
                self.assertEqual(summary["total"], len(seeds))
                self.assertEqual(summary["completed"], counts[0] + counts[1])
                self.assertEqual(
                    tuple(
                        summary[key] for key in ("passed", "failed", "skipped", "error")
                    ),
                    counts,
                )
                expected_rate = (
                    counts[0] / (counts[0] + counts[1])
                    if counts[0] + counts[1]
                    else None
                )
                self.assertEqual(summary["pass_rate"], expected_rate)
                if expected_rate is None:
                    self.assertIsNone(summary["pass_rate_interval"]["lower"])
                    self.assertIsNone(summary["pass_rate_interval"]["upper"])

    def test_mixed_report_excludes_errors_and_skips_from_confidence_interval(self):
        process = self.run_cli(
            "--seeds",
            "10",
            "40",
            "30",
            "20",
            "10",
            "--hook",
            "apsis_cli_adapter:outcome",
            "--confidence",
            "0.95",
        )
        report = self.parse_report(process, 1)
        self.assertEqual([row["seed"] for row in report["results"]], [10, 40, 30, 20])
        self.assertEqual(
            [row["status"] for row in report["results"]],
            ["passed", "error", "skipped", "failed"],
        )
        summary = report["summary"]
        self.assertEqual(summary["total"], 4)
        self.assertEqual(summary["completed"], 2)
        self.assertEqual(summary["pass_rate"], 0.5)
        interval = summary["pass_rate_interval"]
        self.assertEqual(interval["method"], "wilson")
        self.assertEqual(interval["confidence"], 0.95)
        self.assertAlmostEqual(interval["lower"], 0.09453120573423074, places=12)
        self.assertAlmostEqual(interval["upper"], 0.9054687942657693, places=12)
        self.assertEqual(
            report["results"][1],
            {
                "seed": 40,
                "status": "error",
                "metrics": {},
                "note": "Hook raised RuntimeError.",
            },
        )
        self.assertNotIn("adapter-detail-must-stay-private", process.stdout)

    def test_nondefault_confidence_reaches_output(self):
        process = self.run_cli(
            "--seeds",
            "10",
            "20",
            "--hook",
            "apsis_cli_adapter:outcome",
            "--confidence",
            "0.8",
        )
        report = self.parse_report(process, 1)
        interval = report["summary"]["pass_rate_interval"]
        self.assertEqual(interval["confidence"], 0.8)
        self.assertAlmostEqual(interval["lower"], 0.1642517200298998, places=12)
        self.assertAlmostEqual(interval["upper"], 0.8357482799701001, places=12)

    def test_invalid_hook_return_becomes_error_and_later_seed_runs(self):
        process = self.run_cli(
            "--seeds", "1", "10", "--hook", "apsis_cli_adapter:invalid_metric"
        )
        report = self.parse_report(process, 1)
        self.assertEqual(
            report["results"][0],
            {
                "seed": 1,
                "status": "error",
                "metrics": {},
                "note": "Hook raised ValueError.",
            },
        )
        self.assertEqual(report["results"][1]["status"], "passed")
        self.assertEqual(report["summary"]["completed"], 1)
        self.assertEqual(report["summary"]["error"], 1)
        self.assertEqual(report["summary"]["pass_rate"], 1.0)
        self.assertNotIn("NaN", process.stdout)

    def test_invalid_seed_arguments_produce_usage_error_without_json(self):
        cases = [
            ([], "--seeds"),
            (["--seeds"], "expected at least one argument"),
            (["--seeds", "1.5"], "invalid int value"),
            (["--seeds", "True"], "invalid int value"),
            (["--seeds", "1", "--unknown"], "unrecognized arguments"),
        ]
        for arguments, message in cases:
            with self.subTest(arguments=arguments):
                self.assert_usage_error(self.run_cli(*arguments), message)

    def test_invalid_confidences_produce_usage_error_without_json(self):
        for confidence in ("0", "1", "-0.1", "1.1", "nan", "inf", "-inf"):
            with self.subTest(confidence=confidence):
                self.assert_usage_error(
                    self.run_cli("--seeds", "1", f"--confidence={confidence}"),
                    "confidence must be a finite number between 0 and 1",
                )
        self.assert_usage_error(
            self.run_cli("--seeds", "1", "--confidence", "invalid"),
            "invalid float value",
        )

    def test_underscore_confidence_tokens_are_usage_errors(self):
        for confidence in ("0_5e-1", "0.5_0", "5e-0_1"):
            for option in (
                ("--confidence", confidence),
                (f"--confidence={confidence}",),
            ):
                with self.subTest(confidence=confidence, option=option):
                    self.assert_usage_error(
                        self.run_cli(
                            "--seeds",
                            "10",
                            "--hook",
                            "apsis_cli_adapter:outcome",
                            *option,
                        ),
                        "invalid float value",
                    )

    def test_help_routes_to_usage_without_running_the_adapter_or_emitting_json(self):
        adapter_arguments = (
            "--seeds",
            "10",
            "--hook",
            "apsis_cli_noisy_adapter:outcome",
        )
        for arguments in (
            ("--help",),
            ("-h",),
            ("--help", *adapter_arguments),
            (*adapter_arguments, "--help"),
            (*adapter_arguments, "-h"),
        ):
            with self.subTest(arguments=arguments):
                process = self.run_cli(*arguments)
                self.assertEqual(process.returncode, 0, process.stderr)
                self.assertEqual(process.stderr, "")
                self.assertTrue(process.stdout.startswith("usage:"), process.stdout)
                for text in (
                    "--seeds",
                    "--confidence",
                    "--hook",
                    "show this help message and exit",
                ):
                    self.assertIn(text, process.stdout)
                self.assertNotIn('"schema_version"', process.stdout)
                self.assertNotIn("adapter-import-progress", process.stdout)
                self.assertNotIn("adapter-hook-progress", process.stdout)
                with self.assertRaises(json.JSONDecodeError):
                    json.loads(process.stdout)

    def test_malformed_hook_specs_produce_usage_error_without_json(self):
        for hook in (
            "",
            "apsis_cli_adapter",
            ":outcome",
            "apsis_cli_adapter:",
            "a:b:c",
        ):
            with self.subTest(hook=hook):
                self.assert_usage_error(
                    self.run_cli("--seeds", "1", "--hook", hook),
                    "hook must have the form module:function",
                )

    def test_surrounding_whitespace_in_confidence_is_a_usage_error(self):
        for confidence in (" 0.5 ", "0.5 ", "\t0.5\n"):
            with self.subTest(confidence=confidence):
                self.assert_usage_error(
                    self.run_cli(
                        "--seeds",
                        "10",
                        "--hook",
                        "apsis_cli_adapter:outcome",
                        "--confidence",
                        confidence,
                    ),
                    "invalid float value",
                )

    def test_unimportable_missing_and_noncallable_hooks_report_usage_errors(self):
        cases = [
            ("apsis_cli_missing_adapter:outcome", "ModuleNotFoundError"),
            ("apsis_cli_adapter:missing", "AttributeError"),
            ("apsis_cli_adapter:not_callable", "hook must be callable"),
        ]
        for hook, message in cases:
            with self.subTest(hook=hook):
                self.assert_usage_error(
                    self.run_cli("--seeds", "1", "--hook", hook), message
                )

    def test_hook_exit_zero_is_not_a_successful_cli_run(self):
        process = self.run_cli("--seeds", "1", "--hook", "apsis_cli_adapter:exit_zero")
        self.assertEqual(process.returncode, 2, process.stderr)
        self.assertEqual(process.stdout, "")
        self.assertIn("SystemExit", process.stderr)
        self.assertNotIn("Traceback", process.stderr)

    def test_import_syntax_and_runtime_errors_are_usage_errors_without_json(self):
        cases = (
            ("apsis_cli_syntax_adapter:outcome", "SyntaxError"),
            ("apsis_cli_runtime_adapter:outcome", "RuntimeError"),
        )
        for hook, message in cases:
            with self.subTest(hook=hook):
                self.assert_usage_error(
                    self.run_cli("--seeds", "1", "--hook", hook), message
                )

    def test_import_error_diagnostics_omit_exception_messages(self):
        cases = (
            ("apsis_cli_syntax_adapter:outcome", "SyntaxError", "invalid syntax"),
            (
                "apsis_cli_runtime_adapter:outcome",
                "RuntimeError",
                "broken adapter import",
            ),
        )
        for hook, exception_type, private_message in cases:
            with self.subTest(hook=hook):
                process = self.run_cli("--seeds", "1", "--hook", hook)
                self.assert_usage_error(process, f"hook raised {exception_type}.")
                self.assertNotIn(private_message, process.stderr)

    def test_import_and_hook_stdout_preserve_the_json_document(self):
        process = self.run_cli(
            "--seeds", "1", "2", "--hook", "apsis_cli_noisy_adapter:outcome"
        )
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["summary"]["status"], "passed")
        self.assertEqual([record["seed"] for record in report["results"]], [1, 2])
        self.assertEqual(
            [record["status"] for record in report["results"]], ["passed", "passed"]
        )
        for marker in ("adapter-import-progress", "adapter-hook-progress"):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, process.stdout)

    def test_json_object_keys_are_sorted_at_every_level(self):
        process = self.run_cli("--seeds", "10", "--hook", "apsis_cli_adapter:outcome")
        report = self.parse_report(process, 0)

        def assert_sorted_keys(pairs):
            keys = [key for key, value in pairs]
            self.assertEqual(keys, sorted(keys))
            return dict(pairs)

        self.assertEqual(
            json.loads(process.stdout, object_pairs_hook=assert_sorted_keys), report
        )

    def test_huge_integer_metric_reports_serialization_error_without_traceback(self):
        process = self.run_cli(
            "--seeds", "1", "--hook", "apsis_cli_adapter:huge_int_metric"
        )
        self.assertEqual(process.returncode, 2, process.stderr)
        self.assertEqual(process.stdout, "")
        self.assertIn("cannot serialize report", process.stderr)
        self.assertNotIn("Traceback", process.stderr)

    def test_whitespace_and_underscores_in_seed_tokens_are_usage_errors(self):
        for seed in (" 7 ", "1_000"):
            with self.subTest(seed=seed):
                self.assert_usage_error(
                    self.run_cli("--seeds", seed), "invalid int value"
                )


if __name__ == "__main__":
    unittest.main()
