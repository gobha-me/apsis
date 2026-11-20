"""Validate fresh aggregate files through the real gate report reader."""

import copy
import json
from pathlib import Path
import re
import tempfile
import unittest

from tools.world_wide_gate import read_report


ARM = {
    "n": 2,
    "war": 1,
    "episodes": 10,
    "fed": 5,
    "slow": 3,
    "slowLowMeanPm": 250,
    "slowLowP10Pm": 100,
    "slowEmpty": 1,
    "slowDaysMean": 60,
}


class WorldWideGateReportTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="world-wide-report-tests-")
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name) / "world_wide_l2.json"

    def report(self):
        return {arm: copy.deepcopy(ARM) for arm in ("off", "on")}

    def write(self, report):
        self.path.write_text(json.dumps(report), encoding="utf-8")

    def assert_invalid(self, report, diagnostic):
        self.write(report)
        with self.assertRaisesRegex(ValueError, re.escape(diagnostic)):
            read_report(self.path, 2)

    def test_valid_paired_aggregate_is_preserved(self):
        report = self.report()
        self.write(report)
        self.assertEqual(read_report(self.path, 2), report)

    def test_report_requires_exactly_two_named_object_arms(self):
        for value in (
            None,
            [],
            "report",
            2,
            True,
            {},
            {"off": ARM},
            {"off": ARM, "on": ARM, "extra": ARM},
        ):
            with self.subTest(value=value):
                self.assert_invalid(
                    value, "report must contain exactly the off and on arms"
                )
        for arm in ("off", "on"):
            for value in (None, [], "arm", 2, True):
                with self.subTest(arm=arm, value=value):
                    report = self.report()
                    report[arm] = value
                    self.assert_invalid(report, f"{arm} must be an object")

    def test_every_required_field_rejects_missing_and_invalid_counts(self):
        for arm in ("off", "on"):
            for field in ARM:
                report = self.report()
                del report[arm][field]
                with self.subTest(arm=arm, field=field, missing=True):
                    self.assert_invalid(
                        report, f"{arm}.{field} must be a nonnegative integer"
                    )
                for value in (None, True, False, -1, 1.0, "1", [], {}):
                    with self.subTest(arm=arm, field=field, value=value):
                        report = self.report()
                        report[arm][field] = value
                        self.assert_invalid(
                            report, f"{arm}.{field} must be a nonnegative integer"
                        )

    def test_requested_seed_denominator_and_war_count_are_checked_in_both_arms(self):
        for arm in ("off", "on"):
            for field, value in (("n", 0), ("n", 1), ("n", 3), ("war", 3)):
                with self.subTest(arm=arm, field=field, value=value):
                    report = self.report()
                    report[arm][field] = value
                    self.assert_invalid(
                        report, f"{arm}: seed denominator or war count is invalid"
                    )

    def test_dip_count_capacity_and_duration_inconsistencies_are_rejected(self):
        cases = (
            ({"fed": 8}, "fed + slow exceeds episodes"),
            ({"slow": 6}, "fed + slow exceeds episodes"),
            ({"slowEmpty": 4}, "slowEmpty exceeds slow"),
            ({"slowLowMeanPm": 1001}, "capacity per-mille value exceeds 1000"),
            ({"slowLowP10Pm": 1001}, "capacity per-mille value exceeds 1000"),
            ({"slowDaysMean": 30}, "slow mean duration is outside the window"),
            ({"slowDaysMean": 14401}, "slow mean duration is outside the window"),
        )
        for arm in ("off", "on"):
            for changes, diagnostic in cases:
                with self.subTest(arm=arm, changes=changes):
                    report = self.report()
                    report[arm].update(changes)
                    self.assert_invalid(report, f"{arm}: {diagnostic}")

    def test_valid_count_capacity_and_duration_boundaries_are_accepted(self):
        for war in (0, 2):
            for duration in (31, 14400):
                for capacity in (0, 1000):
                    for empty in (0, 3):
                        with self.subTest(
                            war=war, duration=duration, capacity=capacity, empty=empty
                        ):
                            report = self.report()
                            for data in report.values():
                                data.update(
                                    war=war,
                                    fed=7,
                                    slowDaysMean=duration,
                                    slowLowMeanPm=capacity,
                                    slowLowP10Pm=capacity,
                                    slowEmpty=empty,
                                )
                            self.write(report)
                            self.assertEqual(read_report(self.path, 2), report)

    def test_no_slow_observations_require_zero_placeholder_metrics(self):
        for arm in ("off", "on"):
            report = self.report()
            report[arm].update(
                slow=0,
                slowEmpty=0,
                slowLowMeanPm=0,
                slowLowP10Pm=0,
                slowDaysMean=0,
            )
            self.write(report)
            self.assertEqual(read_report(self.path, 2), report)
            for field in ("slowLowMeanPm", "slowLowP10Pm", "slowDaysMean"):
                with self.subTest(arm=arm, field=field):
                    invalid = copy.deepcopy(report)
                    invalid[arm][field] = 1
                    self.assert_invalid(
                        invalid, f"{arm}: slow metrics without slow observations"
                    )
            report[arm]["slowEmpty"] = 1
            self.assert_invalid(report, f"{arm}: slowEmpty exceeds slow")

    def test_malformed_json_is_rejected(self):
        for source in ("", "{", "not json", "{} trailing", '{"off":'):
            with self.subTest(source=source):
                self.path.write_text(source, encoding="utf-8")
                with self.assertRaises(json.JSONDecodeError):
                    read_report(self.path, 2)

    def test_duplicate_arm_and_nested_metric_keys_are_rejected(self):
        valid = json.dumps(self.report())
        for source, key in (
            (valid[:-1] + ', "off": {}}', "off"),
            (valid.replace('"war": 1', '"war": 1, "war": 0', 1), "war"),
        ):
            with self.subTest(key=key):
                self.path.write_text(source, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, f"duplicate JSON key: {key}"):
                    read_report(self.path, 2)

    def test_nonfinite_json_constants_are_rejected_before_schema_checks(self):
        for constant in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(constant=constant):
                source = json.dumps(self.report()).replace(
                    '"war": 1', f'"war": {constant}', 1
                )
                self.path.write_text(source, encoding="utf-8")
                with self.assertRaisesRegex(
                    ValueError, f"non-finite JSON value: {constant}"
                ):
                    read_report(self.path, 2)

    def test_non_utf8_report_is_rejected(self):
        self.path.write_bytes(b"\xff\xfe")
        with self.assertRaisesRegex(UnicodeDecodeError, "utf-8"):
            read_report(self.path, 2)


if __name__ == "__main__":
    unittest.main()
