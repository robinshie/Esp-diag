from pathlib import Path
import json
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SwitchFaultRecoveryTests(unittest.TestCase):
    def test_readonly_acceptance_policy_blocks_writes(self):
        self.run_experiment(readonly=True)

    def test_bdi_fault_and_recovery_experiment(self):
        self.run_experiment(readonly=False)

    def run_experiment(self, readonly):
        runtime = ROOT / 'test/.runtime'
        runtime.mkdir(exist_ok=True)
        library = ROOT / 'lib/EmbeddedBDI/lib'
        binary = runtime / ('switch_fault_recovery_readonly' if readonly else 'switch_fault_recovery')
        compiled = subprocess.run([
            'clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
            '-Wno-ignored-qualifiers', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-I' + str(library),
            '-DESP_BDI_READ_ONLY=' + ('1' if readonly else '0'),
            str(ROOT / 'test/switch_fault_recovery.cpp'),
            *map(str, sorted(library.rglob('*.cpp'))), '-o', str(binary),
        ], capture_output=True, text=True)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        fixtures = ROOT / 'test/fixtures/h3c-r6328p03'
        result = subprocess.run([str(binary),
                                 str(fixtures / 'counters-inbound-ge1-readonly.txt'),
                                 str(fixtures / 'counters-inbound-ge3-readonly.txt')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        cases = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(cases), 11 if readonly else 10)
        self.assertTrue(all(case['passed'] for case in cases))
        report = {'kind': 'simulated_switch_fault_experiment', 'real_switch_writes': 0,
                  'sanitizers': ['address', 'undefined'], 'cases': cases}
        filename = 'switch-fault-recovery-readonly-experiment.json' if readonly else 'switch-fault-recovery-experiment.json'
        (runtime / filename).write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
