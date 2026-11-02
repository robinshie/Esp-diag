"""Native Comware parser/action policy with ASan + UBSan, no device mutation."""
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class H3cBdiPolicyTests(unittest.TestCase):
    def test_parsers_policy_and_real_readback_lifecycle(self):
        runtime = ROOT / "test/.runtime"
        runtime.mkdir(exist_ok=True)
        binary = runtime / "h3c_bdi_policy"
        firmware_src = ROOT / "src"
        if not firmware_src.exists():
            firmware_src = ROOT / "firmware/src"
        bdi_lib = ROOT / "lib/EmbeddedBDI/lib"
        bdi_sources = sorted(bdi_lib.rglob("*.cpp"))
        compile_result = subprocess.run([
            "clang++", "-std=c++11", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-ignored-qualifiers",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I" + str(firmware_src), "-I" + str(bdi_lib), str(ROOT / "test/h3c_bdi.cpp"),
            *map(str, bdi_sources),
            "-o", str(binary),
        ], capture_output=True, text=True)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        command = [str(binary)]
        live_capture = runtime / "ssh-queries-live.json"
        if live_capture.exists():
            records = json.loads(live_capture.read_text())
            interface = next(record["output"] for record in records if record["command"] == "interfaces")
            captured = runtime / "h3c-live-interface.txt"
            captured.write_text(interface)
            command.append(str(captured))
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("verified containment/recovery passed", result.stdout)
        replay = subprocess.run([str(binary), '--replay', str(ROOT / 'test/fixtures/h3c-r6328p03')],
                                capture_output=True, text=True)
        self.assertEqual(replay.returncode, 0, replay.stdout + replay.stderr)
        self.assertIn('five tables valid, monitoring with detector unconfigured, no writes', replay.stdout)
        discovery_replay = subprocess.run([str(binary), '--replay', str(ROOT / 'test/fixtures/h3c-r6328p03-r4')],
                                         capture_output=True, text=True)
        self.assertEqual(discovery_replay.returncode, 0, discovery_replay.stdout + discovery_replay.stderr)
        self.assertIn('valid empty history, read-only monitoring', discovery_replay.stdout)


if __name__ == "__main__":
    unittest.main()
