"""Lock the legacy wide report used alongside the rescue-debt sampler."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


AGGWIDE = Path(__file__).resolve().parents[1] / "sim" / "tools" / "aggwide.py"


def career(style, tank, *, cleared=(), opened=-1, rescue=(), stuck=False, ledger=True):
    return {
        "style": style,
        "tank": tank,
        "debtClear": list(cleared),
        "debtOpen": opened,
        "rescue": list(rescue),
        "stuck": stuck,
        "ledger": ledger,
    }


class WideReportTests(unittest.TestCase):
    def test_legacy_cli_preserves_rows_counts_and_rounded_debt_flags(self):
        data = {
            "pilotYears": 240,
            "careers": [
                career(
                    "hauler",
                    3,
                    cleared=(1081, 0, 360),
                    opened=0,
                    rescue=(1, 1, 3),
                    stuck=True,
                    ledger=False,
                ),
                career("hauler", 4, opened=1, rescue=(3,)),
                career("hauler", 6, rescue=(2, 4)),
                career("crisis", 6, cleared=(1082,)),
                career("careful", 6, cleared=(720,)),
            ],
        }
        with tempfile.TemporaryDirectory(prefix="aggwide-") as directory:
            root = Path(directory)
            (root / "pilot_wide.json").write_text(json.dumps(data), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(AGGWIDE)],
                cwd=root,
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            output = json.loads((root / "agg_wide.json").read_text(encoding="utf-8"))

        empty = {
            "careers": 0,
            "rescues": 0,
            "debts": 0,
            "med": None,
            "p90": None,
            "max": None,
            "openAtEnd": 0,
            "stuck": 0,
            "ledger": True,
        }
        crisis = dict(empty, careers=1, debts=1, med=3.01, p90=3.01, max=3.01)
        expected = {
            "hauler/3": dict(
                empty,
                careers=1,
                rescues=3,
                debts=3,
                med=1.0,
                p90=3.0,
                max=3.0,
                stuck=1,
                ledger=False,
            ),
            "hauler/4": dict(
                empty,
                careers=1,
                rescues=1,
                debts=1,
                med=0.0,
                p90=0.0,
                max=0.0,
                openAtEnd=1,
            ),
            "hauler/6": dict(empty, careers=1, rescues=2),
            "hauler/all": dict(
                empty,
                careers=3,
                rescues=6,
                debts=4,
                med=0.5,
                p90=3.0,
                max=3.0,
                openAtEnd=1,
                stuck=1,
                ledger=False,
            ),
            "crisis/3": empty,
            "crisis/4": empty,
            "crisis/6": crisis,
            "crisis/all": crisis,
            "pathfinder/3": empty,
            "pathfinder/4": empty,
            "pathfinder/6": empty,
            "pathfinder/all": empty,
            "careful/6": dict(empty, careers=1, debts=1, med=2.0, p90=2.0, max=2.0),
            "bold/6": empty,
            "reckless/6": empty,
            "payer/6": empty,
            "forgetful/6": empty,
            "double agent/6": empty,
        }
        self.assertEqual(list(output), list(expected))
        self.assertEqual(output, expected)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "careers 5 pilot-years 240")
        self.assertIn("their age so far, a lower bound", result.stdout)
        self.assertEqual(
            [tuple(line.split()[:2]) for line in lines if line.endswith(" FLAG")],
            [("crisis", "6"), ("crisis", "all")],
        )


if __name__ == "__main__":
    unittest.main()
