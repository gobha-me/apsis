"""Plan real CLI comparisons without requiring or launching a native binary."""

import csv
import hashlib
import unittest

from lever_sweeps_support import ENV_HOOKS, REPO_ROOT, LeverSweepTestCase


def recorded_defaults():
    """Decode the parameter source independently of the driver's parser."""
    parameters = {}
    source = REPO_ROOT / "sim/src/core/params.def"
    for line in source.read_text(encoding="utf-8").splitlines():
        if line.startswith("PARAM("):
            fields = next(csv.reader([line[6:-1]], skipinitialspace=True))
            parameters[fields[0]] = {"value": int(fields[1]), "unit": fields[2]}
    return parameters


class LeverSweepPlanTests(LeverSweepTestCase):
    def test_plan_records_exact_inputs_and_changes_only_each_named_lever(self):
        defaults = recorded_defaults()
        self.assertEqual(defaults["mediumMass"], {"value": 40, "unit": "mass"})
        self.assertEqual(defaults["heavyMass"], {"value": 80, "unit": "mass"})
        self.assertEqual(defaults["mediumSpace"], {"value": 120, "unit": "space"})
        self.assertEqual(
            defaults["stockDrivePm"], {"value": 1500, "unit": "per mille J"}
        )
        for mass_param, mass in (("mediumMass", 45), ("heavyMass", 90)):
            with self.subTest(mass_param=mass_param):
                output = self.work / mass_param
                result = self.plan(output=output, mass_param=mass_param, mass=mass)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertIn("no simulation executed", result.stdout)
                manifest = self.manifest(output)
                self.assertEqual(manifest["version"], 1)
                self.assertEqual(manifest["status"], "planned")
                self.assertEqual(manifest["years"], 8)
                self.assertEqual(manifest["styles"], ["hauler", "crisis", "pathfinder"])
                self.assertEqual(manifest["tanks"], [3, 4, 6])
                self.assertEqual(manifest["career_overrides"], {"stockTank": [3, 4, 6]})
                self.assertEqual(
                    manifest["fixture"],
                    [
                        {"seed": 333860, "majors": 4, "starts": [25, 95, 160]},
                        {"seed": 110478, "majors": 4, "starts": [10, 80, 160]},
                        {"seed": 697804, "majors": 4, "starts": [30, 90, 180]},
                        {"seed": 57206, "majors": 4, "starts": [53, 64, 75]},
                        {"seed": 632322, "majors": 4, "starts": [10, 90, 165]},
                        {"seed": 517908, "majors": 4, "starts": [80, 160, 230]},
                        {"seed": 552723, "majors": 5, "starts": [75, 120, 200]},
                    ],
                )
                self.assertIsNone(manifest["pilot_exp_sha256"])
                self.assertEqual(
                    manifest["params_sha256"],
                    hashlib.sha256(
                        (REPO_ROOT / "sim/src/core/params.def").read_bytes()
                    ).hexdigest(),
                )
                self.assertRegex(manifest["source_revision"], r"^[0-9a-f]{40}$")
                self.assertEqual(
                    set(manifest["cases"]), {"baseline", "mass", "drive", "space"}
                )
                expected_changes = {
                    "baseline": {},
                    "mass": {mass_param: mass},
                    "drive": {"stockDrivePm": 2000},
                    "space": {"mediumSpace": 130},
                }
                for name, changes in expected_changes.items():
                    case = manifest["cases"][name]
                    expected = {key: dict(value) for key, value in defaults.items()}
                    for key, value in changes.items():
                        expected[key]["value"] = value
                    self.assertEqual(case["parameters"], expected)
                    self.assertEqual(case["overrides"], changes)
                    changed = {
                        key: value["value"]
                        for key, value in case["parameters"].items()
                        if value != defaults[key]
                    }
                    self.assertEqual(changed, changes)
                    self.assertEqual(case["directory"], name)
                    self.assertEqual(case["command"], [str(self.binary.resolve()), "8"])
                    self.assertEqual(set(case["environment"]), {"APSIS_SET"})
                    assignments = case["environment"]["APSIS_SET"].split(",")
                    self.assertEqual(len(assignments), len(expected))
                    self.assertEqual(
                        dict(assignment.split("=", 1) for assignment in assignments),
                        {key: str(value["value"]) for key, value in expected.items()},
                    )
                    self.assertEqual(list((output / name).iterdir()), [])
                self.assertEqual(
                    {path.name for path in output.iterdir()},
                    {"manifest.json", "baseline", "mass", "drive", "space"},
                )
        self.assertFalse(self.binary.exists())

    def test_selected_tank_is_a_comparison_column_not_a_stock_tank_override(self):
        for tank in (3, 4, 6):
            with self.subTest(tank=tank):
                output = self.work / f"tank-{tank}"
                result = self.plan(output=output, tank=tank, years=17)
                self.assertEqual(result.returncode, 0, result.stderr)
                manifest = self.manifest(output)
                self.assertEqual(manifest["comparison_tank"], tank)
                self.assertEqual(manifest["career_overrides"], {"stockTank": [3, 4, 6]})
                self.assertEqual(manifest["years"], 17)
                for case in manifest["cases"].values():
                    self.assertEqual(
                        case["command"], [str(self.binary.resolve()), "17"]
                    )
                    self.assertEqual(
                        case["parameters"]["stockTank"], {"value": 6, "unit": "jumps"}
                    )
                    self.assertNotIn("stockTank", case["overrides"])
                    self.assertIn(
                        "stockTank=6", case["environment"]["APSIS_SET"].split(",")
                    )

    def test_every_nonbaseline_discrete_drive_class_can_be_planned(self):
        for drive in (500, 1000, 2000, 3000):
            with self.subTest(drive=drive):
                output = self.work / f"drive-{drive}"
                result = self.plan(output=output, drive=drive)
                self.assertEqual(result.returncode, 0, result.stderr)
                manifest = self.manifest(output)
                self.assertEqual(
                    manifest["cases"]["drive"]["parameters"]["stockDrivePm"],
                    {"value": drive, "unit": "per mille J"},
                )

    def test_arbitrary_tank_inputs_are_not_an_experiment_api(self):
        for arguments in (
            self.run_arguments(tank=5),
            self.run_arguments(mass_param="stockTank", mass=5),
        ):
            with self.subTest(arguments=arguments):
                result = self.cli("run", *arguments, "--dry-run")
                self.assert_cli_error(result, "invalid choice")
                self.assertFalse(self.output.exists())

    def test_inherited_overrides_are_rejected_before_creating_any_artifacts(self):
        values = {
            "APSIS_SET": "stockTank=5",
            "APSIS_DRIVE": "2000",
            "APSIS_MSPACE": "130",
            "APSIS_MMASS": "45",
            "APSIS_OFF": "rescue",
        }
        self.assertEqual(set(values), set(ENV_HOOKS))
        for name, value in values.items():
            for mode in ((), ("--dry-run",)):
                with self.subTest(name=name, mode=mode):
                    result = self.cli(
                        "run",
                        *self.run_arguments(),
                        *mode,
                        environment={name: value},
                    )
                    self.assert_cli_error(
                        result, f"unset inherited overrides before running: {name}"
                    )
                    self.assertEqual(result.stdout, "")
                    self.assertFalse(self.output.exists())

    def test_empty_inherited_hooks_are_allowed_and_recorded_environment_is_explicit(
        self,
    ):
        result = self.cli(
            "run",
            *self.run_arguments(),
            "--dry-run",
            environment={name: "" for name in ENV_HOOKS},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for case in self.manifest()["cases"].values():
            self.assertEqual(set(case["environment"]), {"APSIS_SET"})
            self.assertTrue(case["environment"]["APSIS_SET"])


if __name__ == "__main__":
    unittest.main()
