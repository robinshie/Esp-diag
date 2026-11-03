import base64
import json
from pathlib import Path
import tempfile
import unittest
from urllib.parse import parse_qs, urlsplit

from tools.collect_device_logs import collect, read_report, summarize


class OfflineDiagnosticsTest(unittest.TestCase):
    def report(self, boot='boot-a'):
        return {'schema_version': 1, 'source': 'esp_native', 'boot': boot,
                'lines': [{'text': '[SSH][COMMAND] request_id=bdi-read-1 command=loops state=ok'}]}

    def status(self):
        return {'mode': 'standalone', 'log_source': 'esp_native', 'boot': 'boot-a',
                'ssh_state': 'connected', 'heartbeat': 'heartbeat_state=healthy',
                'bdi': {'cycle': 9, 'observation_valid': False, 'state': 'unknown'}}

    def chunk(self, payload, start=0, end=None):
        end = len(payload) if end is None else end
        return {'state': 'ready', 'start': start, 'next': end, 'size': len(payload),
                'eof': end == len(payload), 'base64': base64.b64encode(payload[start:end]).decode()}

    def test_pending_and_multibyte_utf8_split_preserved(self):
        report = self.report()
        report['lines'].append({'text': '环路证据'})
        raw = json.dumps(report, ensure_ascii=False).encode()
        split = raw.index('环'.encode()) + 1
        replies = iter([{'state': 'pending', 'job': 3}, self.chunk(raw, 0, split), self.chunk(raw, split)])
        paths = []
        def get(path):
            paths.append(path)
            return next(replies)
        self.assertEqual(read_report(get, 'log.json', lambda _: None), report)
        self.assertEqual(paths[1], '/storage/read?job=3')
        self.assertIn('offset=' + str(split), paths[2])

    def test_malformed_or_truncated_chunks_rejected(self):
        raw = json.dumps(self.report()).encode()
        for change in ({'next': 1}, {'size': 65537}, {'eof': False}, {'base64': '!'}, {'start': 4}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                read_report(lambda _: {**self.chunk(raw), **change}, 'log', lambda _: None)

    def test_expired_job_restarts_same_offset_without_losing_validated_chunks(self):
        raw = json.dumps(self.report()).encode()
        split = len(raw) // 2
        replies = iter([self.chunk(raw, 0, split), {'state': 'pending', 'job': 7},
                        {'state': 'expired'}, {'state': 'pending', 'job': 9},
                        self.chunk(raw, split)])
        paths = []
        def get(path):
            paths.append(path)
            return next(replies)
        self.assertEqual(read_report(get, 'log.json', lambda _: None), self.report())
        self.assertEqual(paths[2], '/storage/read?job=7')
        self.assertEqual(paths[3], '/storage/read?file=log.json&offset=' + str(split))
        self.assertEqual(paths[4], '/storage/read?job=9')

    def test_repeated_expiry_stops_after_three_restarts(self):
        paths = []
        def get(path):
            paths.append(path)
            return {'state': 'expired'}
        with self.assertRaisesRegex(ValueError, 'expired'):
            read_report(get, 'log.json', lambda _: None)
        self.assertEqual(len(paths), 4)
        self.assertEqual(set(paths), {'/storage/read?file=log.json&offset=0'})

    def test_retry_after_expiry_still_rejects_wrong_chunk_offset(self):
        raw = json.dumps(self.report()).encode()
        replies = iter([{'state': 'expired'}, {**self.chunk(raw), 'start': 1}])
        with self.assertRaisesRegex(ValueError, '位置或长度异常'):
            read_report(lambda _: next(replies), 'log.json', lambda _: None)

    def test_wait_for_hotspot_then_collect_pages_without_writes(self):
        calls, attempts = [], 0
        raw = json.dumps(self.report()).encode()
        def get(path):
            nonlocal attempts
            calls.append(path)
            if path == '/storage':
                return {'state': 'ready', 'pending': '2'}
            if path == '/device/status':
                attempts += 1
                if attempts == 1:
                    raise OSError('offline')
                return self.status()
            if path.startswith('/storage/files?'):
                before = parse_qs(urlsplit(path).query)['before'][0]
                return {'state': 'ready', 'source': 'esp_native', 'has_older': before == '0',
                        'files': [{'name': before + '.json', 'sequence': 10 if before == '0' else 9}]}
            if path.startswith('/storage/read?'):
                return self.chunk(raw)
            self.fail('Unexpected interface: ' + path)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertTrue(collect(get, output, observe=0, pause=lambda _: None))
            bundle = json.loads(output.read_text())
        self.assertEqual(len(bundle['reports']), 2)
        self.assertTrue(bundle['summary']['ssh_connected'])
        self.assertFalse(bundle['summary']['bdi_observation_valid'])
        self.assertIn('/storage/files?before=10', calls)

    def test_interrupted_collection_keeps_status_and_reports(self):
        raw = json.dumps(self.report()).encode()
        reads = 0
        def get(path):
            nonlocal reads
            if path == '/storage':
                return {'state': 'ready'}
            if path == '/device/status':
                return self.status()
            if path.startswith('/storage/files?'):
                return {'state': 'ready', 'source': 'esp_native', 'files': [
                    {'name': 'good', 'sequence': 2}, {'name': 'lost', 'sequence': 1}]}
            reads += 1
            if reads > 1:
                raise OSError('hotspot disconnected')
            return self.chunk(raw)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertFalse(collect(get, output, observe=0))
            bundle = json.loads(output.read_text())
        self.assertEqual(len(bundle['reports']), 1)
        self.assertTrue(bundle['errors'])
        self.assertFalse(bundle['collection_complete'])

    def test_cycle_active_and_old_boot_commands_do_not_prove_valid_bdi(self):
        result = summarize({'statuses': [self.status()], 'reports': [self.report('previous-boot')]})
        self.assertTrue(result['ssh_connected'])
        self.assertTrue(result['heartbeat_healthy'])
        self.assertFalse(result['bdi_observation_valid'])
        self.assertEqual(result['commands_ok_current_boot'], [])

    def test_offline_timeout_saves_failure(self):
        def get(_):
            raise OSError('no route')
        ticks = iter([0, 11])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertFalse(collect(get, output, wait=10, clock=lambda: next(ticks)))
            self.assertTrue(json.loads(output.read_text())['errors'])

    def test_busy_longer_than_old_window_eventually_collects(self):
        attempts = 0
        raw = json.dumps(self.report()).encode()
        def get(path):
            nonlocal attempts
            if path == '/device/status':
                return self.status()
            if path == '/storage':
                return {'state': 'ready', 'pending': '23'}
            if path.startswith('/storage/files?'):
                attempts += 1
                if attempts <= 50:
                    return {'state': 'busy'}
                return {'state': 'ready', 'source': 'esp_native',
                        'files': [{'name': '1.json', 'sequence': 1}]}
            return self.chunk(raw)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertTrue(collect(get, output, observe=0, pause=lambda _: None))
            bundle = json.loads(output.read_text())
        self.assertEqual(attempts, 52)  # Includes the fresh snapshot after download.
        self.assertEqual(bundle['listing_samples'][0]['response']['state'], 'busy')
        self.assertEqual(bundle['storage_samples'][0]['pending'], '23')

    def test_disconnect_during_download_is_in_tail_logs_not_only_final_status(self):
        calls, snapshots, downloaded = [], 0, False
        def get(path):
            nonlocal snapshots, downloaded
            calls.append(path)
            if path == '/device/status':
                return {**self.status(), 'ssh_state': 'disconnected' if downloaded else 'connected'}
            if path == '/storage':
                return {'state': 'ready', 'pending': '1'}
            query = parse_qs(urlsplit(path).query)
            if path.startswith('/storage/files?'):
                snapshots += 1
                names = [3, 2] if downloaded else [2]
                return {'state': 'ready', 'source': 'esp_native', 'files': [
                    {'name': str(n), 'sequence': n} for n in names]}
            self.assertTrue(path.startswith('/storage/read?'))
            report = self.report()
            if query['file'] == ['3']:
                report['lines'] = [{'text': '[SSH][STATUS] state=disconnected'}]
            downloaded = True
            return self.chunk(json.dumps(report).encode())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertTrue(collect(get, output, observe=0, limit=1, pause=lambda _: None))
            bundle = json.loads(output.read_text())
        self.assertEqual(snapshots, 2)
        self.assertEqual(len(bundle['reports']), 2)
        self.assertEqual(bundle['reports'][-1]['lines'][0]['text'], '[SSH][STATUS] state=disconnected')
        self.assertEqual(bundle['tail_collection']['reports'], 1)
        self.assertFalse(bundle['summary']['ssh_connected'])
        self.assertTrue(all(path.startswith(('/device/status', '/storage')) for path in calls))

    def test_tail_paginates_and_limits_new_reports_without_rereading_history(self):
        snapshots, reads = 0, []
        def get(path):
            nonlocal snapshots
            if path == '/device/status':
                return self.status()
            if path == '/storage':
                return {'state': 'ready'}
            query = parse_qs(urlsplit(path).query)
            if path.startswith('/storage/files?'):
                snapshots += 1
                n = 2 if snapshots == 1 else 5 if query['before'] == ['0'] else 4
                return {'state': 'ready', 'source': 'esp_native', 'has_older': True,
                        'files': [{'name': str(n), 'sequence': n}]}
            reads.append(query['file'][0])
            return self.chunk(json.dumps(self.report()).encode())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertTrue(collect(get, output, observe=0, limit=1, tail_limit=1, pause=lambda _: None))
            bundle = json.loads(output.read_text())
        self.assertEqual(reads, ['2', '5'])
        self.assertEqual(snapshots, 3)
        self.assertTrue(bundle['tail_collection']['limit_reached'])

    def test_tail_failure_keeps_initial_logs_and_marks_collection_incomplete(self):
        snapshots = 0
        def get(path):
            nonlocal snapshots
            if path == '/device/status':
                return self.status()
            if path == '/storage':
                return {'state': 'ready'}
            if path.startswith('/storage/files?'):
                snapshots += 1
                if snapshots > 1:
                    raise OSError('tail disconnected')
                return {'state': 'ready', 'source': 'esp_native',
                        'files': [{'name': '2', 'sequence': 2}]}
            return self.chunk(json.dumps(self.report()).encode())
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            self.assertFalse(collect(get, output, observe=0, pause=lambda _: None))
            bundle = json.loads(output.read_text())
        self.assertEqual(len(bundle['reports']), 1)
        self.assertIn('tail disconnected', bundle['errors'])
        self.assertFalse(bundle['collection_complete'])

    def test_storage_failure_preserves_raw_response_and_previous_run(self):
        def get(path):
            if path == '/device/status':
                return self.status()
            if path == '/storage':
                return {'state': 'write_error', 'pending': '96', 'dropped': '70'}
            return {'state': 'write_error', 'files': []}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'result.json'
            output.write_text('{"previous": true}')
            self.assertFalse(collect(get, output, observe=0))
            bundle = json.loads(output.read_text())
            backups = list(Path(directory).glob('result-*.json'))
            self.assertEqual(json.loads(backups[0].read_text()), {'previous': True})
        self.assertIn('write_error', bundle['errors'][0])
        self.assertEqual(bundle['storage_samples'][0]['dropped'], '70')


if __name__ == '__main__':
    unittest.main()
