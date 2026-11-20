"""Exercise CLI output isolation and failed output descriptors with real adapters."""

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
import os
import sys

from tools.seed_runner import SeedOutcome

def outcome(seed):
    return SeedOutcome("passed", {"seed": seed})

def raw_outcome(seed):
    os.write(1, f"adapter-hook-fd1-{seed}\\n".encode())
    return outcome(seed)

def buffered_outcome(seed):
    print(f"adapter-hook-original-stdout-{seed}", file=sys.__stdout__)
    return outcome(seed)

def exit_hook(seed):
    sys.exit(None)
"""


class SeedRunnerOutputCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adapter_directory = tempfile.TemporaryDirectory(prefix="apsis-output-cli-")
        cls.addClassCleanup(cls.adapter_directory.cleanup)
        adapters = {
            "apsis_output_cli_adapter": textwrap.dedent(ADAPTER_SOURCE),
            "apsis_output_cli_raw_import": (
                "import os\n"
                'os.write(1, b"adapter-import-fd1\\n")\n'
                "from apsis_output_cli_adapter import outcome, raw_outcome\n"
            ),
            "apsis_output_cli_buffered_import": (
                "import sys\n"
                'print("adapter-import-original-stdout", file=sys.__stdout__)\n'
                "from apsis_output_cli_adapter import buffered_outcome as outcome\n"
            ),
            "apsis_output_cli_libc_adapter": textwrap.dedent("""\
                import ctypes
                from apsis_output_cli_adapter import outcome

                libc = ctypes.CDLL(None)
                libc.printf.argtypes = [ctypes.c_char_p]
                libc.printf.restype = ctypes.c_int

                def c_outcome(seed):
                    message = f"adapter-hook-libc-{seed}".encode()
                    if libc.printf(message) != len(message):
                        raise RuntimeError("libc printf failed")
                    return outcome(seed)
                """),
            "apsis_output_cli_libc_import": (
                "from apsis_output_cli_libc_adapter import libc, outcome, c_outcome\n"
                'message = b"adapter-import-libc"\n'
                "if libc.printf(message) != len(message):\n"
                '    raise RuntimeError("libc printf failed")\n'
            ),
        }
        for module, source in adapters.items():
            path = Path(cls.adapter_directory.name) / f"{module}.py"
            path.write_text(source, encoding="utf-8")
        cls.environment = os.environ.copy()
        python_paths = [cls.adapter_directory.name, str(REPOSITORY)]
        if cls.environment.get("PYTHONPATH"):
            python_paths.append(cls.environment["PYTHONPATH"])
        cls.environment["PYTHONPATH"] = os.pathsep.join(python_paths)
        cls.environment["PYTHONDONTWRITEBYTECODE"] = "1"

    def run_cli(
        self,
        *arguments,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=None,
    ):
        return subprocess.run(
            [sys.executable, "-m", "tools.seed_runner", *arguments],
            cwd=REPOSITORY,
            env=self.environment,
            stdout=stdout,
            stderr=stderr,
            preexec_fn=preexec_fn,
            text=True,
            encoding="utf-8",
            timeout=10,
            check=False,
        )

    def assert_passed_report(self, process):
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(process.stdout)
        self.assertEqual(report["summary"]["status"], "passed")
        self.assertEqual(report["summary"]["completed"], 2)
        self.assertEqual(
            report["results"],
            [
                {
                    "seed": seed,
                    "status": "passed",
                    "metrics": {"seed": seed},
                    "note": None,
                }
                for seed in (1, 2)
            ],
        )
        self.assertNotIn("Traceback", process.stderr)

    def test_raw_fd1_writes_during_import_and_hooks_preserve_stdout_json(self):
        cases = (
            (
                "apsis_output_cli_raw_import:outcome",
                ["adapter-import-fd1"],
            ),
            (
                "apsis_output_cli_adapter:raw_outcome",
                ["adapter-hook-fd1-1", "adapter-hook-fd1-2"],
            ),
            (
                "apsis_output_cli_raw_import:raw_outcome",
                ["adapter-import-fd1", "adapter-hook-fd1-1", "adapter-hook-fd1-2"],
            ),
        )
        for hook, markers in cases:
            with self.subTest(hook=hook):
                process = self.run_cli("--seeds", "1", "2", "--hook", hook)
                self.assert_passed_report(process)
                for marker in markers:
                    self.assertNotIn(marker, process.stdout)
                    self.assertIn(marker, process.stderr)

    def test_buffered_original_stdout_is_drained_before_json_is_written(self):
        process = self.run_cli(
            "--seeds", "1", "2", "--hook", "apsis_output_cli_buffered_import:outcome"
        )
        self.assert_passed_report(process)
        for marker in (
            "adapter-import-original-stdout",
            "adapter-hook-original-stdout-1",
            "adapter-hook-original-stdout-2",
        ):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, process.stdout)
                self.assertIn(marker, process.stderr)

    def test_buffered_libc_import_and_hook_output_cannot_corrupt_stdout_json(self):
        # Python's unbuffered mode also disables C stdout buffering at startup.
        self.environment = self.environment.copy()
        self.environment.pop("PYTHONUNBUFFERED", None)
        cases = (
            ("apsis_output_cli_libc_import:outcome", ["adapter-import-libc"]),
            (
                "apsis_output_cli_libc_adapter:c_outcome",
                ["adapter-hook-libc-1", "adapter-hook-libc-2"],
            ),
            (
                "apsis_output_cli_libc_import:c_outcome",
                ["adapter-import-libc", "adapter-hook-libc-1", "adapter-hook-libc-2"],
            ),
        )
        # No newline or explicit flush: these short printf writes stay in C stdio
        # until the runner flushes libc while fd 1 still points at stderr.
        for hook, markers in cases:
            with self.subTest(hook=hook):
                process = self.run_cli("--seeds", "1", "2", "--hook", hook)
                self.assert_passed_report(process)
                self.assertEqual(process.stderr, "".join(markers))
                for marker in markers:
                    self.assertNotIn(marker, process.stdout)

    def test_broken_report_pipe_exits_two_without_traceback(self):
        reader, writer = os.pipe()
        os.close(reader)
        try:
            # No process owns a read end before the CLI starts writing its report.
            process = self.run_cli(
                "--seeds",
                "1",
                "--hook",
                "apsis_output_cli_adapter:outcome",
                stdout=writer,
            )
        finally:
            os.close(writer)
        self.assertEqual(process.returncode, 2, process.stderr)
        self.assertNotIn("Traceback", process.stderr)
        self.assertNotIn("Exception ignored", process.stderr)

    def test_broken_diagnostic_pipe_exits_two_without_stdout_or_shutdown_retry(self):
        cases = (
            ("--seeds", "invalid"),
            ("--seeds", "1", "--hook", "apsis_output_cli_adapter:exit_hook"),
        )
        for arguments in cases:
            with self.subTest(arguments=arguments):
                reader, writer = os.pipe()
                os.close(reader)
                try:
                    process = self.run_cli(*arguments, stderr=writer)
                finally:
                    os.close(writer)
                # Failed stderr cannot be read; stdout must remain empty and the
                # process must retain exit 2 rather than a shutdown-flush exit.
                self.assertEqual(process.returncode, 2)
                self.assertEqual(process.stdout, "")

    def test_closed_standard_output_descriptors_exit_two_without_traceback(self):
        for descriptor in (1, 2):
            with self.subTest(descriptor=descriptor):
                # Close only the child's descriptor after subprocess installs its
                # capture pipes and before the real Python CLI starts.
                process = self.run_cli(
                    "--seeds",
                    "1",
                    "--hook",
                    "apsis_output_cli_adapter:outcome",
                    preexec_fn=lambda: os.close(descriptor),
                )
                self.assertEqual(process.returncode, 2, process.stderr)
                self.assertEqual(process.stdout, "")
                self.assertNotIn("Traceback", process.stderr)
                self.assertNotIn("Exception ignored", process.stderr)


if __name__ == "__main__":
    unittest.main()
