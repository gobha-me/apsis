"""Keep adapter process-control exceptions from becoming CLI false passes."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
PRIVATE_DETAIL = "adapter-control-detail-must-stay-private"


class SeedRunnerControlCliTests(unittest.TestCase):
    def run_control_adapter(self, statement, *, during_import=False):
        source = "import sys\nfrom tools.seed_runner import SeedOutcome\n\n"
        if during_import:
            source += f"{statement}\n\n"
            source += 'def outcome(seed):\n    return SeedOutcome("passed")\n'
        else:
            source += f"def outcome(seed):\n    {statement}\n"

        with tempfile.TemporaryDirectory(prefix="apsis-control-cli-") as directory:
            adapter = Path(directory) / "apsis_control_adapter.py"
            adapter.write_text(source, encoding="utf-8")
            environment = os.environ.copy()
            python_paths = [directory, str(REPOSITORY)]
            if environment.get("PYTHONPATH"):
                python_paths.append(environment["PYTHONPATH"])
            environment["PYTHONPATH"] = os.pathsep.join(python_paths)
            environment["PYTHONDONTWRITEBYTECODE"] = "1"
            return subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.seed_runner",
                    "--seeds",
                    "1",
                    "--hook",
                    "apsis_control_adapter:outcome",
                ],
                cwd=REPOSITORY,
                env=environment,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=10,
                check=False,
            )

    def assert_control_error(self, process, exception_type):
        self.assertEqual(process.returncode, 2, process.stderr)
        self.assertEqual(process.stdout, "")
        self.assertIn(exception_type, process.stderr)
        self.assertNotIn("Traceback", process.stderr)
        self.assertNotIn(PRIVATE_DETAIL, process.stderr)

    def test_exit_during_hook_import_is_not_a_successful_cli_run(self):
        for exit_code in (0, 1, None):
            with self.subTest(exit_code=exit_code):
                process = self.run_control_adapter(
                    f"sys.exit({exit_code!r})", during_import=True
                )
                self.assert_control_error(process, "SystemExit")

    def test_keyboard_interrupt_during_hook_import_has_no_json_or_traceback(self):
        process = self.run_control_adapter(
            f"raise KeyboardInterrupt({PRIVATE_DETAIL!r})", during_import=True
        )
        self.assert_control_error(process, "KeyboardInterrupt")

    def test_hook_system_exit_is_not_a_successful_cli_run(self):
        for exit_code in (1, None):
            with self.subTest(exit_code=exit_code):
                process = self.run_control_adapter(f"sys.exit({exit_code!r})")
                self.assert_control_error(process, "SystemExit")

    def test_system_exit_messages_stay_private_during_import_and_hook_execution(self):
        for during_import in (True, False):
            with self.subTest(during_import=during_import):
                process = self.run_control_adapter(
                    f"sys.exit({PRIVATE_DETAIL!r})", during_import=during_import
                )
                self.assert_control_error(process, "SystemExit")
                self.assertNotIn(PRIVATE_DETAIL, process.stderr)

    def test_hook_keyboard_interrupt_has_no_json_or_traceback(self):
        process = self.run_control_adapter(
            f"raise KeyboardInterrupt({PRIVATE_DETAIL!r})"
        )
        self.assert_control_error(process, "KeyboardInterrupt")

    def test_hook_base_exceptions_have_no_json_or_traceback(self):
        for exception_type in ("BaseException", "GeneratorExit"):
            with self.subTest(exception_type=exception_type):
                process = self.run_control_adapter(
                    f"raise {exception_type}({PRIVATE_DETAIL!r})"
                )
                self.assert_control_error(process, exception_type)


if __name__ == "__main__":
    unittest.main()
