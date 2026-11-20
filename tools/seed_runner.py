"""Seed execution records and pass-rate summaries, without a simulation."""

import argparse
from contextlib import contextmanager, redirect_stdout
from dataclasses import dataclass, field
import importlib
import json
import math
from numbers import Real
import os
import re
from statistics import NormalDist
import sys
from typing import Callable, Iterable, Literal


@dataclass(frozen=True)
class SeedOutcome:
    """Value returned by a future simulation adapter for one integer seed."""

    status: Literal["passed", "failed", "skipped"]
    metrics: dict[str, int | float] = field(default_factory=dict)
    note: str | None = None


SeedHook = Callable[[int], SeedOutcome]


def _validate_confidence(confidence: Real) -> None:
    if (
        isinstance(confidence, bool)
        or not isinstance(confidence, Real)
        or not 0 < confidence < 1
    ):
        raise ValueError("confidence must be a finite number between 0 and 1")


def wilson_interval(
    passed: int, completed: int, confidence: Real = 0.95
) -> dict[str, str | float | None]:
    """Two-sided Wilson score interval for a binary pass rate; null if empty."""
    _validate_confidence(confidence)
    if (
        type(passed) is not int
        or type(completed) is not int
        or not 0 <= passed <= completed
    ):
        raise ValueError("counts must be integers with 0 <= passed <= completed")
    interval = {
        "method": "wilson",
        "confidence": float(confidence),
        "lower": None,
        "upper": None,
    }
    if not completed:
        return interval
    # The lower tail avoids rounding (1 + confidence) / 2 up to exactly 1.
    z = -NormalDist().inv_cdf((1 - confidence) / 2)
    rate = passed / completed
    denominator = 1 + z * z / completed
    center = (rate + z * z / (2 * completed)) / denominator
    radius = (
        z
        * math.sqrt(rate * (1 - rate) / completed + z * z / (4 * completed * completed))
        / denominator
    )
    interval["lower"] = max(0.0, center - radius)
    interval["upper"] = min(1.0, center + radius)
    return interval


def _validate_outcome(outcome: SeedOutcome) -> None:
    # Validate the record fields rather than class identity: a CLI started with
    # -m runs as __main__, while an adapter imports tools.seed_runner.
    if outcome.status not in ("passed", "failed", "skipped"):
        raise ValueError("invalid outcome status")
    if not isinstance(outcome.metrics, dict):
        raise TypeError("metrics must be a dictionary")
    for name, value in outcome.metrics.items():
        if not isinstance(name, str) or type(value) not in (int, float):
            raise TypeError("metrics must have string names and numeric values")
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("metric values must be finite")
    if outcome.note is not None and not isinstance(outcome.note, str):
        raise TypeError("note must be a string or None")


def run_seeds(
    seeds: Iterable[int], hook: SeedHook | None = None, confidence: Real = 0.95
) -> dict[str, object]:
    """Run each distinct seed in input order; adapter errors remain explicit."""
    _validate_confidence(confidence)
    unique_seeds = []
    seen = set()
    for seed in seeds:
        if type(seed) is not int:
            raise TypeError("seeds must be integers")
        if seed not in seen:
            seen.add(seed)
            unique_seeds.append(seed)

    results = []
    counts = dict.fromkeys(("passed", "failed", "skipped", "error"), 0)
    for seed in unique_seeds:
        try:
            outcome = (
                hook(seed)
                if hook is not None
                else SeedOutcome("skipped", note="No simulation hook configured.")
            )
            _validate_outcome(outcome)
            record = {
                "seed": seed,
                "status": outcome.status,
                "metrics": dict(outcome.metrics),
                "note": outcome.note,
            }
        except Exception as exc:
            # Keep adapter errors outside the statistical sample. Exception text
            # can contain adapter-specific data, so record only its type.
            record = {
                "seed": seed,
                "status": "error",
                "metrics": {},
                "note": f"Hook raised {type(exc).__name__}.",
            }
        results.append(record)
        counts[record["status"]] += 1

    completed = counts["passed"] + counts["failed"]
    if counts["failed"] or counts["error"]:
        status = "failed"
    elif not completed:
        status = "no_data"
    elif counts["skipped"]:
        status = "incomplete"
    else:
        status = "passed"
    return {
        "schema_version": 1,
        "results": results,
        "summary": {
            "status": status,
            "total": len(results),
            "completed": completed,
            **counts,
            "pass_rate": counts["passed"] / completed if completed else None,
            "pass_rate_interval": wilson_interval(
                counts["passed"], completed, confidence
            ),
        },
    }


