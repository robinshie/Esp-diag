from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BdiPortExerciseTests(unittest.TestCase):
    def test_operator_goal_real_plans_and_executor_guards(self):
        library = ROOT / 'lib/EmbeddedBDI/lib'
        binary = ROOT / 'test/.runtime/bdi_port_exercise'
        result = subprocess.run([
            'clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-ignored-qualifiers', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-DPORT_EXERCISE_BDI_TEST=1',
            '-I' + str(library), str(ROOT / 'test/port_exercise.cpp'),
            *map(str, sorted(library.rglob('*.cpp'))), '-o', str(binary),
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('17 BDI operator acceptance safety scenarios passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
