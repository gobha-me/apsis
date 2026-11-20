"""Run the real browser viewer module with deterministic DOM and frame I/O."""

import shutil
import subprocess
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
NODE = shutil.which("node")


@unittest.skipUnless(NODE, "Node is required to import the real viewer module")
class ViewerPageTests(unittest.TestCase):
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

    def test_loader_rejects_tampered_final_checkpoint_hash(self):
        result = self.run_viewer("--hash-mismatch")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_rejects_edited_recorded_frames(self):
        result = self.run_viewer("--frame-mismatch")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_rejects_one_flipped_checkpoint_byte_with_original_hashes(self):
        result = self.run_viewer("--checkpoint-byte-flip")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_checks_timeline_hash_and_complete_run_record(self):
        result = self.run_viewer("--timeline-integrity")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_loader_rejects_ill_typed_timeline_with_matching_hash(self):
        result = self.run_viewer("--timeline-types")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
