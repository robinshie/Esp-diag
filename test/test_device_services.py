"""Production BOOT, SSH identity/worker and on-device analysis regression."""
from pathlib import Path
import json
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DeviceServicesTests(unittest.TestCase):
    def compile_run(self, name, extra=()):
        binary = ROOT / "test/.runtime" / name
        binary.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["clang++", "-std=c++11", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-Wno-deprecated-declarations", "-I" + str(ROOT / "src"), *extra, str(ROOT / f"test/{name}.cpp"), "-o", str(binary)], check=True, capture_output=True, text=True)
        return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout

    def test_native_settings_and_wire_formats(self):
        for name in ("network_settings", "mqtt_settings", "switch_settings", "setup_settings", "ssh_commands", "mqtt_report_cursor", "journal_files", "archive_frame", "fault_journal"):
            with self.subTest(name=name):
                self.compile_run(name)

    def test_boot_triple_and_hold(self):
        self.compile_run("boot_gesture")

    def test_auto_trust_persistence_and_rotation(self):
        self.compile_run("ssh_host_key")

    def test_worker_authentication_commands_and_failure_boundaries(self):
        self.compile_run("switch_ssh", ["-I" + str(ROOT / "test/ssh_stubs"), "-I" + str(ROOT / ".pio/libdeps/esp32-s3-devkitc-1/LibSSH-ESP32/src")])

    def test_device_analysis_dedup_redaction_and_retry(self):
        rows = [json.loads(line) for line in self.compile_run("device_analysis").splitlines()]
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]["source"], "h3c_ssh")
        self.assertEqual(rows[0]["cycle"], 1)
        self.assertGreaterEqual(rows[0]["score"], 0)
        self.assertLessEqual(rows[0]["score"], 1)
        self.assertIn("REDACTED", rows[2]["evidence"])
