"""Exercise the report-table command with actual processes and files."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
REPORT = {
    "columns": ["Metric", "Baseline", "Candidate"],
    "rows": [["rate", "95.00%", "96.00%"]],
}
EXPECTED = (
    "| Metric | Baseline | Candidate |\n"
    "| --- | --- | --- |\n"
    "| rate | 95.00% | 96.00% |\n"
)


class ReportTableCliTests(unittest.TestCase):
    def run_cli(self, *arguments, source=""):
        return subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools" / "report_table.py"), *arguments],
            input=source,
            capture_output=True,
            encoding="utf-8",
            cwd=REPO_ROOT,
            timeout=10,
            check=False,
        )

    def assert_input_error(self, result, diagnostic):
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("usage:", result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_default_stdin_and_explicit_dash_emit_exact_table_and_newline(self):
        for arguments in ((), ("-",)):
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments, source=json.dumps(REPORT))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, EXPECTED)
                self.assertEqual(result.stderr, "")

    def test_utf8_file_is_used_instead_of_stdin(self):
        report = {"columns": ["Métric", "基準", "Candidate"], "rows": [["Δ", "🎲", ""]]}
        with tempfile.TemporaryDirectory(prefix="report-table-tests-") as directory:
            path = Path(directory) / "comparison with spaces.json"
            path.write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
            result = self.run_cli(str(path), source="not JSON")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "| Métric | 基準 | Candidate |\n| --- | --- | --- |\n| Δ | 🎲 |  |\n",
        )
        self.assertEqual(result.stderr, "")

    def test_module_entry_point_runs_real_renderer(self):
        result = subprocess.run(
            [sys.executable, "-m", "tools.report_table"],
            input=json.dumps(REPORT),
            capture_output=True,
            encoding="utf-8",
            cwd=REPO_ROOT,
            timeout=10,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, EXPECTED)
        self.assertEqual(result.stderr, "")

    def test_escaping_is_applied_after_json_decoding(self):
        report = {
            "columns": ["Metric", "<base>", "A|B"],
            "rows": [["\\| & <br>", "a\r\nb\rc\nd", ""]],
        }
        result = self.run_cli(source=json.dumps(report))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "| Metric | &lt;base&gt; | A&#124;B |\n| --- | --- | --- |\n"
            "| &#92;&#124; &amp; &lt;br&gt; | a<br>b<br>c<br>d |  |\n",
        )
        self.assertEqual(result.stderr, "")

    def test_empty_row_list_emits_header_only_with_one_trailing_newline(self):
        result = self.run_cli(
            source=json.dumps({"columns": REPORT["columns"], "rows": []})
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "| Metric | Baseline | Candidate |\n| --- | --- | --- |\n"
        )
        self.assertEqual(result.stderr, "")

    def test_malformed_json_has_usage_error_and_no_partial_table(self):
        for source in ("", "{", '{"columns": []', '{"x": 1} trailing', "{bad}"):
            with self.subTest(source=source):
                self.assert_input_error(self.run_cli(source=source), "error:")

    def test_non_object_json_is_rejected(self):
        for value in (None, [], [REPORT], "text", 42, 1.5, True):
            with self.subTest(value=value):
                self.assert_input_error(
                    self.run_cli(source=json.dumps(value)),
                    "input must be an object with columns and rows",
                )

    def test_missing_fields_and_invalid_headers_are_rejected(self):
        for report in (
            {},
            {"rows": []},
            {"columns": []},
            {"columns": ["M", "B"]},
            {"columns": ["M", " ", "C"], "rows": []},
        ):
            with self.subTest(report=report):
                self.assert_input_error(
                    self.run_cli(source=json.dumps(report)), "columns must contain"
                )
        self.assert_input_error(
            self.run_cli(source=json.dumps({"columns": REPORT["columns"]})),
            "rows must be a list",
        )

    def test_invalid_rows_and_cells_are_rejected_without_partial_output(self):
        for rows, diagnostic in (
            (None, "rows must be a list"),
            ({}, "rows must be a list"),
            (["abc"], "row 1 must contain 3 string cells"),
            ([["a", "b"]], "row 1 must contain 3 string cells"),
            ([["a", False, "c"]], "row 1 must contain 3 string cells"),
            ([["a", None, "c"]], "row 1 must contain 3 string cells"),
            ([["a", 1.25, "c"]], "row 1 must contain 3 string cells"),
            (REPORT["rows"] + [["bad"]], "row 2 must contain 3 string cells"),
        ):
            with self.subTest(rows=rows):
                self.assert_input_error(
                    self.run_cli(
                        source=json.dumps({"columns": REPORT["columns"], "rows": rows})
                    ),
                    diagnostic,
                )

    def test_missing_file_and_directory_are_reported_as_input_errors(self):
        with tempfile.TemporaryDirectory(prefix="report-table-tests-") as directory:
            missing = Path(directory) / "absent.json"
            self.assert_input_error(
                self.run_cli(str(missing)), "No such file or directory"
            )
            self.assert_input_error(self.run_cli(directory), "Is a directory")

    def test_non_utf8_file_is_reported_without_traceback(self):
        with tempfile.TemporaryDirectory(prefix="report-table-tests-") as directory:
            path = Path(directory) / "invalid-utf8.json"
            path.write_bytes(b"\xff\xfe")
            result = self.run_cli(str(path))
        self.assert_input_error(result, "utf-8")

    def test_extra_arguments_are_rejected(self):
        self.assert_input_error(
            self.run_cli("-", "extra.json"), "unrecognized arguments"
        )


if __name__ == "__main__":
    unittest.main()
