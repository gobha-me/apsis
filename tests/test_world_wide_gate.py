"""Known aggregate contracts for the real world-wide gate and its summary."""

from importlib.util import find_spec, module_from_spec
import unittest
from unittest.mock import call, patch

from tools import seed_runner
from tools.world_wide_gate import evaluate, summary, wilson_interval


def aggregate(seeds=100, off_wars=20, on_wars=25):
    """Build independent arms; these numbers never run simulation seeds."""
    return {
        arm: {
            "n": seeds,
            "war": wars,
            "episodes": 0,
            "fed": 0,
            "slow": 0,
            "slowLowMeanPm": 0,
            "slowLowP10Pm": 0,
            "slowEmpty": 0,
            "slowDaysMean": 0,
        }
        for arm, wars in (("off", off_wars), ("on", on_wars))
    }


def render(result, seeds=100):
    return summary(
        {
            **result,
            "status": result["war_gate"]["status"],
            "sample": {"seeds_per_arm": seeds},
        }
    )


class WilsonAndWarGateTests(unittest.TestCase):
    def test_known_95_percent_wilson_values_include_extreme_counts(self):
        # Fixed reference values for z=1.96, rather than a second copy of
        # the implementation's formula or expectations calculated by it.
        for passed, total, lower, upper in (
            (50, 100, 0.40382982859014716, 0.5961701714098528),
            (0, 10, 0.0, 0.2775401687666166),
            (10, 10, 0.7224598312333834, 1.0),
        ):
            with self.subTest(passed=passed, total=total):
                interval = wilson_interval(passed, total)
                self.assertEqual(interval["method"], "wilson")
                self.assertEqual(interval["confidence"], 0.95)
                self.assertEqual(interval["z"], 1.96)
                self.assertAlmostEqual(interval["lower"], lower, places=12)
                self.assertAlmostEqual(interval["upper"], upper, places=12)

    def test_both_war_intervals_use_seed_runner_bounds_at_legacy_z(self):
        spec = find_spec("tools.world_wide_gate")
        gate = module_from_spec(spec)
        shared_bounds = [(0.0, 0.0005), (0.00075, 0.003)]
        # Patch before loading the gate so its import sees the instrumented helper.
        with patch.object(
            seed_runner, "wilson_bounds", side_effect=shared_bounds
        ) as bounds:
            spec.loader.exec_module(gate)
            result = gate.evaluate(aggregate(1000, 0, 1))

        self.assertEqual(
            bounds.call_args_list, [call(0, 1000, 1.96), call(1, 1000, 1.96)]
        )
        for arm, (lower, upper) in zip(("off", "on"), shared_bounds):
            interval = result["arms"][arm]["war"]["interval"]
            self.assertEqual((interval["lower"], interval["upper"]), (lower, upper))
        self.assertFalse(result["war_gate"]["intervals_overlap"])
        self.assertEqual(result["war_gate"]["status"], "fail")

    def test_fixed_z_preserves_war_decision_at_overlap_boundary(self):
        # At 75/999 versus 111/999, z=1.96 gives a sliver of overlap.
        # The ordinary 95% normal quantile gives disjoint intervals.
        for on, overlap, status in ((111, True, "pass"), (112, False, "fail")):
            with self.subTest(on=on):
                gate = evaluate(aggregate(999, 75, on))["war_gate"]
                self.assertFalse(gate["ratio_in_range"])
                self.assertEqual(gate["intervals_overlap"], overlap)
                self.assertEqual(gate["status"], status)

    def test_ratio_alone_passes_with_nonoverlapping_intervals(self):
        result = evaluate(aggregate(800, 400, 480))
        gate = result["war_gate"]
        self.assertEqual(gate["on_off_rate_ratio"], 1.2)
        self.assertTrue(gate["ratio_in_range"])
        self.assertFalse(gate["intervals_overlap"])
        self.assertEqual(gate["status"], "pass")
        self.assertLess(
            result["arms"]["off"]["war"]["interval"]["upper"],
            result["arms"]["on"]["war"]["interval"]["lower"],
        )

    def test_overlapping_intervals_alone_pass_outside_either_ratio_limit(self):
        for off, on, ratio in ((2, 4, 2.0), (4, 2, 0.5)):
            with self.subTest(off=off, on=on):
                result = evaluate(aggregate(10, off, on))
                gate = result["war_gate"]
                self.assertEqual(gate["on_off_rate_ratio"], ratio)
                self.assertFalse(gate["ratio_in_range"])
                self.assertTrue(gate["intervals_overlap"])
                self.assertEqual(gate["status"], "pass")

    def test_ratio_boundaries_are_inclusive_and_outside_values_fail(self):
        # With an off rate of exactly 0.5, both boundary ratios are exact
        # binary fractions. All four pairs have disjoint Wilson intervals.
        for on, ratio, status in (
            (299, 0.7475, "fail"),
            (300, 0.75, "pass"),
            (500, 1.25, "pass"),
            (501, 1.2525, "fail"),
        ):
            with self.subTest(on=on):
                gate = evaluate(aggregate(800, 400, on))["war_gate"]
                self.assertEqual(gate["on_off_rate_ratio"], ratio)
                self.assertEqual(gate["ratio_in_range"], status == "pass")
                self.assertFalse(gate["intervals_overlap"])
                self.assertEqual(gate["status"], status)

    def test_clear_war_failure_preserves_arm_counts_and_denominators(self):
        result = evaluate(aggregate(100, 5, 95))
        gate = result["war_gate"]
        self.assertEqual(gate["status"], "fail")
        self.assertFalse(gate["ratio_in_range"])
        self.assertFalse(gate["intervals_overlap"])
        self.assertAlmostEqual(gate["on_off_rate_ratio"], 19.0)
        self.assertIsNone(gate["ratio_undefined_reason"])
        for arm, count, rate in (("off", 5, 0.05), ("on", 95, 0.95)):
            self.assertEqual(result["arms"][arm]["war"]["count"], count)
            self.assertEqual(result["arms"][arm]["war"]["denominator"], 100)
            self.assertEqual(result["arms"][arm]["war"]["rate"], rate)
        self.assertIn("War gate FAIL:", render(result))

    def test_zero_off_wars_leave_decision_to_interval_overlap(self):
        for on, overlap, status in (
            (0, True, "pass"),
            (1, True, "pass"),
            (100, False, "fail"),
        ):
            with self.subTest(on=on):
                gate = evaluate(aggregate(100, 0, on))["war_gate"]
                self.assertIsNone(gate["on_off_rate_ratio"])
                self.assertEqual(
                    gate["ratio_undefined_reason"], "off war count is zero"
                )
                self.assertFalse(gate["ratio_in_range"])
                self.assertEqual(gate["intervals_overlap"], overlap)
                self.assertEqual(gate["status"], status)


