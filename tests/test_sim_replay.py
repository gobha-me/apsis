"""Tiny recorded command streams and optional real-sim replay determinism checks.

Set APSIS_SIM_REPLAY_DRIVER to the test-only native driver. Set
APSIS_SIM_REPLAY_DRIVER_O0 to a driver whose complete sim libraries were compiled
at -O0 to compare with a primary -O2 build. Tests never regenerate golden files.
"""

import json
import os
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "sim-replay"
DRIVER = os.environ.get("APSIS_SIM_REPLAY_DRIVER")
DRIVER_O0 = os.environ.get("APSIS_SIM_REPLAY_DRIVER_O0")


def corpus():
    return json.loads((FIXTURES / "expected.json").read_text(encoding="utf-8"))


class ReplayCorpusTests(unittest.TestCase):
    def test_corpus_is_tiny_and_expected_traces_cover_every_day(self):
        records = corpus()
        self.assertEqual(records["schemaVersion"], 1)
        self.assertEqual(len(records["replays"]), 3)
        self.assertLess(
            sum(path.stat().st_size for path in FIXTURES.iterdir() if path.is_file()),
            1_000_000,
        )
        same_day_actions = False
        for name, expected in records["replays"].items():
            with self.subTest(replay=name):
                self.assertEqual(Path(name).name, name)
                recipe = (FIXTURES / name).read_text(encoding="utf-8")
                rows = [line.split() for line in recipe.splitlines() if line.strip()]
                seed, majors, days = map(int, rows[0])
                self.assertEqual(
                    (expected["seed"], expected["majors"], expected["days"]),
                    (seed, majors, days),
                )
                self.assertEqual(expected["actions"], len(rows) - 1)
                self.assertGreater(expected["actions"], 1)
                self.assertTrue(expected["pilot"]["alive"])
                self.assertGreater(expected["pilot"]["surveys"], 0)
                for row in rows[1:]:
                    self.assertEqual(len(row), 8)
                    self.assertLess(int(row[0]), days)
                    self.assertGreaterEqual(int(row[0]), 0)
                same_day_actions |= len({row[0] for row in rows[1:]}) < len(rows) - 1
                hashes = expected["stateHashes"]
                self.assertEqual(len(hashes), days + 1)
                self.assertEqual(len(set(hashes)), days + 1)
                self.assertTrue(all(re.fullmatch(r"[0-9a-f]{16}", h) for h in hashes))
                self.assertEqual(expected["replayFrames"], days + 1)
                self.assertEqual(expected["repeatFrames"], days + 1)
                self.assertEqual(expected["reverseFrames"], days + 1)
                self.assertEqual(expected["resumeFrames"], days - days // 2 + 1)
                self.assertGreater(expected["roundTripBytes"], 0)
                self.assertGreater(expected["pilot"]["legs"], 0)
        self.assertTrue(
            same_day_actions, "exercise canonical ordering with real commands"
        )


@unittest.skipUnless(DRIVER, "set APSIS_SIM_REPLAY_DRIVER for real-sim replays")
class NativeReplayTests(unittest.TestCase):
    def run_driver(self, recipe, driver=None, *, args=()):
        return subprocess.run(
            [str(Path(driver or DRIVER).resolve()), *args],
            input=recipe,
            capture_output=True,
            text=True,
            cwd=ROOT,
            timeout=15,
            check=False,
        )

    def test_recorded_replays_match_every_golden_checkpoint_hash(self):
        for name, expected in corpus()["replays"].items():
            with self.subTest(replay=name):
                recipe = (FIXTURES / name).read_text(encoding="utf-8")
                result = self.run_driver(recipe)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout), expected)

    def test_same_seed_and_commands_produce_bit_identical_output_across_processes(self):
        for name in corpus()["replays"]:
            with self.subTest(replay=name):
                recipe = (FIXTURES / name).read_text(encoding="utf-8")
                first = self.run_driver(recipe)
                second = self.run_driver(recipe)
                self.assertEqual(first.returncode, 0, first.stderr)
                self.assertEqual(second.returncode, 0, second.stderr)
                self.assertEqual(first.stdout, second.stdout)

    @unittest.skipUnless(DRIVER_O0, "set APSIS_SIM_REPLAY_DRIVER_O0 for -O0/-O2 parity")
    def test_all_daily_hashes_are_identical_at_o0_and_o2(self):
        for name in corpus()["replays"]:
            with self.subTest(replay=name):
                recipe = (FIXTURES / name).read_text(encoding="utf-8")
                optimized = self.run_driver(recipe)
                unoptimized = self.run_driver(recipe, DRIVER_O0)
                self.assertEqual(optimized.returncode, 0, optimized.stderr)
                self.assertEqual(unoptimized.returncode, 0, unoptimized.stderr)
                self.assertEqual(optimized.stdout, unoptimized.stdout)

    def test_hash_regressions_detect_a_changed_seed_or_recorded_action(self):
        name, expected = next(iter(corpus()["replays"].items()))
        recipe = (FIXTURES / name).read_text(encoding="utf-8")
        seed, _majors, days = map(int, recipe.splitlines()[0].split())
        changed_seed = recipe.replace(str(seed), str(seed + 1), 1)
        changed_command = recipe + f"{days - 1} 9 3 1 0 0 0 0\n"
        for altered in (changed_seed, changed_command):
            with self.subTest(recipe=altered):
                result = self.run_driver(altered)
                self.assertEqual(result.returncode, 0, result.stderr)
                actual = json.loads(result.stdout)
                self.assertNotEqual(
                    actual["stateHashes"][-1], expected["stateHashes"][-1]
                )

    def test_verify_accepts_five_majors(self):
        result = self.run_driver("333860 5 2\n0 0 0 0 0 0 0 0\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["majors"], 5)

    def test_record_accepts_five_majors_and_rejects_six(self):
        valid = self.run_driver("", args=("record", "333860", "5", "2", "0"))
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertEqual(valid.stdout.partition("\n")[0], "333860 5 2")

        invalid = self.run_driver("", args=("record", "333860", "6", "2", "0"))
        self.assertEqual(invalid.returncode, 2, invalid.stderr)
        self.assertEqual(invalid.stdout, "")

    def test_invalid_or_unbounded_recipes_fail_without_partial_json(self):
        for recipe in (
            "",
            "333860 4 1\n",
            "333860 4 361\n",
            "4294967296 4 30\n",
            "-1 4 30\n",
            "333860 3 30\n",
            "333860 6 2\n0 0 0 0 0 0 0 0\n",
            "333860 4 30 extra\n",
            "333860 4 30\n30 0 0 0 0 0 0 0\n",
            "333860 4 30\n-1 0 0 0 0 0 0 0\n",
            "333860 4 30\n0 11 0 0 0 0 0 0\n",
            "333860 4 30\n0 9 0 0 0 0 0\n",
        ):
            with self.subTest(recipe=recipe):
                result = self.run_driver(recipe)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
