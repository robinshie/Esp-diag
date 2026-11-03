from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LoopAutoTests(unittest.TestCase):
    def test_authorized_ports_real_bdi_and_shutdown_readback(self):
        runtime = ROOT / 'test/.runtime'
        runtime.mkdir(exist_ok=True)
        library = ROOT / 'lib/EmbeddedBDI/lib'
        binary = runtime / 'loop_auto'
        compiled = subprocess.run([
            'clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-ignored-qualifiers', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-DESP_LOOP_AUTO=1',
            '-I' + str(library), str(ROOT / 'test/loop_auto.cpp'),
            *map(str, sorted(library.rglob('*.cpp'))), '-o', str(binary),
        ], capture_output=True, text=True)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        executed = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
        self.assertIn('17 live-loop authorization and automatic isolation scenarios passed', executed.stdout)


if __name__ == '__main__':
    unittest.main()
