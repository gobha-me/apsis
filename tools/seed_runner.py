"""Seed execution records and pass-rate summaries, without a simulation."""

import argparse
from contextlib import redirect_stdout
from dataclasses import dataclass, field
import importlib
import json
import math
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


def _validate_confidence(confidence: float) -> None:
    if type(confidence) not in (int, float) or not 0 < confidence < 1:
        raise ValueError("confidence must be a finite number between 0 and 1")


def wilson_interval(
    passed: int, completed: int, confidence: float = 0.95
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
        "confidence": confidence,
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
    seeds: Iterable[int], hook: SeedHook | None = None, confidence: float = 0.95
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", type=_parse_seed, nargs="+", required=True)
    parser.add_argument("--confidence", type=float, default=0.95)
    parser.add_argument("--hook", help="trusted Python adapter as module:function")
    args = parser.parse_args()
    stdout = sys.stdout
    hook = None
    try:
        _validate_confidence(args.confidence)
        if args.hook is not None:
            module, separator, name = args.hook.partition(":")
            if not separator or not module or not name or ":" in name:
                raise ValueError("hook must have the form module:function")
            with redirect_stdout(sys.stderr):
                hook = getattr(importlib.import_module(module), name)
            if not callable(hook):
                raise TypeError("hook must be callable")
    except Exception as exc:
        parser.error(str(exc))
    except (SystemExit, KeyboardInterrupt) as exc:
        print(f"seed runner: hook raised {type(exc).__name__}.", file=sys.stderr)
        return 2
    try:
        with redirect_stdout(sys.stderr):
            report = run_seeds(args.seeds, hook, args.confidence)
    except (SystemExit, KeyboardInterrupt) as exc:
        print(f"seed runner: hook raised {type(exc).__name__}.", file=sys.stderr)
        return 2
    try:
        document = json.dumps(report, indent=2, sort_keys=True, allow_nan=False)
    except (TypeError, ValueError, OverflowError) as exc:
        print(
            f"seed runner: cannot serialize report ({type(exc).__name__}).",
            file=sys.stderr,
        )
        return 2
    print(document, file=stdout)
    status = report["summary"]["status"]
    return 0 if status == "passed" else 1 if status == "failed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
