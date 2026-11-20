"""Report fixture careers through the real sweep CLI and Markdown renderer."""

import json
import subprocess
import sys

from lever_sweeps_support import REPO_ROOT, STYLES, LeverSweepTestCase, write_case


METRICS = ("pay", "refit timing", "strandings / 1,000 legs", "opening", "range")
REFITS = "2500: 2.0 y (3/21); 7500: 4.0 y (3/21); 15000: N/A y (0/21)"
OPENING = "survey 3/21, median 10.0 d; dark 4/21, median 359.5 d, Y1 2/21"
RANGE = "21/42/10 / 42 homes (eff/push/return; common to styles)"


class LeverSweepReportingTests(LeverSweepTestCase):
    def read_table(self, lever, output=None):
        output = self.output if output is None else output
        return json.loads(
            (output / "reports" / f"{lever}.table.json").read_text(encoding="utf-8")
        )

    def test_four_style_labeled_tables_have_exact_preformatted_aggregates(self):
        self.completed_fixture()
        result = self.cli("report", str(self.output))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("Wrote four comparison tables", result.stdout)
        expected_columns = {
            "tank": [
                "Style / metric",
                "Baseline: tank 6 jumps",
                "Alternative: tank 3 jumps",
                "Alternative: tank 4 jumps",
            ],
            "mass": [
                "Style / metric",
                "Baseline: mediumMass=40 mass",
                "Alternative: mediumMass=45 mass",
            ],
            "drive": [
                "Style / metric",
                "Baseline: stockDrivePm=1500 per mille J",
                "Alternative: stockDrivePm=2000 per mille J",
            ],
            "space": [
                "Style / metric",
                "Baseline: mediumSpace=120 space",
                "Alternative: mediumSpace=130 space",
            ],
        }
        expected_pay = {
            "tank": ((610, 310, 410), (620, 320, 420), (630, 330, 430)),
            "mass": ((610, 1610), (620, 1620), (630, 1630)),
            "drive": ((610, 2610), (620, 2620), (630, 2630)),
            "space": ((610, 3610), (620, 3620), (630, 3630)),
        }
        reports = self.output / "reports"
        self.assertEqual(
            {path.name for path in reports.iterdir()},
            {
                f"{lever}.{suffix}"
                for lever in expected_columns
                for suffix in ("table.json", "md")
            },
        )
        combined = (self.output / "report.md").read_text(encoding="utf-8")
        self.assertIn("Horizon: 8 years; 21 matched curated map/starts", combined)
        self.assertEqual(combined.count("| Style / metric |"), 4)
        for lever, columns in expected_columns.items():
            with self.subTest(lever=lever):
                table = self.read_table(lever)
                expected_rows = []
                count = len(columns) - 1
                for style, pay_values in zip(STYLES, expected_pay[lever]):
                    values = (
                        [f"{value:.1f} money/y (n=21)" for value in pay_values],
                        [REFITS] * count,
                        ["12.99 (3/231 legs)"] * count,
                        [OPENING] * count,
                        [RANGE] * count,
                    )
                    expected_rows.extend(
                        [f"{style}: {metric}", *cells]
                        for metric, cells in zip(METRICS, values)
                    )
                self.assertEqual(table, {"columns": columns, "rows": expected_rows})
                self.assertTrue(
                    all(isinstance(cell, str) for row in table["rows"] for cell in row)
                )
                # Independently invoke the real renderer on the driver's JSON.
                rendered = subprocess.run(
                    [
                        sys.executable,
                        str(REPO_ROOT / "tools/report_table.py"),
                        str(reports / f"{lever}.table.json"),
                    ],
                    capture_output=True,
                    encoding="utf-8",
                    timeout=10,
                    check=False,
                )
                self.assertEqual(rendered.returncode, 0, rendered.stderr)
                markdown = (reports / f"{lever}.md").read_text(encoding="utf-8")
                self.assertEqual(markdown, rendered.stdout)
                self.assertEqual(len(markdown.splitlines()), 17)
                self.assertEqual(combined.count(f"## {lever.title()}\n"), 1)
                self.assertIn(markdown.rstrip(), combined)

    def test_selected_tank_controls_baseline_and_all_other_lever_columns(self):
        expectations = {
            3: ([3, 4, 6], ["310.0", "410.0", "610.0"], "1310.0"),
            4: ([4, 3, 6], ["410.0", "310.0", "610.0"], "1410.0"),
        }
        for tank, (order, tank_pay, mass_pay) in expectations.items():
            with self.subTest(tank=tank):
                output = self.work / f"tank-{tank}"
                self.completed_fixture(output=output, tank=tank)
                result = self.cli("report", str(output))
                self.assertEqual(result.returncode, 0, result.stderr)
                table = self.read_table("tank", output)
                self.assertEqual(
                    table["columns"],
                    ["Style / metric", f"Baseline: tank {tank} jumps"]
                    + [f"Alternative: tank {value} jumps" for value in order[1:]],
                )
                self.assertEqual(
                    table["rows"][0],
                    ["hauler: pay"] + [f"{value} money/y (n=21)" for value in tank_pay],
                )
                mass = self.read_table("mass", output)
                self.assertEqual(
                    mass["rows"][0],
                    [
                        "hauler: pay",
                        f"{tank_pay[0]} money/y (n=21)",
                        f"{mass_pay} money/y (n=21)",
                    ],
                )
                for lever in ("drive", "space"):
                    table = self.read_table(lever, output)
                    self.assertEqual(table["rows"][0][1], mass["rows"][0][1])

    def test_censored_events_zero_legs_and_median_pay_remain_explicit(self):
        self.completed_fixture()
        path = self.output / "baseline/pilot_l3.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        group = [
            record
            for record in data["careers"]
            if record["style"] == "hauler" and record["tank"] == 6
        ]
        for record, net in zip(group, [-1000] * 10 + [0] + [100] * 10):
            record.update(
                net=net,
                legs=0,
                strand=0,
                step=[-1, -1, -1],
                firstSurvey=-1,
                firstDark=-1,
            )
        write_case(self.output, "baseline", data)
        result = self.cli("report", str(self.output))
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = [
            "0.0 money/y (n=21)",
            "2500: N/A y (0/21); 7500: N/A y (0/21); 15000: N/A y (0/21)",
            "N/A (0/0 legs)",
            "survey 0/21, median N/A d; dark 0/21, median N/A d, Y1 0/21",
            RANGE,
        ]
        for lever in ("tank", "mass", "drive", "space"):
            with self.subTest(lever=lever):
                self.assertEqual(
                    [row[1] for row in self.read_table(lever)["rows"][:5]], expected
                )

    def test_range_uses_each_case_and_tank_and_is_shared_across_styles(self):
        self.completed_fixture(tank=4)
        for case_index, name in enumerate(("baseline", "mass", "drive", "space")):
            source = self.output / name / "pilot_l3.json"
            data = json.loads(source.read_text(encoding="utf-8"))
            for record in data["range"]:
                tank = record["tank"]
                record.update(
                    homes=tank + case_index,
                    eff=tank - 2 + case_index,
                    push=tank - 1 + case_index,
                    roundTrip=case_index,
                )
            write_case(self.output, name, data)
        result = self.cli("report", self.output)
        self.assertEqual(result.returncode, 0, result.stderr)
        baseline = "42/63/0 / 84 homes (eff/push/return; common to styles)"
        expectations = {
            "tank": [
                baseline,
                "21/42/0 / 63 homes (eff/push/return; common to styles)",
                "84/105/0 / 126 homes (eff/push/return; common to styles)",
            ],
            "mass": [
                baseline,
                "63/84/21 / 105 homes (eff/push/return; common to styles)",
            ],
            "drive": [
                baseline,
                "84/105/42 / 126 homes (eff/push/return; common to styles)",
            ],
            "space": [
                baseline,
                "105/126/63 / 147 homes (eff/push/return; common to styles)",
            ],
        }
        for lever, values in expectations.items():
            with self.subTest(lever=lever):
                ranges = [
                    row
                    for row in self.read_table(lever)["rows"]
                    if row[0].endswith(": range")
                ]
                self.assertEqual(
                    ranges, [[f"{style}: range", *values] for style in STYLES]
                )

    def test_report_regeneration_is_repeatable_without_a_native_executable(self):
        self.completed_fixture()
        self.assertFalse(self.binary.exists())
        result = self.cli("report", str(self.output))
        self.assertEqual(result.returncode, 0, result.stderr)
        paths = list((self.output / "reports").iterdir()) + [
            self.output / "report.md",
            self.output / "manifest.json",
        ]
        paths.extend(self.output.glob("*/pilot_l3.json"))
        original = {path: path.read_bytes() for path in paths}
        result = self.cli("report", str(self.output))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual({path: path.read_bytes() for path in paths}, original)

    def test_changed_artifact_hash_is_rejected_before_tables_are_written(self):
        self.completed_fixture()
        source = self.output / "mass/pilot_l3.json"
        source.write_bytes(source.read_bytes() + b"\n")
        self.assert_cli_error(
            self.cli("report", str(self.output)), "artifact changed since run:"
        )
        self.assertFalse((self.output / "reports").exists())

    def test_malformed_or_unmatched_samples_are_rejected_before_reporting(self):
        mutations = (
            ("duplicate-career", "duplicate sample:"),
            ("duplicate-range", "duplicate sample:"),
            ("horizon", "horizon does not match manifest: mass"),
            ("career-key", "unmatched styles/tanks/seeds/starts in mass"),
            ("range-key", "unmatched styles/tanks/seeds/starts in mass"),
            ("curated-start", "expected 21 matched curated map/starts: baseline/3"),
            ("incomplete-group", "incomplete group: baseline/hauler/3"),
        )
        for mutation, diagnostic in mutations:
            with self.subTest(mutation=mutation):
                output = self.work / mutation
                self.completed_fixture(output=output)
                name = (
                    "baseline"
                    if mutation in ("curated-start", "incomplete-group")
                    else "mass"
                )
                source = output / name / "pilot_l3.json"
                data = json.loads(source.read_text(encoding="utf-8"))
                if mutation == "duplicate-career":
                    data["careers"].append(dict(data["careers"][0]))
                elif mutation == "duplicate-range":
                    data["range"].append(dict(data["range"][0]))
                elif mutation == "horizon":
                    data["pilotYears"] += 1
                elif mutation == "career-key":
                    data["careers"][0]["seed"] = -1
                elif mutation in ("range-key", "curated-start"):
                    data["range"][0]["start"] = -1
                else:
                    data["careers"].pop(0)
                    data["pilotYears"] -= 8
                write_case(output, name, data)
                self.assert_cli_error(self.cli("report", str(output)), diagnostic)
                self.assertFalse((output / "reports").exists())
