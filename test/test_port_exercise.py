from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PortExerciseTests(unittest.TestCase):
    def test_fixed_ports_once_only_and_readback(self):
        binary = ROOT / 'test/.runtime/port_exercise'
        result = subprocess.run(['clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
                                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                 str(ROOT / 'test/port_exercise.cpp'), '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('10 one-shot physical port exercise safety scenarios passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
