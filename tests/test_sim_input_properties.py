"""Run the fixed-work seeded native input properties within a CI-safe deadline."""

import json
import os
import subprocess
import unittest
from pathlib import Path

DRIVER = os.environ.get("APSIS_SIM_INPUT_PROPERTIES")
ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(
    DRIVER, "set APSIS_SIM_INPUT_PROPERTIES for native input properties"
)
class NativeInputPropertiesTests(unittest.TestCase):
    def test_seeded_properties_complete_within_fifteen_second_budget(self):
        result = subprocess.run(
            [str(Path(DRIVER).resolve())],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout),
            {
                "passed": True,
                "seed": 0x5EED1234,
                "inputCases": 128,
                "serializedActions": 8192,
                "streams": 8,
                "daysPerStream": 30,
            },
        )


if __name__ == "__main__":
    unittest.main()
