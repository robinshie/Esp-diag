from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PortShutdownTests(unittest.TestCase):
    def test_only_shutdown_port_two_and_confirm_twice(self):
        binary = ROOT / 'test/.runtime/port_shutdown'
        command = ['clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   str(ROOT / 'test/port_shutdown.cpp'), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('10 single-port shutdown safety scenarios passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
