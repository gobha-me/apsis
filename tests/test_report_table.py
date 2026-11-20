"""Contract and adversarial input tests for the actual Markdown renderer."""

import copy
import unittest

from tools.report_table import render_table


class RenderTableTests(unittest.TestCase):
    def test_documented_example_has_exact_format_without_trailing_newline(self):
        self.assertEqual(
            render_table(
                ["Metric", "Baseline", "Candidate A"],
                [["Run label", "pending", "pending"], ["Pass rate (95% CI)", "", ""]],
            ),
            "| Metric | Baseline | Candidate A |\n"
            "| --- | --- | --- |\n"
            "| Run label | pending | pending |\n"
            "| Pass rate (95% CI) |  |  |",
        )

    def test_empty_rows_still_render_header_and_separator(self):
        self.assertEqual(
            render_table(["Metric", "Before", "After"], []),
            "| Metric | Before | After |\n| --- | --- | --- |",
        )

    def test_column_and_row_order_with_multiple_comparisons(self):
        self.assertEqual(
            render_table(
                ["Metric", "Z baseline", "B candidate", "A candidate"],
                [["Z metric", "3", "2", "1"], ["A metric", "6", "5", "4"]],
            ),
            "| Metric | Z baseline | B candidate | A candidate |\n"
            "| --- | --- | --- | --- |\n"
            "| Z metric | 3 | 2 | 1 |\n"
            "| A metric | 6 | 5 | 4 |",
        )

    def test_escaping_applies_to_headers_and_data_without_creating_cells(self):
        table = render_table(
            ["M|etric", "<base>&", "back\\slash"],
            [["<script>&</script>", "left|right", "\\|"]],
        )
        self.assertEqual(
            table,
            "| M&#124;etric | &lt;base&gt;&amp; | back&#92;slash |\n"
            "| --- | --- | --- |\n"
            "| &lt;script&gt;&amp;&lt;/script&gt; | left&#124;right | &#92;&#124; |",
        )
        for line in table.splitlines():
            self.assertEqual(line.count("|"), 4)

    def test_existing_entities_are_escaped_as_literal_user_text(self):
        self.assertEqual(
            render_table(
                ["Metric", "Base", "Candidate"], [["&amp;", "&#124;", "<br>"]]
            ),
            "| Metric | Base | Candidate |\n| --- | --- | --- |\n"
            "| &amp;amp; | &amp;#124; | &lt;br&gt; |",
        )

    def test_all_line_endings_stay_in_their_original_cell(self):
        for ending in ("\n", "\r", "\r\n"):
            with self.subTest(ending=repr(ending)):
                self.assertEqual(
                    render_table(
                        [f"M{ending}etric", "Base", "Candidate"],
                        [[f"a{ending}b{ending}", "", ""]],
                    ),
                    "| M<br>etric | Base | Candidate |\n"
                    "| --- | --- | --- |\n| a<br>b<br> |  |  |",
                )
        self.assertEqual(
            render_table(["M", "B", "C"], [["a\r\nb\rc\nd", "", ""]]),
            "| M | B | C |\n| --- | --- | --- |\n| a<br>b<br>c<br>d |  |  |",
        )

    def test_unicode_markdown_quotes_and_whitespace_are_preserved(self):
        self.assertEqual(
            render_table(
                ["  Métric  ", "基準", "Δ candidate"],
                [
                    [
                        "**bold** _italic_ `code`",
                        "[link](target)",
                        "'quoted' \"text\" 🎲",
                    ]
                ],
            ),
            "|   Métric   | 基準 | Δ candidate |\n| --- | --- | --- |\n"
            "| **bold** _italic_ `code` | [link](target) | 'quoted' \"text\" 🎲 |",
        )

    def test_preformatted_values_and_empty_data_cells_are_not_normalized(self):
        self.assertEqual(
            render_table(
                ["Metric", "Baseline", "Candidate"],
                [["", "", ""], ["rate", "001.230 % [0.1, 0.9]", " -0.00 ms "]],
            ),
            "| Metric | Baseline | Candidate |\n| --- | --- | --- |\n"
            "|  |  |  |\n| rate | 001.230 % [0.1, 0.9] |  -0.00 ms  |",
        )

    def test_render_does_not_mutate_supplied_lists(self):
        columns = ["M|etric", "Base", "Candidate"]
        rows = [["a\nb", "<x>", "\\"]]
        original_columns, original_rows = copy.deepcopy((columns, rows))
        first = render_table(columns, rows)
        self.assertEqual(columns, original_columns)
        self.assertEqual(rows, original_rows)
        self.assertEqual(render_table(columns, rows), first)

    def test_rejects_non_list_columns_and_fewer_than_three_labels(self):
        for columns in (
            None,
            "ABC",
            ("M", "B", "C"),
            {},
            3,
            True,
            [],
            ["M"],
            ["M", "B"],
        ):
            with self.subTest(columns=columns):
                with self.assertRaisesRegex(ValueError, "columns must contain"):
                    render_table(columns, [])

    def test_rejects_blank_and_non_string_labels_in_every_position(self):
        for index in range(3):
            for label in ("", " ", "\t\r\n", "\u2003", None, 0, False, [], {}):
                with self.subTest(index=index, label=label):
                    columns = ["Metric", "Base", "Candidate"]
                    columns[index] = label
                    with self.assertRaisesRegex(ValueError, "columns must contain"):
                        render_table(columns, [])

    def test_rejects_non_list_rows(self):
        for rows in (None, "", (), {}, 1, False):
            with self.subTest(rows=rows):
                with self.assertRaisesRegex(ValueError, "rows must be a list"):
                    render_table(["M", "B", "C"], rows)

    def test_rejects_non_list_rows_and_incorrect_row_width(self):
        for row in (
            None,
            "abc",
            ("a", "b", "c"),
            {},
            3,
            False,
            [],
            ["a"],
            ["a", "b"],
            ["a", "b", "c", "d"],
        ):
            with self.subTest(row=row):
                with self.assertRaisesRegex(
                    ValueError, "^row 1 must contain 3 string cells$"
                ):
                    render_table(["M", "B", "C"], [row])

    def test_rejects_non_string_cells_in_every_position(self):
        for index in range(3):
            for value in (None, 0, 1.25, False, [], {}, float("nan")):
                with self.subTest(index=index, value=value):
                    row = ["a", "b", "c"]
                    row[index] = value
                    with self.assertRaisesRegex(
                        ValueError, "^row 1 must contain 3 string cells$"
                    ):
                        render_table(["M", "B", "C"], [row])

    def test_error_reports_one_based_index_and_actual_column_count(self):
        with self.assertRaisesRegex(ValueError, "^row 3 must contain 4 string cells$"):
            render_table(
                ["Metric", "Base", "A", "B"],
                [["a", "1", "2", "3"], ["b", "4", "5", "6"], ["bad", "7", "8"]],
            )

    def test_code_spans_keep_literal_character_references_and_line_break_markup(self):
        self.assertEqual(
            render_table(
                ["`M|etric`", "`B&<>`", "`C\\andidate`"],
                [
                    ["`x|y`", "`a&b <x> \\path`", "`one\r\ntwo\rthree\nfour`"],
                    ["``a`b|c``", "`&#124;`", "before `x|y` after"],
                ],
            ),
            "| `M&#124;etric` | `B&amp;&lt;&gt;` | `C&#92;andidate` |\n"
            "| --- | --- | --- |\n"
            "| `x&#124;y` | `a&amp;b &lt;x&gt; &#92;path` | `one<br>two<br>three<br>four` |\n"
            "| ``a`b&#124;c`` | `&amp;#124;` | before `x&#124;y` after |",
        )

    def test_nul_tab_and_unicode_line_separator_are_escaped_in_every_column(self):
        for character, reference in (
            ("\x00", "&#0;"),
            ("\t", "&#9;"),
            ("\u2028", "&#8232;"),
        ):
            for index in range(3):
                with self.subTest(character=ascii(character), index=index):
                    columns = ["Metric", "Baseline", "Candidate"]
                    row = ["rate", "95.00%", "96.00%"]
                    columns[index] = f"before{character}after"
                    row[index] = f"left{character}right"
                    table = render_table(columns, [row])
                    expected_headers = ["Metric", "Baseline", "Candidate"]
                    expected_cells = ["rate", "95.00%", "96.00%"]
                    expected_headers[index] = f"before{reference}after"
                    expected_cells[index] = f"left{reference}right"
                    self.assertEqual(
                        table,
                        "| "
                        + " | ".join(expected_headers)
                        + " |\n| --- | --- | --- |\n| "
                        + " | ".join(expected_cells)
                        + " |",
                    )
                    self.assertNotIn(character, table)


if __name__ == "__main__":
    unittest.main()
