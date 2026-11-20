"""Recorded export contracts and optional bounded tests of the real native adapter.

Native cases use APSIS_VIEWER_EXPORT and APSIS_VIEWER_OBSERVATIONS executables.
The latter includes the real adapter and obtains observations from the real sim.
No test compiles or starts a simulation campaign during unittest discovery.
"""

import itertools
import json
import os
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORDED = ROOT / "tests" / "fixtures" / "viewer" / "recorded-run.json"
EXPORTER = os.environ.get("APSIS_VIEWER_EXPORT")
OBSERVATIONS = os.environ.get("APSIS_VIEWER_OBSERVATIONS")
RECIPE = "333860 4 0 4 0\n2 9 0 0 0 0 0 0\n1 7 0 0 0 0 0 0\n"


def observed_timeline(frames):
    """Expected derived observations from raw sim snapshots, plus actual events."""
    expected = []
    event_count = 0
    for before, after in itertools.pairwise(frames):
        active_before = before["hostilePairs"] * 2 >= before["pairs"] >= 2
        active_after = after["hostilePairs"] * 2 >= after["pairs"] >= 2
        if active_after != active_before:
            expected.append(
                {
                    "day": after["day"],
                    "type": "general-war",
                    "source": "derived",
                    "active": active_after,
                    "hostilePairs": after["hostilePairs"],
                    "pairs": after["pairs"],
                }
            )
        delta = int(after["strandings"]) - int(before["strandings"])
        if delta:
            expected.append(
                {
                    "day": after["day"],
                    "type": "stranding",
                    "source": "derived",
                    "delta": str(delta),
                    "total": after["strandings"],
                }
            )
        for day, kind, argument in after["events"][event_count:]:
            expected.append(
                {
                    "day": day,
                    "type": "pilot-event",
                    "source": "Pilot.ev",
                    "kind": kind,
                    "arg": argument,
                }
            )
        event_count = len(after["events"])
    return expected


class RecordedViewerExportTests(unittest.TestCase):
    def test_recorded_run_retains_complete_recipe_and_known_full_checkpoint_hash(self):
        artifact = json.loads(RECORDED.read_text(encoding="utf-8"))
        self.assertEqual(artifact["schema"], "apsis-viewer-v1")
        self.assertEqual(artifact["finalStateHash"], "2e41d7a6ad443e92")
        self.assertEqual(artifact["framesHash"], "1b7f7ef382e6bf47")
        self.assertEqual(artifact["timelineHash"], "fb93e5f4223ddcee")
        self.assertRegex(artifact["finalCheckpoint"], r"^(?:[0-9a-f]{2})+$")
        run = artifact["run"]
        self.assertEqual(
            [run[key] for key in ("seed", "majors", "startDay", "horizon", "station")],
            [333860, 4, 9000, 720, 0],
        )
        self.assertEqual(set(run["settings"]), {"rules", "core", "world"})
        self.assertTrue(run["settings"]["rules"])
        self.assertTrue(run["settings"]["core"])
        self.assertTrue(run["settings"]["world"])
        self.assertEqual(
            run["inputs"][0], {"day": 9000, "kind": 0, "fields": ["0"] * 6}
        )
        self.assertEqual(
            run["inputs"][1:],
            [
                {
                    "day": day,
                    "kind": 6,
                    "fields": [str(destination), "-1", "0", "1", "0", "0"],
                }
                for day, destination in ((9001, 1), (9180, 2), (9360, 3), (9540, 0))
            ],
        )
        self.assertEqual(
            [frame["day"] for frame in artifact["frames"]], list(range(9000, 9721))
        )

    def test_recorded_empty_observations_do_not_invent_transitions_or_rescues(self):
        artifact = json.loads(RECORDED.read_text(encoding="utf-8"))
        self.assertEqual(artifact["timeline"], [])
        previous = artifact["frames"][0]
        for frame in artifact["frames"]:
            war = frame["war"]
            self.assertEqual(
                war["active"], war["hostilePairs"] * 2 >= war["pairs"] >= 2
            )
            self.assertEqual(war["active"], previous["war"]["active"])
            self.assertEqual(frame["pilot"]["strandings"], "0")
            previous = frame


