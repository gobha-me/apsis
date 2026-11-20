"""Exercise fresh-evidence cleanup through the real determinism CLI."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_REPORTS = (
    "gates_l1.json",
    "world_core_l2.json",
    "pilot_gates_l3.json",
    "pilot_switches_l4.json",
    "pilot_endstates_l4.json",
    "pilot_law_l4.json",
)
ARTIFACT_REPORTS = NATIVE_REPORTS + (
    "world_l2.json",
    "world-run.json",
    "world.log",
    "report.md",
    "summary.json",
)


class DeterminismPrepareTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="determinism-prepare-tests-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def run_prepare(self, report_dir):
        environment = os.environ.copy()
        environment.pop("GITHUB_STEP_SUMMARY", None)
        return subprocess.run(
            [
                sys.executable,
                str(REPO_ROOT / "tools" / "determinism.py"),
                "prepare",
                "--report-dir",
                str(report_dir),
            ],
            cwd=self.root,
            env=environment,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

    def test_prepare_removes_stale_evidence_and_preserves_unrelated_files(self):
        report_dir = self.root / "reports with spaces"
        artifact_dir = report_dir / "apsis-determinism"
        artifact_dir.mkdir(parents=True)
        for directory, names in (
            (report_dir, NATIVE_REPORTS),
            (artifact_dir, ARTIFACT_REPORTS),
        ):
            for name in names:
                (directory / name).write_text("stale evidence", encoding="utf-8")
            (directory / "keep.json").write_text("keep me", encoding="utf-8")
        nested = artifact_dir / "unrelated directory"
        nested.mkdir()
        (nested / "world_l2.json").write_text("keep nested", encoding="utf-8")

        result = self.run_prepare(report_dir)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        for directory, names in (
            (report_dir, NATIVE_REPORTS),
            (artifact_dir, ARTIFACT_REPORTS),
        ):
            for name in names:
                self.assertFalse((directory / name).exists(), directory / name)
            self.assertEqual(
                (directory / "keep.json").read_text(encoding="utf-8"), "keep me"
            )
        self.assertEqual(
            (nested / "world_l2.json").read_text(encoding="utf-8"), "keep nested"
        )

    def test_prepare_creates_missing_directories_and_is_idempotent(self):
        report_dir = self.root / "new" / "reports"
        for attempt in range(2):
            with self.subTest(attempt=attempt):
                result = self.run_prepare(report_dir)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertEqual(list((report_dir / "apsis-determinism").iterdir()), [])

    def test_prepare_reports_a_file_in_place_of_the_output_directory(self):
        report_dir = self.root / "not a directory"
        report_dir.write_text("preserve", encoding="utf-8")

        result = self.run_prepare(report_dir)

        self.assertEqual(result.returncode, 1)
        self.assertIn("determinism tooling failed:", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(report_dir.read_text(encoding="utf-8"), "preserve")


if __name__ == "__main__":
    unittest.main()
