"""Recipe recovery and evidence comparisons; native replays are explicit opt-ins."""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "viewer" / "replay_roundtrip.py"
SPEC = importlib.util.spec_from_file_location("viewer_roundtrip", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
RECORDED = ROOT / "tests" / "fixtures" / "viewer" / "recorded-run.json"
EXPORTER = os.environ.get("APSIS_VIEWER_EXPORT")


def recording():
    return json.loads(RECORDED.read_text(encoding="utf-8"))


class ReplayRecipeTests(unittest.TestCase):
    def test_recover_existing_complete_recipe_without_duplicate_start(self):
        source = MODULE.recipe(recording())
        self.assertEqual(
            source,
            "333860 4 9000 720 0\n"
            "9001 6 1 -1 0 1 0 0\n"
            "9180 6 2 -1 0 1 0 0\n"
            "9360 6 3 -1 0 1 0 0\n"
            "9540 6 0 -1 0 1 0 0\n",
        )

    def test_reject_out_of_bounds_inputs_extra_start_and_noncanonical_order(self):
        mutations = [
            lambda run: run.update(seed=True),
            lambda run: run.update(seed=2**32),
            lambda run: run.update(majors=6),
            lambda run: run.update(horizon=0),
            lambda run: run.update(station=-1),
            lambda run: run.update(inputs=[]),
            lambda run: run["inputs"][0].update(kind=1),
            lambda run: run["inputs"][0].update(day=True),
            lambda run: run["inputs"][1].update(kind=0),
            lambda run: run["inputs"][1].update(day=run["startDay"] - 1),
            lambda run: run["inputs"][1].update(day=run["startDay"] + run["horizon"]),
            lambda run: run["inputs"][1].update(fields=["0"]),
            lambda run: run["inputs"][1]["fields"].__setitem__(
                0, "9223372036854775808"
            ),
            lambda run: run["inputs"][1]["fields"].__setitem__(0, "01"),
            lambda run: run["inputs"][1].update(unexpected="value"),
            lambda run: run["inputs"].__setitem__(slice(1, None), run["inputs"][:0:-1]),
        ]
        for mutate in mutations:
            artifact = recording()
            mutate(artifact["run"])
            with (
                self.subTest(run=artifact["run"]["inputs"][:2]),
                self.assertRaises(ValueError),
            ):
                MODULE.recipe(artifact)

    def test_reject_duplicate_keys_and_nonfinite_json(self):
        for source in ['{"run":1,"run":2}', '{"amount":NaN}', '{"amount":Infinity}']:
            with self.subTest(source=source), self.assertRaises(ValueError):
                MODULE.decode(source)

    def test_comparison_checks_every_evidence_category_and_ignores_only_metadata(self):
        artifact = recording()
        replayed = recording()
        replayed["metadata"]["adapterRevision"] = "new tool revision"
        result = subprocess.CompletedProcess(
            [sys.executable], 0, json.dumps(replayed), ""
        )
        with (
            patch.object(MODULE.subprocess, "run", return_value=result) as launch,
            patch.dict(os.environ, {"APSIS_SET": "stockTank=3"}),
        ):
            recovered, summary = MODULE.roundtrip(artifact, sys.executable)
        self.assertEqual(recovered, MODULE.recipe(artifact))
        self.assertEqual(summary["matched"], list(MODULE.EVIDENCE))
        self.assertFalse(summary["metadataCompared"])
        self.assertNotIn("APSIS_SET", launch.call_args.kwargs["env"])
        for key in MODULE.EVIDENCE:
            changed = recording()
            changed[key] = None
            result.stdout = json.dumps(changed)
            with (
                self.subTest(key=key),
                patch.object(MODULE.subprocess, "run", return_value=result),
                self.assertRaisesRegex(ValueError, key),
            ):
                MODULE.roundtrip(artifact, sys.executable)

    def test_exporter_failure_never_reports_success(self):
        result = subprocess.CompletedProcess([sys.executable], 7, "", "adapter failed")
        with (
            patch.object(MODULE.subprocess, "run", return_value=result),
            self.assertRaisesRegex(ValueError, "exporter exited 7: adapter failed"),
        ):
            MODULE.roundtrip(recording(), sys.executable)

    def test_cli_rejects_malformed_recording_without_stdout(self):
        with tempfile.TemporaryDirectory(prefix="apsis-roundtrip-test-") as directory:
            path = Path(directory) / "bad.json"
            path.write_text('{"schema":"wrong"}', encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(path), "--exporter", sys.executable],
                text=True,
                capture_output=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("expected an apsis-viewer-v1 export", result.stderr)

    def test_boolean_frame_values_cannot_match_integer_fields(self):
        artifact = recording()
        changed = recording()
        changed["frames"][0]["pilot"]["alive"] = 0
        result = subprocess.CompletedProcess(
            [sys.executable], 0, json.dumps(changed), ""
        )
        with (
            patch.object(MODULE.subprocess, "run", return_value=result),
            self.assertRaisesRegex(ValueError, "frames"),
        ):
            MODULE.roundtrip(artifact, sys.executable)


@unittest.skipUnless(EXPORTER, "set APSIS_VIEWER_EXPORT for real replay round trips")
class NativeReplayRoundtripTests(unittest.TestCase):
    def test_recorded_export_replays_with_equal_settings_frames_timeline_and_checkpoint(
        self,
    ):
        source, summary = MODULE.roundtrip(recording(), EXPORTER)
        self.assertEqual(summary["finalStateHash"], "2e41d7a6ad443e92")
        self.assertEqual(summary["frames"], 721)
        self.assertEqual(len(source.splitlines()), 5)

    def test_same_day_input_order_and_int64_fields_survive_real_roundtrip(self):
        source = (
            "333860 4 0 4 0\n2 9 0 0 0 0 0 9223372036854775807\n"
            "1 7 0 0 0 0 0 -9223372036854775808\n"
            "1 9 0 0 0 0 0 0\n"
        )
        exported = subprocess.run(
            [EXPORTER],
            input=source,
            text=True,
            capture_output=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(exported.returncode, 0, exported.stderr)
        artifact = json.loads(exported.stdout)
        recovered, _ = MODULE.roundtrip(artifact, EXPORTER)
        self.assertIn("-9223372036854775808", recovered)
        self.assertIn("9223372036854775807", recovered)
        self.assertEqual(
            [row.split()[:2] for row in recovered.splitlines()[1:]],
            [["1", "7"], ["1", "9"], ["2", "9"]],
        )

    def test_changed_recorded_settings_and_frames_fail_real_roundtrip(self):
        for category in ("run", "frames"):
            artifact = recording()
            if category == "run":
                artifact["run"]["settings"]["core"]["stockTank"] = "3"
            else:
                artifact["frames"][1]["pilot"]["wallet"] = "999"
            with (
                self.subTest(category=category),
                self.assertRaisesRegex(ValueError, category),
            ):
                MODULE.roundtrip(artifact, EXPORTER)

    def test_cli_writes_recipe_only_after_success_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory(prefix="apsis-roundtrip-test-") as directory:
            path = Path(directory) / "run.txt"
            command = [
                sys.executable,
                str(SCRIPT),
                str(RECORDED),
                "--exporter",
                EXPORTER,
                "--recipe-output",
                str(path),
            ]
            result = subprocess.run(
                command, text=True, capture_output=True, timeout=60, check=False
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["frames"], 721)
            self.assertEqual(
                path.read_text(encoding="utf-8"), MODULE.recipe(recording())
            )
            again = subprocess.run(
                command, text=True, capture_output=True, timeout=60, check=False
            )
            self.assertEqual(again.returncode, 1)
            self.assertEqual(again.stdout, "")
            self.assertEqual(
                path.read_text(encoding="utf-8"), MODULE.recipe(recording())
            )


if __name__ == "__main__":
    unittest.main()