@unittest.skipUnless(
    EXPORTER, "set APSIS_VIEWER_EXPORT to test the real native exporter"
)
class ViewerExportCliTests(unittest.TestCase):
    def run_export(self, recipe):
        return subprocess.run(
            [EXPORTER],
            input=recipe,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )

    def test_real_export_is_accepted_by_real_viewer_loader(self):
        # CI's later native-adapter step already sets APSIS_VIEWER_EXPORT and
        # runs this file. Missing Node must fail here rather than skip SF2.
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node is required to test the real viewer loader")
        exported = self.run_export(RECIPE)
        self.assertEqual(exported.returncode, 0, exported.stderr)
        viewer = subprocess.run(
            [node, str(ROOT / "tests" / "viewer_page.test.mjs"), "--export-stdin"],
            input=exported.stdout,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=30,
            check=False,
        )
        self.assertEqual(viewer.returncode, 0, viewer.stdout + viewer.stderr)
        self.assertIn(
            "real exporter stdout is accepted by the real viewer loader", viewer.stdout
        )

    def test_missing_malformed_and_out_of_bounds_input_fails_without_partial_json(self):
        invalid = [
            "",
            "# only a comment\n",
            "333860 4 0 1",
            "not a run",
            "333860 4 0 1 0 extra",
            "-1 4 0 1 0",
            "4294967296 4 0 1 0",
            "1 2 0 1 0",
            "1 6 0 1 0",
            "1 4 -1 1 0",
            "1 4 36001 1 0",
            "1 4 0 0 0",
            "1 4 0 3601 0",
            "1 4 0 1 -1",
            "1 4 0 1 0\n0 0 0 0 0 0 0 0",  # no second START
            "1 4 0 1 0\n0 11 0 0 0 0 0 0",
            "1 4 0 1 0\n0 -1 0 0 0 0 0 0",
            "1 4 0 1 0\n-1 6 1 -1 0 1 0 0",
            "1 4 0 1 0\n1 6 1 -1 0 1 0 0",
            "1 4 0 1 0\n0 6 1 -1 0 1 0",
            "1 4 0 1 0\n0 6 1 -1 0 1 0 0 extra",
            "1 4 0 1 0\n0 6 1 -1 0 1 0 bad",
            "1 4 0 1 0\n" + "0 7 0 0 0 0 0 0\n" * 4096,
        ]
        for recipe in invalid:
            with self.subTest(recipe=recipe[:100]):
                result = self.run_export(recipe)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn("Invalid run.", result.stderr)
                self.assertIn("Bounds:", result.stderr)

    def test_absent_generated_station_has_clear_error_and_no_export(self):
        result = self.run_export("333860 4 0 1 2147483647\n")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("Start station is absent", result.stderr)

    def test_two_real_exports_are_byte_identical_and_preserve_canonical_inputs(self):
        first = self.run_export(RECIPE)
        second = self.run_export(RECIPE)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        artifact = json.loads(first.stdout)
        self.assertEqual(
            [frame["day"] for frame in artifact["frames"]], [0, 1, 2, 3, 4]
        )
        self.assertFalse(artifact["frames"][0]["pilot"]["alive"])
        self.assertTrue(artifact["frames"][1]["pilot"]["alive"])
        self.assertEqual(
            [entry["day"] for entry in artifact["run"]["inputs"]], [0, 1, 2]
        )
        self.assertEqual(
            [entry["kind"] for entry in artifact["run"]["inputs"]], [0, 7, 9]
        )
        self.assertRegex(artifact["finalStateHash"], r"^[0-9a-f]{16}$")
        commented = "# recipe\n\n" + RECIPE.replace("\n", " # row\n")
        self.assertEqual(self.run_export(commented).stdout, first.stdout)

    @unittest.skipUnless(
        OBSERVATIONS, "set APSIS_VIEWER_OBSERVATIONS for real sim oracle"
    )
    def test_export_matches_real_sim_counters_events_and_full_checkpoint(self):
        result = self.run_export(RECIPE)
        self.assertEqual(result.returncode, 0, result.stderr)
        artifact = json.loads(result.stdout)
        oracle = subprocess.run(
            [OBSERVATIONS, "oracle"],
            input=RECIPE,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )
        self.assertEqual(oracle.returncode, 0, oracle.stderr)
        raw = json.loads(oracle.stdout)
        self.assertEqual(artifact["finalStateHash"], raw["finalStateHash"])
        self.assertNotEqual(artifact["finalStateHash"], raw["worldOnlyHash"])
        self.assertEqual(artifact["timeline"], observed_timeline(raw["frames"]))
        for exported, actual in zip(artifact["frames"], raw["frames"], strict=True):
            self.assertEqual(exported["day"], actual["day"])
            self.assertEqual(exported["pilot"]["strandings"], actual["strandings"])
            self.assertEqual(exported["war"]["hostilePairs"], actual["hostilePairs"])
            self.assertEqual(exported["war"]["pairs"], actual["pairs"])

    @unittest.skipUnless(
        OBSERVATIONS, "set APSIS_VIEWER_OBSERVATIONS for real sim oracle"
    )
    def test_nonempty_export_timeline_copies_actual_pilot_events(self):
        # One seed with ordinary repeated flights reaches its first debt warning.
        # This exercises the exporter main loop with a real nonempty Pilot.ev.
        recipe = "333860 4 0 3020 0\n" + "".join(
            f"{day} 6 {destination} -1 0 1 0 0\n"
            for day in range(0, 3020, 2)
            for destination in range(2)
        )
        exported = self.run_export(recipe)
        self.assertEqual(exported.returncode, 0, exported.stderr)
        oracle = subprocess.run(
            [OBSERVATIONS, "oracle"],
            input=recipe,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )
        self.assertEqual(oracle.returncode, 0, oracle.stderr)
        actual = json.loads(oracle.stdout)
        artifact = json.loads(exported.stdout)
        self.assertEqual(actual["frames"][-1]["events"], [[3018, 1, "5"]])
        self.assertEqual(artifact["timeline"], observed_timeline(actual["frames"]))
        self.assertEqual(
            artifact["timeline"],
            [
                {
                    "day": 3018,
                    "type": "pilot-event",
                    "source": "Pilot.ev",
                    "kind": 1,
                    "arg": "5",
                }
            ],
        )
        self.assertEqual(artifact["finalStateHash"], actual["finalStateHash"])

    @unittest.skipUnless(
        OBSERVATIONS, "set APSIS_VIEWER_OBSERVATIONS for real sim oracle"
    )
    def test_real_main_loop_stranding_deltas_and_war_gate_transitions(self):
        recipe = (
            ROOT / "tests" / "fixtures" / "viewer" / "transitions-run.txt"
        ).read_text(encoding="utf-8")
        exported = self.run_export(recipe)
        self.assertEqual(exported.returncode, 0, exported.stderr)
        oracle = subprocess.run(
            [OBSERVATIONS, "oracle"],
            input=recipe,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )
        self.assertEqual(oracle.returncode, 0, oracle.stderr)
        actual = json.loads(oracle.stdout)
        artifact = json.loads(exported.stdout)
        expected = observed_timeline(actual["frames"])
        war = [entry for entry in expected if entry["type"] == "general-war"]
        strandings = [entry for entry in expected if entry["type"] == "stranding"]
        self.assertGreaterEqual(len(strandings), 2)
        self.assertTrue(any(entry["delta"] != entry["total"] for entry in strandings))
        self.assertTrue(
            any(entry["day"] < actual["frames"][-1]["day"] for entry in war)
        )
        # A sustained departure from the baseline detects a missing previous-war
        # update; the second rescue distinguishes an increment from its total.
        self.assertEqual(
            [(entry["day"], entry["active"]) for entry in war], [(23910, True)]
        )
        self.assertEqual(
            [(entry["day"], entry["delta"], entry["total"]) for entry in strandings],
            [(24654, "1", "1"), (25012, "1", "2")],
        )
        self.assertEqual(artifact["timeline"], expected)
        self.assertEqual(artifact["finalStateHash"], actual["finalStateHash"])
        for frame, raw in zip(artifact["frames"], actual["frames"], strict=True):
            self.assertEqual(frame["pilot"]["strandings"], raw["strandings"])
            self.assertEqual(frame["war"]["hostilePairs"], raw["hostilePairs"])
            self.assertEqual(frame["war"]["pairs"], raw["pairs"])


