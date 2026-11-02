"""Exercise the firmware heartbeat policy and actual Embedded-BDI plans."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HeartbeatBdiTests(unittest.TestCase):
    def test_detection_recovery_gating_and_fault_isolation(self):
        binary = ROOT / "test/.runtime/heartbeat_bdi"
        binary.parent.mkdir(parents=True, exist_ok=True)
        sources = sorted((ROOT / "lib/EmbeddedBDI/lib").rglob("*.cpp"))
        subprocess.run([
            "clang++", "-std=c++11", "-g", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-Wno-ignored-qualifiers",
            "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/EmbeddedBDI/lib"),
            str(ROOT / "test/switch_heartbeat_bdi.cpp"), *map(str, sources),
            "-o", str(binary),
        ], check=True, capture_output=True, text=True)
        subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
