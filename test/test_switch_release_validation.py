import importlib.util
import sys
from copy import deepcopy
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('verify_switch_release', ROOT / 'tools/verify_switch_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class SwitchReleaseValidationTests(unittest.TestCase):
    def bundle(self):
        states = []
        for cycle in (10, 11):
            states.append({'boot': 'current', 'firmware_version': 'r15', 'acceptance_read_only': True,
                           'ssh_state': 'connected', 'heartbeat': 'heartbeat_state=healthy',
                           'bdi': {'cycle': cycle, 'enabled': False, 'observation_valid': True,
                                   'strategy_ids': ['loop_discovery', 'loop_isolation', 'storm_isolation'],
                                   'storm': {'state': 'disabled'}, 'tables': {
                                       table: {'cycle': cycle, 'valid': True, 'complete': True, 'fresh': True}
                                       for table in release.TABLES}}})
        commands = [{'text': '[SSH][COMMAND] command=' + name + ' state=' +
                     ('truncated' if name == 'logs' else 'ok')} for name in release.READS]
        return {'collection_complete': True, 'statuses': states,
                'reports': [{'boot': 'current', 'lines': commands}]}

    def test_bounded_logbuffer_is_reported_without_weakening_core_reads(self):
        result = release.validate(self.bundle(), 'r15')
        self.assertTrue(result['passed'])
        self.assertEqual(result['commands_truncated_current_boot'], ['logs'])
        self.assertTrue(result['warnings'])

    def test_incomplete_download_retains_runtime_result_but_fails_overall(self):
        data = self.bundle()
        data['collection_complete'] = False
        result = release.validate(data, 'r15')
        self.assertFalse(result['passed'])
        self.assertTrue(result['runtime_passed'])

    def test_stale_missing_or_mixed_tables_never_count_as_completed_cycles(self):
        for field, value in (('fresh', False), ('complete', False), ('valid', False), ('cycle', 99)):
            data = deepcopy(self.bundle())
            data['statuses'][1]['bdi']['tables']['traffic'][field] = value
            self.assertFalse(release.validate(data, 'r15')['passed'])

    def test_disconnect_reboot_and_missing_readonly_guard_fail(self):
        for key, value in (('ssh_state', 'disconnected'), ('boot', 'different'), ('acceptance_read_only', False)):
            data = self.bundle()
            data['statuses'][1][key] = value
            self.assertFalse(release.validate(data, 'r15')['passed'])

    def test_captured_write_or_truncated_core_table_fails(self):
        data = self.bundle()
        data['reports'][0]['lines'].append({'text': '[SSH][COMMAND] command=restore:GE1/0/3 state=ok'})
        self.assertFalse(release.validate(data, 'r15')['passed'])
        data = self.bundle()
        for line in data['reports'][0]['lines']:
            if 'command=traffic ' in line['text']:
                line['text'] = line['text'].replace('state=ok', 'state=truncated')
        self.assertFalse(release.validate(data, 'r15')['passed'])


if __name__ == '__main__':
    unittest.main()
