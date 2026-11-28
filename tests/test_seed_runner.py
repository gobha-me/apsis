"""Exercise the public seed-runner API without a simulation implementation."""

from fractions import Fraction
import json
import math
import unittest

from tools.seed_runner import SeedOutcome, iter_seed_runs, run_seeds, wilson_interval


class WilsonIntervalTests(unittest.TestCase):
    def test_empty_sample_has_null_bounds_and_preserves_confidence(self):
        self.assertEqual(
            wilson_interval(0, 0, 0.8),
            {"method": "wilson", "confidence": 0.8, "lower": None, "upper": None},
        )

    def test_known_95_percent_intervals(self):
        cases = (
            (0, 1, 0.0, 0.7934506856227626),
            (1, 1, 0.20654931437723745, 1.0),
            (0, 10, 0.0, 0.2775327998628892),
            (10, 10, 0.7224672001371107, 1.0),
            (5, 10, 0.236593090512564, 0.7634069094874361),
            (50, 100, 0.4038315303659956, 0.5961684696340044),
        )
        for passed, completed, lower, upper in cases:
            with self.subTest(passed=passed, completed=completed):
                interval = wilson_interval(passed, completed)
                self.assertEqual(
                    set(interval), {"method", "confidence", "lower", "upper"}
                )
                self.assertEqual(interval["method"], "wilson")
                self.assertEqual(interval["confidence"], 0.95)
                self.assertAlmostEqual(interval["lower"], lower, places=12)
                self.assertAlmostEqual(interval["upper"], upper, places=12)

    def test_custom_confidence_matches_analytic_one_sigma_interval(self):
        confidence = math.erf(1 / math.sqrt(2))
        interval = wilson_interval(1, 2, confidence)
        self.assertEqual(interval["confidence"], confidence)
        self.assertAlmostEqual(interval["lower"], 0.5 - math.sqrt(3) / 6, places=12)
        self.assertAlmostEqual(interval["upper"], 0.5 + math.sqrt(3) / 6, places=12)

    def test_wider_confidence_expands_interval(self):
        narrow = wilson_interval(3, 10, 0.8)
        wide = wilson_interval(3, 10, 0.99)
        self.assertLess(wide["lower"], narrow["lower"])
        self.assertGreater(wide["upper"], narrow["upper"])

    def test_larger_balanced_sample_narrows_interval(self):
        small = wilson_interval(5, 10)
        large = wilson_interval(500, 1000)
        self.assertGreater(large["lower"], small["lower"])
        self.assertLess(large["upper"], small["upper"])

    def test_bounds_are_finite_ordered_and_contain_observed_rate(self):
        for confidence in (0.5, 0.95, math.nextafter(1.0, 0.0)):
            for passed in range(11):
                with self.subTest(confidence=confidence, passed=passed):
                    interval = wilson_interval(passed, 10, confidence)
                    lower, upper = interval["lower"], interval["upper"]
                    self.assertTrue(math.isfinite(lower))
                    self.assertTrue(math.isfinite(upper))
                    self.assertGreaterEqual(lower, 0)
                    self.assertLessEqual(upper, 1)
                    self.assertLessEqual(lower, upper)
                    # Endpoint subtraction can leave floating-point roundoff.
                    # Interior containment and the valid range remain exact.
                    if passed == 0:
                        self.assertAlmostEqual(lower, 0.0, delta=1e-15)
                    else:
                        self.assertLessEqual(lower, passed / 10)
                    if passed == 10:
                        self.assertAlmostEqual(upper, 1.0, delta=1e-15)
                    else:
                        self.assertGreaterEqual(upper, passed / 10)

    def test_zero_passes_clamps_negative_roundoff_to_zero(self):
        # This sample leaves a negative lower bound without the clamp.
        interval = wilson_interval(0, 33, 0.5)
        self.assertEqual(interval["lower"], 0.0)

    def test_complementary_counts_have_reflected_bounds(self):
        for passed in range(8):
            with self.subTest(passed=passed):
                interval = wilson_interval(passed, 7)
                complement = wilson_interval(7 - passed, 7)
                self.assertAlmostEqual(interval["lower"], 1 - complement["upper"])
                self.assertAlmostEqual(interval["upper"], 1 - complement["lower"])

    def test_invalid_counts_are_rejected_even_for_empty_samples(self):
        cases = (
            (-1, 0),
            (0, -1),
            (2, 1),
            (1, 0),
            (True, 1),
            (0, False),
            (1.0, 2),
            (1, 2.0),
            ("1", 2),
            (0, None),
        )
        for passed, completed in cases:
            with self.subTest(passed=passed, completed=completed):
                with self.assertRaisesRegex(ValueError, "counts must be integers"):
                    wilson_interval(passed, completed)

    def test_invalid_confidence_is_rejected_even_without_observations(self):
        for confidence in (False, True, 0, 1, -0.1, 1.1, math.nan, math.inf, -math.inf):
            with self.subTest(confidence=confidence):
                with self.assertRaisesRegex(ValueError, "confidence must be"):
                    wilson_interval(0, 0, confidence)

    def test_fraction_confidence_rounding_to_endpoints_is_rejected(self):
        cases = (
            (Fraction(1, 2**1075), 0.0),
            (Fraction(2**55 - 1, 2**55), 1.0),
        )
        for confidence, rounded in cases:
            self.assertGreater(confidence, 0)
            self.assertLess(confidence, 1)
            self.assertEqual(float(confidence), rounded)
            for passed, completed in ((0, 0), (1, 2)):
                with self.subTest(rounded=rounded, completed=completed):
                    with self.assertRaisesRegex(
                        ValueError,
                        "^confidence must be a finite number between 0 and 1$",
                    ):
                        wilson_interval(passed, completed, confidence)

    def test_interior_fraction_confidence_matches_float_intervals(self):
        for confidence in (Fraction(1, 2), Fraction(4, 5), Fraction(19, 20)):
            for passed, completed in ((0, 0), (0, 3), (1, 3), (3, 3)):
                with self.subTest(
                    confidence=confidence, passed=passed, completed=completed
                ):
                    interval = wilson_interval(passed, completed, confidence)
                    self.assertEqual(
                        interval, wilson_interval(passed, completed, float(confidence))
                    )
                    self.assertIs(type(interval["confidence"]), float)
                    self.assertEqual(interval["confidence"], float(confidence))


