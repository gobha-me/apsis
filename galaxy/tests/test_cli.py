"""Compare the real CLI with snapshots emitted through the public Map API."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import unittest


SEEDS = (0, 42, 1 << 32, (1 << 64) - 1)
ADJACENT = ((2, 0), (3, 0), (2, 1), (3, 1), (-3, -1), (-2, -1))
REORDERED = (
    ADJACENT[3],
    ADJACENT[0],
    ADJACENT[5],
    ADJACENT[1],
    ADJACENT[4],
    ADJACENT[2],
)
CLI = None
PROBE = None


def region_json(cx, cy):
    return {"cx": cx, "cy": cy}


def id_json(cx, cy, slot):
    return {"region": region_json(cx, cy), "slot": slot}


def probe_snapshot(output, regions):
    """Parse a test helper's plain records, independently of CLI serialization."""
    lines = output.splitlines()
    if not lines:
        raise ValueError("library probe produced no metadata")
    header = lines[0].split()
    if len(header) != 8 or header[0] != "M":
        raise ValueError(f"invalid library metadata record: {lines[0]!r}")
    seed, version, ticks, side, inner, outer = map(int, header[1:7])
    result = {
        "format_version": version,
        "seed": str(seed),
        "config": {
            "raw_ticks_per_map_unit": ticks,
            "cell_side_raw": side,
            "inner_radius_raw": inner,
            "outer_radius_raw": outer,
        },
        "status": "ok",
        "raw_units_per_j": None if header[7] == "null" else int(header[7]),
        "viewed_regions": [region_json(cx, cy) for cx, cy in sorted(set(regions))],
        "systems": [],
        "lanes": [],
    }
    for line in lines[1:]:
        fields = line.split()
        if len(fields) != 9:
            raise ValueError(f"invalid library snapshot record: {line!r}")
        values = tuple(map(int, fields[1:]))
        if fields[0] == "S":
            cx, cy, slot, x, y, radius, epoch_x, epoch_y = values
            result["systems"].append(
                {
                    "id": id_json(cx, cy, slot),
                    "position_raw": {"x": x, "y": y},
                    "orbit": {
                        "radius_raw": radius,
                        "epoch_vector_raw": {"x": epoch_x, "y": epoch_y},
                    },
                }
            )
        elif fields[0] == "L":
            ax, ay, a_slot, bx, by, b_slot, length, milli_j = values
            result["lanes"].append(
                {
                    "a": id_json(ax, ay, a_slot),
                    "b": id_json(bx, by, b_slot),
                    "raw_length": length,
                    "milli_j": milli_j,
                }
            )
        else:
            raise ValueError(f"unknown library snapshot record: {line!r}")
    return result


