import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LocalReportTest(unittest.TestCase):
    def test_actual_report_schema_bounds_redaction_and_uptime(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / "report")
            subprocess.run(["c++", "-std=c++11", "-fsanitize=address,undefined", "test/local_report.cpp", "-o", binary], cwd=ROOT, check=True)
            report = json.loads(subprocess.check_output([binary], cwd=ROOT, text=True))
        self.assertEqual(report["schema_version"], 1)
        self.assertEqual(report["source"], "esp_native")
        self.assertEqual(report["transport"], "ETH")
        self.assertEqual(report["clock"], "uptime")
        self.assertEqual(report["report_id"], "real-boot-7")
        self.assertEqual(report["sent_at_ms"], 1250)
        self.assertEqual(report["lines"][0]["at_ms"], 1234)
        self.assertNotIn("secret", json.dumps(report))