class IterSeedRunsTests(unittest.TestCase):
    def test_reads_only_through_the_current_distinct_seed(self):
        events = []

        def seeds():
            for seed in (5, 5, 2):
                events.append(("input", seed))
                yield seed
            self.fail("an unrequested seed was read")

        def adapter(seed):
            events.append(("hook", seed))
            return {"label": f"seed {seed}"}

        runs = iter_seed_runs(seeds(), adapter)
        self.assertEqual(events, [])
        self.assertEqual(next(runs), (5, {"label": "seed 5"}))
        self.assertEqual(events, [("input", 5), ("hook", 5)])
        self.assertEqual(next(runs), (2, {"label": "seed 2"}))
        self.assertEqual(
            events,
            [("input", 5), ("hook", 5), ("input", 5), ("input", 2), ("hook", 2)],
        )

    def test_hook_failure_propagates_before_reading_the_next_seed(self):
        calls = []
        failure = RuntimeError("seed adapter failed")

        def seeds():
            yield 4
            yield 9
            self.fail("a seed after the hook failure was read")

        def adapter(seed):
            calls.append(seed)
            if seed == 9:
                raise failure
            return seed * 2

        runs = iter_seed_runs(seeds(), adapter)
        self.assertEqual(next(runs), (4, 8))
        with self.assertRaises(RuntimeError) as raised:
            next(runs)
        self.assertIs(raised.exception, failure)
        self.assertEqual(calls, [4, 9])

    def test_invalid_later_seed_respects_validation_mode(self):
        for validate_all, expected_calls in ((False, [1]), (True, [])):
            with self.subTest(validate_all=validate_all):
                calls = []
                runs = iter_seed_runs(
                    [1, 1, True, 7], calls.append, validate_all=validate_all
                )
                with self.assertRaisesRegex(TypeError, "seeds must be integers"):
                    list(runs)
                self.assertEqual(calls, expected_calls)


