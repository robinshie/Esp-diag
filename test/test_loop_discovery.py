from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LoopDiscoveryTest(unittest.TestCase):
    def test_actual_bdi_agent_and_multi_condition_discovery(self):
        library = ROOT / 'lib/EmbeddedBDI/lib'
        with tempfile.TemporaryDirectory() as directory:
            binary = str(Path(directory) / 'loop_discovery')
            compile_result = subprocess.run([
                'clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-ignored-qualifiers',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(library),
                str(ROOT / 'test/loop_discovery.cpp'), *map(str, sorted(library.rglob('*.cpp'))),
                '-o', binary], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('false-positive gates passed', result.stdout)