def _parse_seed(value: str) -> int:
    try:
        if re.fullmatch(r"[+-]?[0-9]+", value) is None:
            raise ValueError
        return int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid int value: {value!r}") from exc


def _parse_confidence(value: str) -> float:
    try:
        if value != value.strip():
            raise ValueError
        return float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid float value: {value!r}") from exc


def _write_cli_output(fd: int, message: str) -> None:
    # Use unbuffered writes so a failed pipe is not retried at interpreter exit.
    remaining = memoryview(message.encode("utf-8", errors="backslashreplace"))
    while remaining:
        written = os.write(fd, remaining)
        if not written:
            raise OSError("cannot write CLI output")
        remaining = remaining[written:]


class _CliArgumentParser(argparse.ArgumentParser):
    def _print_message(self, message, file=None):
        if message:
            _write_cli_output(1 if file is sys.stdout else 2, message)


def _flush_cli_stream(stream) -> None:
    if stream is None:
        raise OSError("CLI output stream is unavailable")
    try:
        stream.flush()
    except (OSError, ValueError) as exc:
        # A failed hook-output flush must not be retried after fd 1 is restored.
        try:
            stream.close()
        except (OSError, ValueError):
            pass
        raise OSError("cannot flush CLI output") from exc


@contextmanager
def _cli_stdout():
    _flush_cli_stream(sys.stdout)
    # Check stderr before dup(1) can reuse a closed fd 2.
    os.fstat(2)
    original_stdout = os.dup(1)
    try:
        os.dup2(2, 1)
        yield original_stdout
    finally:
        try:
            os.dup2(original_stdout, 1)
        finally:
            os.close(original_stdout)


def _run_cli_hooks(parser, args) -> dict[str, object] | None:
    hook = None
    if args.hook is not None:
        module, _, name = args.hook.partition(":")
        try:
            hook = getattr(importlib.import_module(module), name)
        except Exception as exc:
            parser.error(f"hook raised {type(exc).__name__}.")
        except BaseException as exc:
            _write_cli_output(2, f"seed runner: hook raised {type(exc).__name__}.\n")
            return None
        if not callable(hook):
            parser.error("hook must be callable")
    try:
        return run_seeds(args.seeds, hook, args.confidence)
    except BaseException as exc:
        _write_cli_output(2, f"seed runner: hook raised {type(exc).__name__}.\n")
        return None


def _main() -> int:
    parser = _CliArgumentParser(description=__doc__)
    parser.add_argument("--seeds", type=_parse_seed, nargs="+", required=True)
    parser.add_argument("--confidence", type=_parse_confidence, default=0.95)
    parser.add_argument("--hook", help="trusted Python adapter as module:function")
    args = parser.parse_args()
    try:
        _validate_confidence(args.confidence)
    except ValueError as exc:
        parser.error(str(exc))
    if args.hook is not None:
        module, separator, name = args.hook.partition(":")
        if not separator or not module or not name or ":" in name:
            parser.error("hook must have the form module:function")
    stdout = sys.stdout
    with _cli_stdout() as report_fd:
        try:
            with redirect_stdout(sys.stderr):
                report = _run_cli_hooks(parser, args)
        finally:
            # Drain even writes through sys.__stdout__ before fd 1 is restored
            # or any JSON is emitted through the saved original stdout.
            try:
                _flush_cli_stream(stdout)
            finally:
                _flush_cli_stream(sys.stderr)
        if report is None:
            return 2
        try:
            document = json.dumps(report, indent=2, sort_keys=True, allow_nan=False)
        except (TypeError, ValueError, OverflowError) as exc:
            _write_cli_output(
                2,
                f"seed runner: cannot serialize report ({type(exc).__name__}).\n",
            )
            return 2
        _write_cli_output(report_fd, document + "\n")
    status = report["summary"]["status"]
    return 0 if status == "passed" else 1 if status == "failed" else 2


def main() -> int:
    try:
        return _main()
    except OSError:
        # The report and diagnostics both use raw writes, with nothing buffered
        # for Python to flush again after a broken or closed output pipe.
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
