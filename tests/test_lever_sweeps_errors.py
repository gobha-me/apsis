"""CLI exit contract: success is 0; bad inputs/dependencies/child failures are 2."""

import json
import shutil
import sys
import unittest

from lever_sweeps_support import LeverSweepTestCase, REPO_ROOT


class LeverSweepErrorTests(LeverSweepTestCase):
    def test_missing_nonexecutable_and_directory_binaries_exit_two(self):
        nonexecutable = self.work / "not executable"
        nonexecutable.write_text("must not run", encoding="utf-8")
        nonexecutable.chmod(0o644)
        directory = self.work / "binary directory"
        directory.mkdir()
        for binary in (self.binary, nonexecutable, directory):
            with self.subTest(binary=binary):
                result = self.cli("run", *self.run_arguments(binary=binary))
                self.assert_cli_error(result, f"pilot_exp is not executable: {binary}")
                self.assertEqual(result.stdout, "")
                self.assertFalse(self.output.exists())

    def test_missing_required_arguments_exit_two(self):
        for arguments in ((), ("run",), ("report",)):
            with self.subTest(arguments=arguments):
                result = self.cli(*arguments)
                self.assert_cli_error(result, "required")
                self.assertIn("usage:", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_positive_inputs_reject_zero_negative_overflow_and_nonintegers(self):
        for option in ("mass", "space", "years"):
            for value in (0, -1, 2147483648, "1.5", "oops"):
                with self.subTest(option=option, value=value):
                    result = self.plan(**{option: value})
                    self.assert_cli_error(result, f"argument --{option}")
                    if value in (0, -1, 2147483648):
                        self.assertIn("integer from 1 to 2147483647", result.stderr)
                    self.assertFalse(self.output.exists())

    def test_signed_32_bit_upper_bound_is_accepted_for_planning(self):
        result = self.plan(mass=2147483647, space=2147483647, years=2147483647)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        self.assertEqual(manifest["years"], 2147483647)
        self.assertEqual(
            manifest["cases"]["mass"]["overrides"], {"mediumMass": 2147483647}
        )
        self.assertEqual(
            manifest["cases"]["space"]["overrides"], {"mediumSpace": 2147483647}
        )

    def test_invalid_discrete_choices_and_unknown_options_exit_two(self):
        for overrides, diagnostic in (
            ({"mass_param": "lightMass"}, "argument --mass-param: invalid choice"),
            ({"drive": 1750}, "argument --drive: invalid choice"),
        ):
            with self.subTest(overrides=overrides):
                self.assert_cli_error(self.plan(**overrides), diagnostic)
                self.assertFalse(self.output.exists())
        self.assert_cli_error(
            self.cli("run", *self.run_arguments(), "--dry-run", "--unknown"),
            "unrecognized arguments: --unknown",
        )

    def test_alternatives_equal_to_baselines_are_rejected_before_output(self):
        for overrides, parameter in (
            ({"mass": 40}, "mediumMass"),
            ({"mass_param": "heavyMass", "mass": 80}, "heavyMass"),
            ({"drive": 1500}, "stockDrivePm"),
            ({"space": 120}, "mediumSpace"),
        ):
            with self.subTest(parameter=parameter):
                result = self.plan(**overrides)
                self.assert_cli_error(
                    result, f"alternative must differ from {parameter}'s baseline"
                )
                self.assertEqual(result.stdout, "")
                self.assertFalse(self.output.exists())

    def test_missing_and_malformed_manifests_exit_two_without_tracebacks(self):
        self.assert_cli_error(
            self.cli("report", str(self.output)), "No such file or directory"
        )
        self.output.mkdir()
        path = self.output / "manifest.json"
        for source, diagnostic in (
            ("{", "Expecting property name"),
            ("{}", "'version'"),
        ):
            with self.subTest(source=source):
                path.write_text(source, encoding="utf-8")
                self.assert_cli_error(self.cli("report", str(self.output)), diagnostic)
                self.assertFalse((self.output / "report.md").exists())

    def test_planned_running_and_unsupported_version_manifests_cannot_report(self):
        result = self.plan()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        for version, status in ((1, "planned"), (1, "running"), (2, "complete")):
            with self.subTest(version=version, status=status):
                manifest.update(version=version, status=status)
                (self.output / "manifest.json").write_text(
                    json.dumps(manifest), encoding="utf-8"
                )
                self.assert_cli_error(
                    self.cli("report", str(self.output)),
                    "report requires a completed version-1 run, not a dry-run plan",
                )
                self.assertFalse((self.output / "reports").exists())

    def test_pilot_subprocess_failure_exits_two_and_preserves_failure_logs(self):
        self.binary.write_text(
            f"#!{sys.executable}\nimport sys\nprint('fixture failure', file=sys.stderr)\nsys.exit(7)\n",
            encoding="utf-8",
        )
        self.binary.chmod(0o755)
        result = self.cli("run", *self.run_arguments())
        self.assert_cli_error(result, "returned non-zero exit status 7")
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "running")
        self.assertNotIn("pilot_l3_sha256", manifest["cases"]["baseline"])
        self.assertEqual(
            (self.output / "baseline" / "stderr.log").read_text(encoding="utf-8"),
            "fixture failure\n",
        )
        self.assertFalse((self.output / "mass" / "stdout.log").exists())
        self.assertFalse((self.output / "report.md").exists())

    def test_missing_real_renderer_exits_two(self):
        self.completed_fixture()
        isolated_root = self.work / "driver without renderer"
        tools = isolated_root / "tools"
        tools.mkdir(parents=True)
        original = REPO_ROOT / "tools" / "lever_sweeps.py"
        copied = tools / "lever_sweeps.py"
        shutil.copyfile(original, copied)
        self.assertEqual(copied.read_bytes(), original.read_bytes())
        result = self.cli("report", str(self.output), root=isolated_root)
        self.assert_cli_error(result, "returned non-zero exit status 2")
        self.assertIn(str(tools / "report_table.py"), result.stderr)
        self.assertIn("No such file or directory", result.stderr)
        self.assertFalse((self.output / "report.md").exists())
        self.assertFalse((self.output / "reports" / "tank.md").exists())


if __name__ == "__main__":
    unittest.main()
