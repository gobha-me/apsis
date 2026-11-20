"""Exercise real run orchestration with a tiny pilot_exp output fixture."""

import hashlib
import json
from pathlib import Path

from lever_sweeps_support import ENV_HOOKS, REPO_ROOT, LeverSweepTestCase


def imported_result_hashes():
    return {
        path.relative_to(REPO_ROOT): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (REPO_ROOT / "sim").rglob("*")
        if path.is_file() and path.suffix in (".json", ".txt")
    }


class LeverSweepArtifactTests(LeverSweepTestCase):
    def test_empty_inherited_hooks_are_stripped_from_child_environment(self):
        self.fixture_binary()
        # Empty hooks pass the parent's truthy-value guard but must not leak.
        result = self.cli(
            "run",
            *self.run_arguments(),
            environment={hook: "" for hook in ENV_HOOKS},
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(set(manifest["cases"]), {"baseline", "mass", "drive", "space"})
        for name, case in manifest["cases"].items():
            with self.subTest(case=name):
                observed = json.loads(
                    (self.output / name / "stdout.log").read_text(encoding="utf-8")
                )
                for hook in ENV_HOOKS:
                    if hook != "APSIS_SET":
                        self.assertNotIn(hook, observed["hooks"])
                self.assertTrue(observed["hooks"]["APSIS_SET"])
                self.assertEqual(
                    observed["hooks"]["APSIS_SET"], case["environment"]["APSIS_SET"]
                )

    def test_fixture_run_isolates_fixed_filenames_and_records_actual_invocations(self):
        before = imported_result_hashes()
        self.assertIn(Path("sim/pilot_l3.json"), before)
        binary = self.fixture_binary()
        output = self.work / "campaign results with spaces"
        result = self.cli(
            "run", *self.run_arguments(output=output), cwd=REPO_ROOT / "sim"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("Wrote four comparison tables", result.stdout)
        manifest = self.manifest(output)
        self.assertEqual(manifest["status"], "complete")
        self.assertEqual(
            manifest["pilot_exp_sha256"],
            hashlib.sha256(binary.read_bytes()).hexdigest(),
        )
        self.assertEqual(set(manifest["cases"]), {"baseline", "mass", "drive", "space"})
        for name, case in manifest["cases"].items():
            with self.subTest(case=name):
                directory = output / name
                self.assertEqual(case["directory"], name)
                self.assertEqual(case["command"], [str(binary.resolve()), "8"])
                self.assertEqual(
                    {p.name for p in directory.iterdir()},
                    {"pilot_l3.json", "stdout.log", "stderr.log"},
                )
                self.assertEqual((directory / "stderr.log").read_bytes(), b"")
                observed = json.loads((directory / "stdout.log").read_text())
                self.assertEqual(observed["cwd"], str(directory.resolve()))
                self.assertEqual(observed["argv"], ["8"])
                self.assertEqual(
                    {k: v for k, v in observed["hooks"].items() if k in ENV_HOOKS},
                    case["environment"],
                )
                applied = dict(
                    assignment.split("=")
                    for assignment in observed["hooks"]["APSIS_SET"].split(",")
                )
                self.assertEqual(
                    applied,
                    {k: str(v["value"]) for k, v in case["parameters"].items()},
                )
                self.assertEqual(
                    case["pilot_l3_sha256"],
                    hashlib.sha256(
                        (directory / "pilot_l3.json").read_bytes()
                    ).hexdigest(),
                )
        self.assertEqual(imported_result_hashes(), before)
        self.assertTrue((output / "report.md").is_file())
        report_before = (output / "report.md").read_bytes()
        regenerated = self.cli("report", output)
        self.assertEqual(regenerated.returncode, 0, regenerated.stderr)
        self.assertEqual((output / "report.md").read_bytes(), report_before)
        self.assertEqual(imported_result_hashes(), before)

    def test_existing_output_and_symlink_to_imported_results_are_refused(self):
        before = imported_result_hashes()
        binary = self.fixture_binary()
        alias = self.work / "imported-results-alias"
        alias.symlink_to(REPO_ROOT / "sim", target_is_directory=True)
        for output in (REPO_ROOT / "sim", alias):
            with self.subTest(output=output):
                result = self.cli(
                    "run", *self.run_arguments(output=output, binary=binary)
                )
                self.assert_cli_error(result, "File exists")
                self.assertEqual(result.stdout, "")
                self.assertEqual(imported_result_hashes(), before)

    def test_existing_fixture_run_is_not_reused_or_overwritten(self):
        self.fixture_binary()
        first = self.cli("run", *self.run_arguments())
        self.assertEqual(first.returncode, 0, first.stderr)
        before = {
            p.relative_to(self.output): p.read_bytes()
            for p in self.output.rglob("*")
            if p.is_file()
        }
        second = self.cli("run", *self.run_arguments())
        self.assert_cli_error(second, "File exists")
        self.assertEqual(second.stdout, "")
        self.assertEqual(
            {
                p.relative_to(self.output): p.read_bytes()
                for p in self.output.rglob("*")
                if p.is_file()
            },
            before,
        )
