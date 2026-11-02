import base64
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import threading
import unittest

from esp_gateway.archive_copy import ArchiveCopy
from esp_gateway.store import Store, fields
from esp_backend.log_ai import predict, model


class ArchiveCopyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.store = Store(str(Path(self.temp.name) / 'gateway.sqlite3')); self.addCleanup(self.store.close)
        self.gateway = SimpleNamespace(store=self.store, agent_topic='test/esp', stop_event=threading.Event(), pong_seen=threading.Event(),
                                       args=SimpleNamespace(report_interval_ms=60000))
        self.gateway.pong_seen.set()
        self.copy = ArchiveCopy(self.gateway)
        self.card = 'c' * 32
        self.payload = json.dumps({'schema_version': 1, 'report_id': 'saved-1', 'lines': [{'text': '数据库原文 <script>不执行</script>' * 40}]}, ensure_ascii=False).encode()
        self.item = {'sequence': 5, 'payload': self.payload.decode(), 'sha256': hashlib.sha256(self.payload).hexdigest()}

    def cursor(self):
        return self.store.db.execute('SELECT agent,sequence FROM sd_copy_cursor').fetchall()

    def test_exact_utf8_copy_and_cursor_only_after_verified_commit(self):
        data = bytearray(); states = []
        def exchange(command, key):
            self.assertEqual(self.cursor(), [])
            f = fields(command); states.append(f['op'])
            if f['op'] == 'data':
                self.assertEqual(int(f['offset']), len(data)); data.extend(base64.b64decode(f['data']))
            if f['op'] == 'end': self.assertEqual(hashlib.sha256(data).hexdigest(), key)
            return {'state': 'committed' if f['op'] == 'end' else 'ready', 'card': self.card, 'next': str(len(data))}
        self.copy.exchange = exchange
        self.copy.transfer(self.item, self.card)
        self.assertEqual(bytes(data), self.payload)
        self.assertEqual(states[0], 'begin'); self.assertEqual(states[-1], 'end')
        self.assertEqual(self.cursor(), [('test/esp/' + self.card, 5)])

    def test_partial_failure_and_changed_card_never_advance_cursor(self):
        for failure in ('end', 'card'):
            def exchange(command, key):
                f = fields(command)
                if failure == 'end' and f['op'] == 'end': raise TimeoutError('lost final acknowledgement')
                next_offset = int(f.get('offset', 0)) + len(base64.b64decode(f.get('data', '')))
                return {'state': 'ready', 'card': 'd' * 32 if failure == 'card' else self.card, 'next': str(next_offset)}
            self.copy.exchange = exchange
            with self.assertRaises((TimeoutError, ConnectionError)):
                self.copy.transfer(self.item, self.card)
            self.assertEqual(self.cursor(), [])

    def test_retry_of_verified_file_is_idempotent_and_survives_gateway_restart(self):
        calls = []
        self.copy.exchange = lambda command, key: calls.append(command) or {'state': 'committed', 'card': self.card}
        self.copy.transfer(self.item, self.card)
        self.assertEqual(len(calls), 1)
        other = Store(str(Path(self.temp.name) / 'gateway.sqlite3'))
        try: self.assertEqual(other.db.execute('SELECT sequence FROM sd_copy_cursor').fetchone()[0], 5)
        finally: other.close()

    def test_bad_digest_rejected_before_usb_write(self):
        self.copy.exchange = lambda *args: self.fail('unverified payload reached USB')
        with self.assertRaises(ValueError): self.copy.transfer({**self.item, 'sha256': '0' * 64}, self.card)

    def test_future_only_start_boundary_is_per_card_and_survives_restart(self):
        calls = []
        self.copy.fetch = lambda config, suffix: calls.append(suffix) or {'received_after_ms': 123456}
        self.assertEqual(self.copy.start_boundary({}, self.card), 123456)
        self.assertEqual(self.copy.start_boundary({}, self.card), 123456)
        reopened = ArchiveCopy(self.gateway)
        reopened.fetch = lambda *_: self.fail('restart must not reset the boundary and skip pending reports')
        self.assertEqual(reopened.start_boundary({}, self.card), 123456)
        self.assertIsNone(reopened.start_boundary({'backfill_history': True}, self.card))
        self.assertEqual(calls, ['/boundary'])

    def test_sd_interval_applies_to_backlog_and_survives_restart_and_live_change(self):
        self.copy.wall_clock = lambda: 1000
        self.copy.exchange = lambda *_: {'state': 'committed', 'card': self.card}
        self.assertEqual(self.copy.remaining_delay(self.card), 0)
        self.copy.transfer(self.item, self.card)
        self.assertEqual(self.copy.remaining_delay(self.card), 60)
        reopened = ArchiveCopy(self.gateway, wall_clock=lambda: 1030)
        self.assertEqual(reopened.remaining_delay(self.card), 30)
        self.gateway.reports = SimpleNamespace(interval=120)
        self.assertEqual(reopened.remaining_delay(self.card), 90)
        self.assertEqual(reopened.remaining_delay('d' * 32), 0)

    def test_actual_exchange_frames_fit_firmware_limit_and_validate_ack_identity(self):
        frames = []; data = bytearray()
        def send(frame):
            frames.append(frame); self.assertLessEqual(len(frame.encode()), 511)
            f = fields(frame)
            if f['op'] == 'data': data.extend(base64.b64decode(f['data']))
            state = 'committed' if f['op'] == 'end' else 'ready'
            self.copy.accept(f"[SDCOPY] req={f['req']} key={f['key']} state={state} next={len(data)} card={self.card}")
            return True
        self.gateway.send_usb = send
        self.copy.transfer(self.item, self.card)
        self.assertEqual(data, self.payload)
        self.assertGreater(len(frames), 3)

    def test_late_ack_is_ignored_and_copy_ack_does_not_become_new_telemetry(self):
        self.copy.request = 'current'
        self.assertTrue(self.copy.accept('[SDCOPY] req=old state=committed'))
        self.assertIsNone(self.copy.reply)
        self.assertTrue(self.copy.accept('[SDCOPY] req=current state=committed'))
        self.assertEqual(self.copy.reply['state'], 'committed')
        self.assertFalse(self.copy.accept('[BDI][STATUS] reasoning_cycle=ACTIVE'))


class DatabaseLogAITest(unittest.TestCase):
    def test_same_model_and_predictions_as_training_parity_vectors(self):
        path = Path(__file__).resolve().parents[1] / 'experiments/tiny_log_explainer/artifacts/parity.json'
        data = model()
        for row in json.loads(path.read_text()):
            expected = max(range(len(row['scores'])), key=row['scores'].__getitem__)
            reply = predict(row['text'])
            self.assertEqual(reply['model'], data['version'])
            self.assertEqual(reply['label'], data['labels'][expected])
            self.assertAlmostEqual(reply['score'], row['scores'][expected], places=5)

    def test_unknown_or_overlong_text_cannot_be_accepted(self):
        self.assertFalse(predict('')['accepted'])
        reply = predict('THERMAL ' + '温度' * 400)
        self.assertTrue(reply['truncated']); self.assertFalse(reply['accepted'])