class GalaxyCliTests(unittest.TestCase):
    maxDiff = None

    def run_program(self, program, arguments):
        return subprocess.run(
            [str(program), *arguments],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def cli_snapshot(self, seed, regions):
        arguments = ["--seed", str(seed)]
        for cx, cy in regions:
            arguments.extend(("--region", f"{cx},{cy}"))
        completed = self.run_program(CLI, arguments)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        return json.loads(completed.stdout)

    def library_snapshot(self, seed, regions):
        completed = self.run_program(
            PROBE, [str(seed), *(f"{cx},{cy}" for cx, cy in regions)]
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, "")
        return probe_snapshot(completed.stdout, regions)

    def test_json_matches_public_library_for_every_field(self):
        cases = {
            "unrevealed": (),
            "empty_center": ((0, 0),),
            "extreme_cells": ((-(1 << 31), (1 << 31) - 1), ((1 << 31) - 1, -(1 << 31))),
            "adjacent": ADJACENT,
            "reversed": tuple(reversed(ADJACENT)),
            "shuffled_and_repeated": REORDERED + (ADJACENT[0], ADJACENT[-1]),
        }
        for seed in SEEDS:
            for name, regions in cases.items():
                with self.subTest(seed=seed, case=name):
                    self.assertEqual(
                        self.cli_snapshot(seed, regions),
                        self.library_snapshot(seed, regions),
                    )

    def test_repeated_and_reordered_requests_keep_identical_json(self):
        for seed in SEEDS:
            with self.subTest(seed=seed):
                expected = self.cli_snapshot(seed, ADJACENT)
                self.assertTrue(expected["systems"])
                self.assertTrue(expected["lanes"])
                for regions in (
                    tuple(reversed(ADJACENT)),
                    REORDERED,
                    ADJACENT + ADJACENT,
                    ((0, 0),) + ADJACENT,
                ):
                    actual = self.cli_snapshot(seed, regions)
                    # Empty cells are still explicitly recorded as viewed.
                    actual.pop("viewed_regions")
                    comparable = {
                        key: value
                        for key, value in expected.items()
                        if key != "viewed_regions"
                    }
                    self.assertEqual(actual, comparable)

    def test_full_uint64_seed_is_preserved_as_a_decimal_string(self):
        result = self.cli_snapshot((1 << 64) - 1, ADJACENT)
        self.assertIsInstance(result["seed"], str)
        self.assertEqual(result["seed"], "18446744073709551615")

    def test_canonical_decimal_seed_and_regions_are_accepted(self):
        regions = ((-5, 10), (0, 0), (-(1 << 31), (1 << 31) - 1))
        for seed in (0, (1 << 64) - 1):
            with self.subTest(seed=seed):
                actual = self.cli_snapshot(seed, regions)
                self.assertEqual(actual, self.library_snapshot(seed, regions))
                self.assertEqual(actual["seed"], str(seed))

    def test_unrevealed_and_empty_regions(self):
        initial = self.cli_snapshot(42, ())
        self.assertEqual(initial["systems"], [])
        self.assertEqual(initial["lanes"], [])
        self.assertEqual(initial["viewed_regions"], [])
        self.assertIsNone(initial["raw_units_per_j"])
        empty = self.cli_snapshot(42, ((0, 0), ((1 << 31) - 1, -(1 << 31))))
        self.assertEqual(empty["systems"], [])
        self.assertEqual(empty["lanes"], [])
        self.assertGreater(empty["raw_units_per_j"], 0)

    def test_argument_errors_are_json_and_exit_two(self):
        cases = {
            "seed_overflow": ["--seed", "18446744073709551616"],
            "negative_seed": ["--seed", "-1"],
            "seed_suffix": ["--seed", "42x"],
            "seed_leading_zeros": ["--seed", "007"],
            "seed_double_zero": ["--seed", "00"],
            "seed_negative_zero": ["--seed", "-0"],
            "seed_plus_sign": ["--seed", "+1"],
            "seed_leading_space": ["--seed", " 1"],
            "seed_trailing_space": ["--seed", "1 "],
            "seed_leading_tab": ["--seed", "\t1"],
            "seed_sign_only": ["--seed", "-"],
            "missing_seed": [],
            "duplicate_seed": ["--seed", "1", "--seed", "2"],
            "missing_seed_value": ["--seed"],
            "missing_region_value": ["--seed", "1", "--region"],
            "coordinate_overflow": ["--seed", "1", "--region", "2147483648,0"],
            "coordinate_underflow": ["--seed", "1", "--region", "0,-2147483649"],
            "extra_coordinate": ["--seed", "1", "--region", "2,0,1"],
            "missing_coordinate": ["--seed", "1", "--region", "2,"],
            "coordinate_negative_zero": ["--seed", "1", "--region", "-0,0"],
            "coordinate_leading_zero": ["--seed", "1", "--region", "01,0"],
            "coordinate_plus_sign": ["--seed", "1", "--region", "+1,0"],
            "coordinate_second_leading_space": ["--seed", "1", "--region", "1, 2"],
            "coordinate_first_leading_space": ["--seed", "1", "--region", " 1,2"],
            "coordinate_negative_leading_zero": ["--seed", "1", "--region", "0,-01"],
            "coordinate_first_sign_only": ["--seed", "1", "--region", "-,0"],
            "coordinate_second_sign_only": ["--seed", "1", "--region", "0,-"],
            "unknown_option": ["--seed", "1", "--bogus"],
        }
        for name, arguments in cases.items():
            with self.subTest(case=name):
                completed = self.run_program(CLI, arguments)
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                error = json.loads(completed.stderr)
                self.assertEqual(set(error), {"status", "error"})
                self.assertEqual(error["status"], "invalid_arguments")
                self.assertIsInstance(error["error"], str)
                self.assertTrue(error["error"])

    def test_help_runs_the_real_cli(self):
        completed = self.run_program(CLI, ["--help"])
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stderr, "")
        self.assertIn("--seed", completed.stdout)
        self.assertIn("--region", completed.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    arguments = parser.parse_args()
    CLI = arguments.cli.resolve()
    PROBE = arguments.probe.resolve()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
