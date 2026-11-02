from pathlib import Path
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StrategyCompositionTests(unittest.TestCase):
    def test_strategy_and_device_replacement(self):
        library = ROOT / 'lib/EmbeddedBDI/lib'
        with tempfile.TemporaryDirectory() as temporary:
            binary = str(Path(temporary) / 'strategy_composition')
            result = subprocess.run([
                'clang++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wno-ignored-qualifiers',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I' + str(library),
                str(ROOT / 'test/strategy_composition.cpp'), *map(str, sorted(library.rglob('*.cpp'))),
                '-o', binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('device replacement, independent authorization and lifecycle passed', result.stdout)
            status = json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{')))
            self.assertEqual(status['device_profile'], 'test-fixture-switch')
            self.assertEqual(status['strategy_ids'], ['loop_discovery', 'loop_isolation'])
            self.assertTrue(status['observation_valid'])
            self.assertEqual(status['discovery']['state'], 'monitoring')

    def test_business_and_transport_do_not_import_vendor(self):
        paths = [*ROOT.joinpath('src/domain').glob('*.h'),
                 *ROOT.joinpath('src/application').glob('*.h'),
                 *ROOT.joinpath('src/bdi/strategies').glob('*.h'),
                 ROOT / 'src/net/switch_ssh.h', ROOT / 'src/net/switch_heartbeat.h']
        for path in paths:
            with self.subTest(path=path.name):
                includes = [line for line in path.read_text().splitlines() if line.startswith('#include')]
                self.assertFalse(any('h3c/' in line for line in includes), includes)