class RecoveryAndDepthTests(unittest.TestCase):
    def test_recovery_and_depth_fields_keep_their_own_denominators_and_units(self):
        raw = aggregate()
        raw["off"].update(
            episodes=20,
            fed=12,
            slow=6,
            slowLowMeanPm=237,
            slowLowP10Pm=15,
            slowEmpty=2,
            slowDaysMean=73,
        )
        raw["on"].update(
            episodes=8,
            fed=2,
            slow=5,
            slowLowMeanPm=80,
            slowLowP10Pm=0,
            slowEmpty=3,
            slowDaysMean=14400,
        )
        result = evaluate(raw)
        self.assertEqual(result["war_gate"]["status"], "pass")
        for arm, expected in (
            (
                "off",
                {
                    "episodes": 20,
                    "fed": 12,
                    "recovery_within_30_days_rate": 0.6,
                    "slow": 6,
                    "slow_share": 0.3,
                    "slowLowMeanPm": 237,
                    "slowLowP10Pm": 15,
                    "slow_low_mean_capacity_percent": 23.7,
                    "slow_low_p10_capacity_percent": 1.5,
                    "slowEmpty": 2,
                    "slow_empty_share": 1 / 3,
                    "slowDaysMean": 73,
                    "horizon_open_at_most_30_days": 2,
                    "status": "descriptive",
                },
            ),
            (
                "on",
                {
                    "episodes": 8,
                    "fed": 2,
                    "recovery_within_30_days_rate": 0.25,
                    "slow": 5,
                    "slow_share": 0.625,
                    "slowLowMeanPm": 80,
                    "slowLowP10Pm": 0,
                    "slow_low_mean_capacity_percent": 8.0,
                    "slow_low_p10_capacity_percent": 0.0,
                    "slowEmpty": 3,
                    "slow_empty_share": 0.6,
                    "slowDaysMean": 14400,
                    "horizon_open_at_most_30_days": 1,
                    "status": "descriptive",
                },
            ),
        ):
            with self.subTest(arm=arm):
                self.assertEqual(result["arms"][arm]["dips"], expected)

        lines = render(result).splitlines()
        off_line = next(
            i for i, line in enumerate(lines) if line.startswith("Intents off:")
        )
        on_line = next(
            i for i, line in enumerate(lines) if line.startswith("Intents on:")
        )
        self.assertIn("war 20/100 (20.00%), 95% Wilson", lines[off_line])
        self.assertIn("war 25/100 (25.00%), 95% Wilson", lines[on_line])
        self.assertEqual(
            lines[off_line + 1],
            "  Dip recovery <=30 days: 12/20 (60.00%) | slow 6/20 (30.00%) | "
            "lowest stock mean 23.7% capacity, p10 1.5% capacity | "
            "empty 2/6 (33.33%) | mean days below 73",
        )
        self.assertEqual(
            lines[on_line + 1],
            "  Dip recovery <=30 days: 2/8 (25.00%) | slow 5/8 (62.50%) | "
            "lowest stock mean 8.0% capacity, p10 0.0% capacity | "
            "empty 3/5 (60.00%) | mean days below 14400",
        )
        self.assertEqual(
            lines[off_line + 2],
            "  Horizon-open dips aged <=30 days: 2 (retained in episodes)",
        )
        self.assertEqual(
            lines[on_line + 2],
            "  Horizon-open dips aged <=30 days: 1 (retained in episodes)",
        )
        self.assertIn(
            "Recovery/depth are descriptive dip-level figures; "
            "no pass thresholds or CIs.",
            lines,
        )
        self.assertIn(
            "Slow includes late recoveries and horizon-open dips aged >30 days.",
            lines,
        )

    def test_no_observations_use_null_and_na_without_losing_raw_zero_fields(self):
        raw = aggregate()
        raw["off"].update(episodes=20, fed=18)
        result = evaluate(raw)
        off, on = (result["arms"][arm]["dips"] for arm in ("off", "on"))
        self.assertEqual(off["recovery_within_30_days_rate"], 0.9)
        self.assertEqual(off["slow_share"], 0.0)
        self.assertEqual(off["horizon_open_at_most_30_days"], 2)
        self.assertIsNone(on["recovery_within_30_days_rate"])
        self.assertIsNone(on["slow_share"])
        for dips in (off, on):
            for field in (
                "slow_low_mean_capacity_percent",
                "slow_low_p10_capacity_percent",
                "slow_empty_share",
                "slowDaysMean",
            ):
                self.assertIsNone(dips[field], field)
            for field in ("slowLowMeanPm", "slowLowP10Pm", "slowEmpty", "slow"):
                self.assertEqual(dips[field], 0, field)
        rendered = render(result)
        self.assertIn(
            "Dip recovery <=30 days: 18/20 (90.00%) | slow 0/20 (0.00%) | "
            "lowest stock mean n/a, p10 n/a | empty 0/0 (n/a) | mean days below n/a",
            rendered,
        )
        self.assertIn(
            "Dip recovery <=30 days: 0/0 (n/a) | slow 0/0 (n/a) | "
            "lowest stock mean n/a, p10 n/a | empty 0/0 (n/a) | mean days below n/a",
            rendered,
        )

    def test_severe_dip_depth_does_not_create_an_additional_gate(self):
        raw = aggregate()
        expected_gate = evaluate(raw)["war_gate"]
        for data in raw.values():
            data.update(episodes=10, slow=10, slowEmpty=10, slowDaysMean=14400)
        result = evaluate(raw)
        self.assertEqual(result["war_gate"], expected_gate)
        self.assertEqual(result["war_gate"]["status"], "pass")
        for arm in ("off", "on"):
            self.assertEqual(result["arms"][arm]["dips"]["status"], "descriptive")


if __name__ == "__main__":
    unittest.main()
