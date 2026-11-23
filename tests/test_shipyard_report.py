"""Read-only report planning, response checks, and optional native integration."""

import copy
import csv
import importlib.util
import io
import json
import os
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "shipyard_report", ROOT / "tools/shipyard_report.py"
)
report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(report)
FIXTURES = ROOT / "shipyard/fixtures/v3"
SKIFF = FIXTURES / "apsis-skiff-definition-v3.json"
RULES = ROOT / "shipyard/rules/apsis-rules-v4.json"
SCHEMAS = ROOT / "shipyard/schemas"


def response():
    return {
        "schema": "apsis-shipyard-result-v1",
        "operation": "validate",
        "pass": True,
        "status": "PASS",
        "rulesVersion": 4,
        "rulesSha256": report.digest(RULES.read_bytes()),
        "issues": [],
        "data": {
            "structuralPass": True,
            "checks": {"crewRoutes": True, "approachClearance": True},
            "definition": {
                "validation": {"ready": None, "jumpReady": None},
                "derived": {"cost": 100, "currentMass": 10},
            },
            "migration": {},
        },
    }


class ReportPlanningTests(unittest.TestCase):
    def test_grid_is_cartesian_and_does_not_modify_source(self):
        before = SKIFF.read_bytes()
        axes = report.parse_axes(
            ["/blueprint/loads/cargo=[0,0.5,1]", "/blueprint/loads/sublightFuel=[0,1]"]
        )
        sources, cases = report.plan([SKIFF], axes)
        self.assertEqual(6, len(cases))
        self.assertEqual(list(range(6)), [case["id"] for case in cases])
        self.assertEqual(before, SKIFF.read_bytes())
        self.assertEqual(report.digest(before), sources[0]["sha256"])
        self.assertEqual(6, len({case["candidateSha256"] for case in cases}))
        self.assertEqual(
            {"/blueprint/loads/cargo": 1, "/blueprint/loads/sublightFuel": 1},
            cases[-1]["overrides"],
        )

    def test_no_axis_means_one_measurement_per_source(self):
        _, cases = report.plan([SKIFF, SKIFF], [])
        self.assertEqual(2, len(cases))
        self.assertEqual(cases[0]["input"], cases[1]["input"])

    def test_bounded_plan_and_input(self):
        with self.assertRaisesRegex(report.ReportError, "64"):
            report.plan([SKIFF] * 65, [])
        with self.assertRaisesRegex(report.ReportError, "64"):
            report.plan([SKIFF] * 33, report.parse_axes(["/blueprint/budget=[1,2]"]))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "large.json"
            path.write_bytes(b" " * (report.MAX_BYTES + 1))
            with self.assertRaisesRegex(report.ReportError, "8 MiB"):
                report.plan([path], [])

    def test_axes_reject_missing_cache_collection_and_noncanonical_array_fields(self):
        for pointer in (
            "/blueprint/noSuchField",
            "/derived/cost",
            "/rules/version",
            "/blueprint/decks",
            "/blueprint/decks/-1/name",
            "/blueprint/decks/00/name",
            "/blueprint/~3",
        ):
            with self.subTest(pointer=pointer), self.assertRaises(report.ReportError):
                report.plan([SKIFF], [(pointer, [0])])

    def test_axis_syntax_and_strict_json(self):
        for value in (
            "/x=[]",
            "/x=[1,1]",
            "/x=[{}]",
            "/x=[NaN]",
            "/x=[1e999]",
            "x=[1]",
            "/x=3",
            "/x",
        ):
            with self.subTest(value=value), self.assertRaises(ValueError):
                report.parse_axes([value])
        with self.assertRaises(report.ReportError):
            report.parse_axes(["/x=[1]", "/x=[2]"])
        with self.assertRaises(report.ReportError):
            report.strict_json('{"a":1,"a":2}')

    def test_pointer_escapes_and_raw_blueprint(self):
        source = {"a/b": {"~": 1}, "decks": [{"name": "deck"}]}
        target, key = report.location(source, "/a~1b/~0")
        self.assertEqual(1, target[key])
        target, key = report.location(source, "/decks/0/name")
        self.assertEqual("deck", target[key])

    def test_dry_run_does_not_require_driver_or_run_process(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(report.subprocess, "run") as run,
        ):
            output = Path(directory) / "plan"
            with redirect_stdout(io.StringIO()):
                result = report.main(
                    [
                        str(SKIFF),
                        "--driver",
                        "/nonexistent/adapter",
                        "--output",
                        str(output),
                        "--dry-run",
                    ]
                )
            self.assertEqual(0, result)
            run.assert_not_called()
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual("planned", manifest["status"])
            self.assertNotIn("input", manifest["cases"][0])
            with redirect_stderr(io.StringIO()):
                self.assertEqual(
                    2, report.main([str(SKIFF), "--output", str(output), "--dry-run"])
                )

    def test_response_rejects_wrong_identity_skipped_checks_and_false_readiness(self):
        baseline = response()
        _, cases = report.plan([SKIFF], [])
        variations = [
            ("rulesSha256", "wrong"),
            ("pass", 1),
            ("status", "FAIL"),
            ("issues", None),
            ("data", None),
        ]
        for field, value in variations:
            changed = copy.deepcopy(baseline)
            changed[field] = value
            with self.subTest(field=field), self.assertRaises(report.ReportError):
                report.summarize(cases[0], changed, baseline["rulesSha256"])
        for change in ("checks", "readiness", "error"):
            changed = copy.deepcopy(baseline)
            if change == "checks":
                changed["data"]["checks"]["crewRoutes"] = False
            elif change == "readiness":
                changed["data"]["definition"]["validation"]["ready"] = True
            else:
                changed["issues"] = [
                    {"code": "bad", "severity": "error", "path": "", "message": "bad"}
                ]
            with self.subTest(change=change), self.assertRaises(report.ReportError):
                report.summarize(cases[0], changed, baseline["rulesSha256"])

    def test_completed_report_uses_native_result_totals_and_explicit_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            driver = Path(directory) / "adapter"
            driver.write_bytes(b"fake test adapter")
            output = Path(directory) / "report"
            completed = subprocess.CompletedProcess(
                [], 0, json.dumps(response()).encode(), b""
            )
            with (
                patch.object(report.subprocess, "run", return_value=completed) as run,
                redirect_stdout(io.StringIO()),
            ):
                self.assertEqual(
                    0,
                    report.main(
                        [str(SKIFF), "--driver", str(driver), "--output", str(output)]
                    ),
                )
            self.assertEqual(1, run.call_count)
            self.assertEqual(str(driver.resolve()), run.call_args.args[0][0])
            data = json.loads((output / "results.json").read_text())
            self.assertIsNone(data["cases"][0]["ready"])
            self.assertEqual(100, data["cases"][0]["totals"]["cost"])
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertEqual("completed", manifest["status"])
            self.assertEqual(
                report.digest(driver.read_bytes()), manifest["driverSha256"]
            )
            self.assertTrue(manifest["schemas"])
            with (output / "results.csv").open(newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual("100", rows[0]["cost"])
            self.assertIn("does not certify", (output / "report.md").read_text())

    def test_adapter_failures_record_failed_manifest(self):
        for outcome in (
            subprocess.CompletedProcess([], 2, b"", b"bad rules"),
            subprocess.CompletedProcess([], 0, b"not json", b""),
            subprocess.TimeoutExpired("adapter", 1),
        ):
            with (
                self.subTest(outcome=type(outcome).__name__),
                tempfile.TemporaryDirectory() as directory,
            ):
                driver = Path(directory) / "adapter"
                driver.write_bytes(b"fake")
                output = Path(directory) / "report"
                kwargs = (
                    {"side_effect": outcome}
                    if isinstance(outcome, Exception)
                    else {"return_value": outcome}
                )
                with (
                    patch.object(report.subprocess, "run", **kwargs),
                    redirect_stderr(io.StringIO()),
                ):
                    self.assertEqual(
                        2,
                        report.main(
                            [
                                str(SKIFF),
                                "--driver",
                                str(driver),
                                "--output",
                                str(output),
                            ]
                        ),
                    )
                self.assertEqual(
                    "failed",
                    json.loads((output / "manifest.json").read_text())["status"],
                )
                self.assertFalse((output / "results.json").exists())

    def test_markdown_escapes_untrusted_names(self):
        self.assertEqual(
            "&lt;script&gt;&#124;&#96; line", report.markdown_text("<script>|`\nline")
        )


@unittest.skipUnless(
    os.environ.get("APSIS_SHIPYARD_VALIDATE"),
    "set APSIS_SHIPYARD_VALIDATE to native adapter",
)
class NativeReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver = Path(os.environ["APSIS_SHIPYARD_VALIDATE"]).resolve()

    def invoke(self, source, rules=RULES):
        return subprocess.run(
            [str(self.driver), str(SCHEMAS), str(rules)],
            input=source,
            capture_output=True,
            timeout=30,
            check=False,
        )

    def test_canonical_corpus_uses_existing_validator(self):
        results = {}
        for path in sorted(FIXTURES.glob("*definition-v3.json")):
            completed = self.invoke(path.read_bytes())
            self.assertEqual(0, completed.returncode, completed.stderr)
            value = json.loads(completed.stdout)
            results[path.name] = value["pass"]
            self.assertEqual(report.digest(RULES.read_bytes()), value["rulesSha256"])
            self.assertIsNone(value["data"]["definition"]["validation"]["jumpReady"])
        self.assertEqual(4, sum(results.values()))
        self.assertFalse(results["apsis-rustbucket-definition-v3.json"])

    def test_invalid_definition_is_a_measurement_but_invalid_rules_are_operational_failure(
        self,
    ):
        completed = self.invoke(b"not json")
        self.assertEqual(0, completed.returncode)
        self.assertFalse(json.loads(completed.stdout)["pass"])
        self.assertIsNone(json.loads(completed.stdout)["data"])
        completed = self.invoke(SKIFF.read_bytes(), SKIFF)
        self.assertEqual(2, completed.returncode)
        self.assertEqual("rules_loading", json.loads(completed.stderr)["error"])

    def test_imported_caches_cannot_forge_report(self):
        source = json.loads(SKIFF.read_bytes())
        baseline = json.loads(self.invoke(SKIFF.read_bytes()).stdout)
        source["derived"] = {"cost": -1, "currentMass": -1}
        source["validation"] = {"ready": True, "jumpReady": True, "issues": []}
        self.assertEqual(
            baseline, json.loads(self.invoke(json.dumps(source).encode()).stdout)
        )

    def test_load_grid_changes_measured_mass_without_writing_source(self):
        source = FIXTURES / "apsis-wren-definition-v3.json"
        before = source.read_bytes()
        with tempfile.TemporaryDirectory() as directory, redirect_stdout(io.StringIO()):
            output = Path(directory) / "loads"
            result = report.main(
                [
                    str(source),
                    "--driver",
                    str(self.driver),
                    "--output",
                    str(output),
                    "--axis",
                    "/blueprint/loads/cargo=[0,0.5,1]",
                    "--axis",
                    "/blueprint/loads/jumpFuel=[0,1]",
                ]
            )
            self.assertEqual(0, result)
            cases = json.loads((output / "results.json").read_text())["cases"]
        self.assertEqual(before, source.read_bytes())
        self.assertEqual(6, len(cases))
        self.assertTrue(all(case["structuralPass"] for case in cases))
        masses = [case["totals"]["currentMass"] for case in cases]
        self.assertGreater(masses[-1], masses[0])
        self.assertEqual(1, len({case["totals"]["cost"] for case in cases}))
        self.assertTrue(all(case["ready"] is None for case in cases))


if __name__ == "__main__":
    unittest.main()
