"""Exercise the sampler validator at its existing completed-duration limits."""

import copy
import unittest

from tools.rescue_debt_sampler import validate_seed


def seed_record(years):
    return {
        "schemaVersion": 1,
        "seedIndex": 0,
        "seed": 1000,
        "start": 20,
        "majors": 4,
        "horizonYears": years,
        "tanks": [3, 4, 6],
        "careers": [
            {
                "style": style,
                "tank": tank,
                "seed": 1000,
                "start": 20,
                "debtClear": [0, years * 360],
                "debtOpen": -1,
            }
            for style in ("hauler", "crisis", "pathfinder")
            for tank in (3, 4, 6)
        ],
    }


class RescueDebtBoundsTests(unittest.TestCase):
    def test_completed_duration_at_sampling_horizon_is_valid(self):
        for years in (1, 40, 1000000):
            with self.subTest(years=years):
                record = seed_record(years)
                self.assertIs(validate_seed(record, index=0, years=years), record)

    def test_completed_duration_above_current_validator_ceiling_is_rejected(self):
        # The current inclusive validator ceiling is horizon + 1 day. R23 still
        # has a one-day gap; exercise its existing rejection without hiding it.
        for years in (1, 40, 1000000):
            valid = seed_record(years)
            for position, career in enumerate(valid["careers"]):
                with self.subTest(
                    years=years, style=career["style"], tank=career["tank"]
                ):
                    record = copy.deepcopy(valid)
                    record["careers"][position]["debtClear"].append(years * 360 + 2)
                    with self.assertRaisesRegex(
                        ValueError, "invalid completed debt durations"
                    ):
                        validate_seed(record, index=0, years=years)


if __name__ == "__main__":
    unittest.main()
