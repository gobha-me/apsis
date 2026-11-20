"""Debt-episode statistics and report contracts for the actual sampler."""

import copy
import json
from pathlib import Path
import tempfile
import unittest

from tools.rescue_debt_sampler import aggregate, checkpoint, quantiles, render_report


def career(style, tank, completed=(), opened=-1, **extra):
    return {
        "style": style,
        "tank": tank,
        "debtClear": list(completed),
        "debtOpen": opened,
        "rescue": [],
        **extra,
    }


def summary(groups):
    return {
        "outcome": "target_reached",
        "targetDebts": 1,
        "actualDebts": 16,
        "overshoot": 15,
        "completedSeeds": 1,
        "maxSeeds": 2,
        "nextSeedIndex": 1,
        "horizonYears": 40,
        "groups": groups,
    }


class RescueDebtReportingTests(unittest.TestCase):
    def setUp(self):
        self.careers = [
            career("hauler", 3, [360, 1080], 0),
            career("hauler", 4, [720], 1440),
            career("hauler", 6),
            career("crisis", 3, opened=0),
            career("crisis", 4, [1800]),
            career("crisis", 6, [360, 720, 1080]),
            career("pathfinder", 3),
            career("pathfinder", 4, [0, 360], 720),
            career("pathfinder", 6, [1080, 1440], 1800),
        ]

    def test_every_style_and_tank_has_hand_calculated_observed_quantiles(self):
        rows = aggregate(self.careers)
        expected = [
            ("hauler", 3, 1, 3, 2, 1, 1, 3),
            ("hauler", 4, 1, 2, 1, 1, 3, 4),
            ("hauler", 6, 1, 0, 0, 0, None, None),
            ("hauler", "all", 3, 5, 3, 2, 2, 4),
            ("crisis", 3, 1, 1, 0, 1, 0, 0),
            ("crisis", 4, 1, 1, 1, 0, 5, 5),
            ("crisis", 6, 1, 3, 3, 0, 2, 3),
            ("crisis", "all", 3, 5, 4, 1, 2, 5),
            ("pathfinder", 3, 1, 0, 0, 0, None, None),
            ("pathfinder", 4, 1, 3, 2, 1, 1, 2),
            ("pathfinder", 6, 1, 3, 2, 1, 4, 5),
            ("pathfinder", "all", 3, 6, 4, 2, 2.5, 5),
        ]
        actual = [
            (
                row["style"],
                row["tank"],
                row["careers"],
                row["episodes"],
                row["completed"],
                row["openAtObservationEnd"],
                row["observedDurationsAndLowerBounds"]["p50Years"],
                row["observedDurationsAndLowerBounds"]["p90Years"],
            )
            for row in rows
        ]
        self.assertEqual(actual, expected)
        for row in rows:
            with self.subTest(style=row["style"], tank=row["tank"]):
                self.assertEqual(
                    row["observedDurationsAndLowerBounds"]["count"], row["episodes"]
                )
                self.assertEqual(
                    row["pooling"],
                    "tanks 3/4/6 pooled by episode"
                    if row["tank"] == "all"
                    else "none",
                )

    def test_completed_only_statistics_exclude_open_lower_bounds(self):
        rows = {(row["style"], row["tank"]): row for row in aggregate(self.careers)}
        for key, expected in (
            (("hauler", 3), {"count": 2, "p50Years": 2, "p90Years": 3}),
            (("hauler", "all"), {"count": 3, "p50Years": 2, "p90Years": 3}),
            (("crisis", 3), {"count": 0, "p50Years": None, "p90Years": None}),
            (("crisis", "all"), {"count": 4, "p50Years": 2.5, "p90Years": 5}),
            (("pathfinder", "all"), {"count": 4, "p50Years": 2, "p90Years": 4}),
        ):
            with self.subTest(group=key):
                self.assertEqual(rows[key]["completedDurationsOnly"], expected)

    def test_pooled_quantiles_weight_episodes_rather_than_tank_medians(self):
        rows = aggregate(
            [
                career("hauler", 3, [360] * 9),
                career("hauler", 4, [3600]),
                career("hauler", 6, [36000]),
            ]
        )
        self.assertEqual(
            rows[3]["observedDurationsAndLowerBounds"],
            {"count": 11, "p50Years": 1, "p90Years": 10},
        )

    def test_unsorted_quantiles_use_360_day_years_median_and_nearest_rank(self):
        self.assertEqual(
            quantiles([36000, 0, 3240, 360, 2880, 720, 2520, 1080, 2160, 1440, 1800]),
            {"count": 11, "p50Years": 5, "p90Years": 9},
        )
        self.assertEqual(
            quantiles([2880, 360, 0, 1080]),
            {"count": 4, "p50Years": 2, "p90Years": 8},
        )

    def test_rescue_price_count_and_amount_never_change_episode_statistics(self):
        before = aggregate(self.careers)
        prices_changed = copy.deepcopy(self.careers)
        for record in prices_changed:
            record["rescue"] = [0, 1, 999999999] * 20
        self.assertEqual(aggregate(prices_changed), before)
        rescue_only = aggregate([career("hauler", 3, rescue=[100, 200, 300])])
        self.assertEqual(rescue_only[0]["episodes"], 0)

    def test_empty_input_retains_all_twelve_explicit_json_and_markdown_groups(self):
        rows = aggregate([])
        self.assertEqual(
            [(row["style"], row["tank"]) for row in rows],
            [
                (style, tank)
                for style in ("hauler", "crisis", "pathfinder")
                for tank in (3, 4, 6, "all")
            ],
        )
        report = render_report(summary(rows))
        for row in json.loads(json.dumps(rows)):
            with self.subTest(style=row["style"], tank=row["tank"]):
                for key in ("careers", "episodes", "completed", "openAtObservationEnd"):
                    self.assertEqual(row[key], 0)
                for key in (
                    "observedDurationsAndLowerBounds",
                    "completedDurationsOnly",
                ):
                    self.assertEqual(
                        row[key], {"count": 0, "p50Years": None, "p90Years": None}
                    )
                tank = "all (3/4/6 pooled)" if row["tank"] == "all" else row["tank"]
                self.assertIn(
                    f"| {row['style'].title()} | {tank} | 0 | 0 | 0 | 0 | n/a | n/a |",
                    report,
                )

    def test_early_death_and_bankruptcy_open_ages_are_lower_bounds(self):
        rows = aggregate(
            [
                career("hauler", 3, opened=0, endState=2, endDay=1),
                career("crisis", 4, opened=360, endState=3, endDay=720),
            ]
        )
        report = render_report(summary(rows))
        self.assertIn("Open ages are lower bounds at career observation end", report)
        self.assertIn(
            "can precede the requested horizon at death or bankruptcy", report
        )
        self.assertIn("not estimates of completed clearing times", report)
        self.assertIn("requested horizon: 40 years per career", report)
        self.assertIn("| Hauler | 3 | 1 | 1 | 0 | 1 | 0.00 | 0.00 |", report)
        self.assertIn("| Crisis | 4 | 1 | 1 | 0 | 1 | 1.00 | 1.00 |", report)

    def test_checkpoint_counts_each_episode_once_and_preserves_mixed_statistics(self):
        data = summary([])
        with tempfile.TemporaryDirectory(prefix="rescue-debt-reporting-") as directory:
            output = Path(directory)
            checkpoint(output, data, self.careers)
            persisted = json.loads(
                (output / "summary.json").read_text(encoding="utf-8")
            )
            report = (output / "report.md").read_text(encoding="utf-8")
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                ["report.md", "summary.json"],
            )
        self.assertEqual(persisted["actualDebts"], 16)
        self.assertEqual(persisted["overshoot"], 15)
        self.assertEqual(persisted["groups"], aggregate(self.careers))
        self.assertIn("actual episodes: 16; overshoot: 15", report)
        self.assertIn("Pooled rows repeat the tank strata", report)
        self.assertIn("are not added again to the stopping count", report)
        self.assertIn(
            "| Pathfinder | all (3/4/6 pooled) | 3 | 6 | 4 | 2 | 2.50 | 5.00 |", report
        )


if __name__ == "__main__":
    unittest.main()
