import importlib.util
from copy import deepcopy
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('verify_broadcast_recovery', ROOT / 'tools/verify_broadcast_recovery.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


class StormAcceptanceTests(unittest.TestCase):
    def state(self, done=False):
        return {'firmware_version': verify.VERSION, 'boot': 'same-boot',
                'ssh_state': 'connected', 'ssh_host_key_state': 'trusted',
                'management': {'session_accepted': True, 'transport': 'ssh'},
                'heartbeat': 'heartbeat_state=healthy&heartbeat_misses=0',
                'storm_acceptance': {'port': verify.PORT, 'required_vlan': 4090, 'test_threshold_pps': 20,
                                     'state': 'passed' if done else 'awaiting_stop_confirmation'},
                'bdi': {'enabled': not done, 'cycle': 10, 'observation_valid': True,
                        'recovery': [{'port': verify.PORT, 'owned': not done, 'state': 'recovered' if done else 'contained'}],
                        'tables': {t: {'cycle': 10, 'valid': True, 'complete': True, 'fresh': True} for t in verify.TABLES}}}

    def test_native_lab_policy_and_lifecycle(self):
        binary = ROOT / 'test/.runtime/storm_acceptance'
        args = ['clang++', '-std=c++11', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DESP_STORM_ACCEPTANCE=1',
                str(ROOT / 'test/storm_acceptance.cpp'), '-o', str(binary)]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('isolated-port policy fences passed', result.stdout)

    def test_no_restore_without_operator_confirmation_or_ownership(self):
        for confirmed, owned in ((False, True), (True, False)):
            status = self.state()
            status['bdi']['recovery'][0]['owned'] = owned
            posts = []
            with tempfile.TemporaryDirectory() as folder:
                result = verify.run(lambda: status, lambda: posts.append(1), Path(folder)/'out.json', confirmed)
            self.assertFalse(result['passed'])
            self.assertFalse(posts)

    def test_one_restore_then_observed_recovery(self):
        states = iter([self.state(), self.state(True)])
        posts = []
        with tempfile.TemporaryDirectory() as folder:
            result = verify.run(lambda: next(states), lambda: posts.append(1) or 'queued', Path(folder)/'out.json', True, pause=lambda _: None)
        self.assertTrue(result['passed'])
        self.assertEqual(posts, [1])

    def test_ambiguous_http_post_never_replays(self):
        posts = []
        def post():
            posts.append(1)
            raise TimeoutError('unknown result')
        with tempfile.TemporaryDirectory() as folder:
            result = verify.run(lambda: self.state(), post, Path(folder)/'out.json', True)
        self.assertFalse(result['passed'])
        self.assertEqual(posts, [1])

    def test_reboot_and_incomplete_readback_do_not_pass(self):
        status = self.state(True)
        with self.assertRaises(ValueError):
            verify.completed(status, 'old-boot')
        for key, value in (('fresh', False), ('complete', False), ('valid', False), ('cycle', 99)):
            changed = deepcopy(status)
            changed['bdi']['tables']['traffic'][key] = value
            self.assertFalse(verify.completed(changed, 'same-boot'))


if __name__ == '__main__':
    unittest.main()