@unittest.skipUnless(
    OBSERVATIONS, "set APSIS_VIEWER_OBSERVATIONS for real adapter helpers"
)
class ViewerObservationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        result = subprocess.run(
            [OBSERVATIONS],
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.observations = json.loads(result.stdout)

    def test_general_war_uses_existing_predicate_including_pair_guard_and_exact_half(
        self,
    ):
        cases = self.observations["warCases"]
        seen = set()
        for case in cases:
            hostile, pairs = case["expectedHostile"], case["expectedPairs"]
            seen.add((hostile, pairs))
            with self.subTest(hostile=hostile, pairs=pairs):
                self.assertEqual(case["hostilePairs"], hostile)
                self.assertEqual(case["pairs"], pairs)
                self.assertEqual(case["active"], hostile * 2 >= pairs >= 2)
        self.assertTrue({(0, 0), (1, 1), (1, 3), (2, 3), (2, 6), (3, 6)} <= seen)

    def test_real_rescue_frames_retain_counter_and_actual_pilot_event(self):
        rescued, failed = self.observations["rescues"]
        for observation in (rescued, failed):
            raw, exported = observation["raw"], observation["exported"]
            self.assertEqual(int(raw["strandings"]) - int(observation["before"]), 1)
            self.assertEqual(exported["pilot"]["strandings"], raw["strandings"])
            self.assertFalse(exported["pilot"]["tripActive"])
        rescue_events = [event for event in rescued["raw"]["events"] if event[1] == 5]
        self.assertEqual(len(rescue_events), 1)
        self.assertGreater(int(rescue_events[0][2]), 0)
        self.assertTrue(rescued["exported"]["pilot"]["alive"])
        self.assertGreaterEqual(rescued["exported"]["pilot"]["at"], 0)
        self.assertFalse(failed["exported"]["pilot"]["alive"])
        self.assertEqual(failed["exported"]["pilot"]["end"], 1)
        self.assertFalse(any(event[1] == 5 for event in failed["raw"]["events"]))
        self.assertTrue(any(event[1] == 3 for event in failed["raw"]["events"]))
