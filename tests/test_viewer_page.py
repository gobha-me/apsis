"""Run the real browser viewer module with deterministic DOM and frame I/O."""

import shutil
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "Node is required to import the real viewer module")
class ViewerPageTests(unittest.TestCase):
    def setUp(self):
        if self._testMethodName == "test_loader_rejects_tampered_final_checkpoint_hash":
            self.hash_result = self.run_viewer("--hash-mismatch")
            if self.hash_result.returncode:
                # setUp failures do not become expected failures: only the
                # verified acceptance of a tampered hash may get that status.
                self.assertIn(
                    "APSIS_HASH_INTEGRITY_DEFECT: tampered hash accepted",
                    self.hash_result.stdout,
                    self.hash_result.stdout + self.hash_result.stderr,
                )

    def run_viewer(self, *arguments):
        return subprocess.run(
            [NODE, str(REPO_ROOT / "tests" / "viewer_page.test.mjs"), *arguments],
            capture_output=True,
            encoding="utf-8",
            cwd=REPO_ROOT,
            timeout=30,
            check=False,
        )

    def test_loader_observations_reload_and_playback(self):
        result = self.run_viewer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.expectedFailure
    def test_loader_rejects_tampered_final_checkpoint_hash(self):
        # Known production gap: validate() checks only typeof finalStateHash.
        # Keep the required integrity assertion explicit until the loader can
        # authenticate a full checkpoint; this tests lane cannot change source.
        result = self.hash_result
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