class RunSeedsTests(unittest.TestCase):
    def test_empty_iterable_has_exact_no_data_report(self):
        self.assertEqual(
            run_seeds(iter(()), confidence=0.8),
            {
                "schema_version": 1,
                "results": [],
                "summary": {
                    "status": "no_data",
                    "total": 0,
                    "completed": 0,
                    "passed": 0,
                    "failed": 0,
                    "skipped": 0,
                    "error": 0,
                    "pass_rate": None,
                    "pass_rate_interval": {
                        "method": "wilson",
                        "confidence": 0.8,
                        "lower": None,
                        "upper": None,
                    },
                },
            },
        )

    def test_no_hook_never_turns_placeholder_execution_into_passes(self):
        report = run_seeds([0, -7, 0, 10**30])
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(
            report["results"],
            [
                {
                    "seed": seed,
                    "status": "skipped",
                    "metrics": {},
                    "note": "No simulation hook configured.",
                }
                for seed in (0, -7, 10**30)
            ],
        )
        self.assertEqual(report["summary"]["status"], "no_data")
        self.assertEqual(report["summary"]["total"], 3)
        self.assertEqual(report["summary"]["skipped"], 3)
        self.assertEqual(report["summary"]["completed"], 0)
        self.assertEqual(report["summary"]["passed"], 0)
        self.assertEqual(report["summary"]["failed"], 0)
        self.assertEqual(report["summary"]["error"], 0)
        self.assertIsNone(report["summary"]["pass_rate"])
        self.assertIsNone(report["summary"]["pass_rate_interval"]["lower"])
        self.assertIsNone(report["summary"]["pass_rate_interval"]["upper"])

    def test_generator_deduplication_preserves_first_seen_order_and_hook_calls(self):
        calls = []

        def adapter(seed):
            calls.append(seed)
            return SeedOutcome("passed", {"seed_squared": seed * seed}, str(seed))

        report = run_seeds((seed for seed in [4, -2, 4, 0, -2]), adapter)
        self.assertEqual(calls, [4, -2, 0])
        self.assertEqual(
            report["results"],
            [
                {
                    "seed": seed,
                    "status": "passed",
                    "metrics": {"seed_squared": seed * seed},
                    "note": str(seed),
                }
                for seed in (4, -2, 0)
            ],
        )
        self.assertEqual(report["summary"]["total"], 3)
        self.assertEqual(report["summary"]["completed"], 3)
        self.assertEqual(report["summary"]["passed"], 3)
        self.assertEqual(report["summary"]["pass_rate"], 1)
        self.assertEqual(report["summary"]["status"], "passed")

    def test_mixed_results_exclude_errors_and_skips_from_statistical_denominator(self):
        def adapter(seed):
            if seed == 3:
                raise RuntimeError("private adapter detail")
            return SeedOutcome(
                {0: "passed", 1: "failed", 2: "skipped", 4: "passed"}[seed]
            )

        report = run_seeds(range(5), adapter)
        summary = report["summary"]
        self.assertEqual(set(report), {"schema_version", "results", "summary"})
        self.assertEqual(
            {
                key: value
                for key, value in summary.items()
                if key != "pass_rate_interval"
            },
            {
                "status": "failed",
                "total": 5,
                "completed": 3,
                "passed": 2,
                "failed": 1,
                "skipped": 1,
                "error": 1,
                "pass_rate": 2 / 3,
            },
        )
        interval = summary["pass_rate_interval"]
        self.assertEqual(interval["method"], "wilson")
        self.assertEqual(interval["confidence"], 0.95)
        self.assertAlmostEqual(interval["lower"], 0.2076596008020477, places=12)
        self.assertAlmostEqual(interval["upper"], 0.9385080552796037, places=12)
        self.assertEqual(
            report["results"][3],
            {
                "seed": 3,
                "status": "error",
                "metrics": {},
                "note": "Hook raised RuntimeError.",
            },
        )
        self.assertEqual(report["results"][4]["status"], "passed")
        self.assertNotIn("private adapter detail", json.dumps(report, allow_nan=False))

    def test_summary_status_precedence(self):
        cases = (
            (["passed", "passed"], "passed", 2, 1.0),
            (["passed", "skipped"], "incomplete", 1, 1.0),
            (["skipped", "skipped"], "no_data", 0, None),
            (["failed", "skipped"], "failed", 1, 0.0),
            (["passed", "error"], "failed", 1, 1.0),
            (["skipped", "error"], "failed", 0, None),
            (["error", "error"], "failed", 0, None),
        )
        for statuses, expected_status, completed, rate in cases:
            with self.subTest(statuses=statuses):

                def adapter(seed):
                    if statuses[seed] == "error":
                        raise ArithmeticError("adapter failed")
                    return SeedOutcome(statuses[seed])

                report = run_seeds(range(len(statuses)), adapter)
                summary = report["summary"]
                self.assertEqual(summary["status"], expected_status)
                self.assertEqual(summary["total"], len(statuses))
                self.assertEqual(summary["completed"], completed)
                self.assertEqual(summary["pass_rate"], rate)
                for status in ("passed", "failed", "skipped", "error"):
                    self.assertEqual(summary[status], statuses.count(status))
                self.assertEqual(
                    [record["status"] for record in report["results"]], statuses
                )
                if completed == 0:
                    self.assertIsNone(summary["pass_rate_interval"]["lower"])
                    self.assertIsNone(summary["pass_rate_interval"]["upper"])

    def test_custom_confidence_reaches_summary_interval(self):
        def adapter(seed):
            return SeedOutcome("passed" if seed == 0 else "failed")

        confidence = math.erf(1 / math.sqrt(2))
        report = run_seeds([0, 1], adapter, confidence)
        interval = report["summary"]["pass_rate_interval"]
        self.assertEqual(interval["confidence"], confidence)
        self.assertAlmostEqual(interval["lower"], 0.5 - math.sqrt(3) / 6, places=12)
        self.assertAlmostEqual(interval["upper"], 0.5 + math.sqrt(3) / 6, places=12)

    def test_fraction_confidence_matches_float_report_and_serializes(self):
        def adapter(seed):
            return SeedOutcome("passed" if seed == 0 else "failed")

        report = run_seeds([0, 1], adapter, confidence=Fraction(1, 2))
        expected = run_seeds([0, 1], adapter, confidence=0.5)
        self.assertEqual(report, expected)
        self.assertEqual(report["summary"]["completed"], 2)
        interval = report["summary"]["pass_rate_interval"]
        self.assertIs(type(interval["confidence"]), float)
        self.assertEqual(interval["confidence"], 0.5)
        self.assertLess(interval["lower"], 0.5)
        self.assertGreater(interval["upper"], 0.5)
        self.assertEqual(json.loads(json.dumps(report, allow_nan=False)), report)

    def test_fraction_confidence_is_serializable_without_observations(self):
        report = run_seeds([], confidence=Fraction(1, 2))
        interval = report["summary"]["pass_rate_interval"]
        self.assertEqual(
            interval,
            {"method": "wilson", "confidence": 0.5, "lower": None, "upper": None},
        )
        self.assertIs(type(interval["confidence"]), float)
        self.assertEqual(json.loads(json.dumps(report, allow_nan=False)), report)

    def test_fraction_confidence_rounding_to_endpoints_prevents_hook_execution(self):
        cases = (
            (Fraction(1, 2**1075), 0.0),
            (Fraction(2**55 - 1, 2**55), 1.0),
        )
        calls = []

        def adapter(seed):
            calls.append(seed)
            return SeedOutcome("passed")

        for confidence, rounded in cases:
            self.assertGreater(confidence, 0)
            self.assertLess(confidence, 1)
            self.assertEqual(float(confidence), rounded)
            for seeds in ([], [0, 1]):
                with self.subTest(rounded=rounded, seeds=seeds):
                    with self.assertRaisesRegex(
                        ValueError,
                        "^confidence must be a finite number between 0 and 1$",
                    ):
                        run_seeds(seeds, adapter, confidence=confidence)
                    self.assertEqual(calls, [])

    def test_interior_fraction_confidences_preserve_hook_results(self):
        for confidence in (Fraction(4, 5), Fraction(19, 20)):
            with self.subTest(confidence=confidence):
                calls = []

                def adapter(seed):
                    calls.append(seed)
                    return SeedOutcome("passed" if seed == 0 else "failed")

                report = run_seeds([0, 1], adapter, confidence=confidence)
                self.assertEqual(calls, [0, 1])
                self.assertEqual(report["summary"]["completed"], 2)
                self.assertEqual(
                    [row["status"] for row in report["results"]], ["passed", "failed"]
                )
                interval = report["summary"]["pass_rate_interval"]
                self.assertEqual(interval, wilson_interval(1, 2, float(confidence)))
                self.assertIs(type(interval["confidence"]), float)
                self.assertEqual(
                    json.loads(json.dumps(report, allow_nan=False)), report
                )

    def test_string_confidences_are_rejected_before_hook_execution(self):
        calls = []

        def adapter(seed):
            calls.append(seed)
            return SeedOutcome("passed")

        for confidence in ("0.5", " 0.5 ", "1/2"):
            with self.subTest(confidence=confidence):
                with self.assertRaisesRegex(ValueError, "confidence must be"):
                    run_seeds([0, 1], adapter, confidence=confidence)
                self.assertEqual(calls, [])

    def test_invalid_seeds_are_rejected_before_any_hook_runs(self):
        class IntegerSubclass(int):
            pass

        for invalid in (True, False, 1.0, "1", None, [], {}, IntegerSubclass(1)):
            with self.subTest(seed=invalid):
                calls = []

                def adapter(seed):
                    calls.append(seed)
                    return SeedOutcome("passed")

                with self.assertRaisesRegex(TypeError, "seeds must be integers"):
                    run_seeds([0, invalid], adapter)
                self.assertEqual(calls, [])

    def test_invalid_confidence_is_rejected_before_hook_execution(self):
        calls = []

        def adapter(seed):
            calls.append(seed)
            return SeedOutcome("passed")

        for confidence in (False, True, 0, 1, -1, 2, math.nan, math.inf, -math.inf):
            with self.subTest(confidence=confidence):
                with self.assertRaisesRegex(ValueError, "confidence must be"):
                    run_seeds([1], adapter, confidence)
                self.assertEqual(calls, [])

    def test_string_confidence_raises_value_error(self):
        with self.assertRaisesRegex(ValueError, "confidence must be"):
            run_seeds([1], confidence="0.5")

    def test_invalid_outcomes_become_explicit_errors_and_execution_continues(self):
        cases = (
            (None, "AttributeError"),
            ({"status": "passed", "metrics": {}, "note": None}, "AttributeError"),
            (SeedOutcome("error"), "ValueError"),
            (SeedOutcome("PASSED"), "ValueError"),
            (SeedOutcome(None), "ValueError"),
            (SeedOutcome("passed", []), "TypeError"),
            (SeedOutcome("passed", None), "TypeError"),
            (SeedOutcome("passed", {1: 2}), "TypeError"),
            (SeedOutcome("passed", {"x": True}), "TypeError"),
            (SeedOutcome("passed", {"x": "2"}), "TypeError"),
            (SeedOutcome("passed", {"x": None}), "TypeError"),
            (SeedOutcome("passed", {"x": math.nan}), "ValueError"),
            (SeedOutcome("passed", {"x": math.inf}), "ValueError"),
            (SeedOutcome("passed", {"x": -math.inf}), "ValueError"),
            (SeedOutcome("skipped", {"x": math.nan}), "ValueError"),
            (SeedOutcome("failed", note=2), "TypeError"),
        )
        for outcome, exception_name in cases:
            with self.subTest(outcome=outcome):
                calls = []

                def adapter(seed):
                    calls.append(seed)
                    return outcome if seed == 0 else SeedOutcome("passed")

                report = run_seeds([0, 1], adapter)
                self.assertEqual(calls, [0, 1])
                self.assertEqual(
                    report["results"],
                    [
                        {
                            "seed": 0,
                            "status": "error",
                            "metrics": {},
                            "note": f"Hook raised {exception_name}.",
                        },
                        {"seed": 1, "status": "passed", "metrics": {}, "note": None},
                    ],
                )
                self.assertEqual(report["summary"]["status"], "failed")
                self.assertEqual(report["summary"]["error"], 1)
                self.assertEqual(report["summary"]["completed"], 1)
                self.assertEqual(report["summary"]["passed"], 1)
                self.assertEqual(report["summary"]["pass_rate"], 1)
                json.dumps(report, allow_nan=False)

    def test_valid_metrics_and_notes_round_trip_without_normalization(self):
        metrics = {"zero": 0, "negative": -3, "fraction": 0.125, "Δ": -0.0}
        note = "observed | <value>\nΔ"

        def adapter(seed):
            return SeedOutcome("passed", metrics, note)

        report = run_seeds([1], adapter)
        self.assertEqual(
            report["results"],
            [{"seed": 1, "status": "passed", "metrics": metrics, "note": note}],
        )
        self.assertEqual(json.loads(json.dumps(report, allow_nan=False)), report)

    def test_metric_records_copy_mutable_adapter_dictionary(self):
        metrics = {}

        def adapter(seed):
            metrics["value"] = seed
            return SeedOutcome("passed", metrics)

        report = run_seeds([1, 2], adapter)
        self.assertEqual(report["results"][0]["metrics"], {"value": 1})
        self.assertEqual(report["results"][1]["metrics"], {"value": 2})
        metrics["value"] = 99
        self.assertEqual(report["results"][0]["metrics"], {"value": 1})
        self.assertEqual(report["results"][1]["metrics"], {"value": 2})
        self.assertIsNot(
            report["results"][0]["metrics"], report["results"][1]["metrics"]
        )

    def test_default_metric_dictionaries_are_independent(self):
        first = SeedOutcome("passed")
        second = SeedOutcome("passed")
        first.metrics["value"] = 1
        self.assertEqual(second.metrics, {})

    def test_process_control_exceptions_are_not_swallowed(self):
        for exception_type in (KeyboardInterrupt, SystemExit):
            with self.subTest(exception_type=exception_type):

                def adapter(seed):
                    raise exception_type("stop execution")

                with self.assertRaises(exception_type):
                    run_seeds([1], adapter)


if __name__ == "__main__":
    unittest.main()
