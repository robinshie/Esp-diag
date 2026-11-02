#!/usr/bin/env python3
"""Isolated real MQTT/Mongo/API acceptance, using cached Docker images only.

Creates UUID-named disposable containers on random loopback ports. Never stops,
reconfigures or deletes an existing service. Does not contact real hardware.
"""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from fastapi.testclient import TestClient
import paho.mqtt.client as mqtt
from pymongo import MongoClient
from esp_backend.ai_provider import AISettings, ExplanationService
from esp_backend.api import create_app
from esp_backend.ingestion import ReportIngestor
from esp_backend.repository import MongoRepository
from esp_backend.settings import Settings
from test.test_backend_native import NativeRepositoryTest, native_report, status


def docker(*args, check=True):
    return subprocess.run(['docker', *args], text=True, check=check, capture_output=True)


def wait_for(predicate, label, timeout=20):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except Exception as error:
            last = type(error).__name__
        time.sleep(0.05)
    raise AssertionError(f'timed out: {label}' + (f' ({last})' if last else ''))


@contextmanager
def services():
    suffix = uuid.uuid4().hex[:12]
    mongo_name, broker_name = 'esp-native-test-mongo-' + suffix, 'esp-native-test-mqtt-' + suffix
    created = []
    try:
        docker('run', '--pull', 'never', '-d', '--rm', '--name', mongo_name,
               '-p', '127.0.0.1::27017', 'mongo:8.0', '--bind_ip_all')
        created.append(mongo_name)
        topic = 'native-test-' + suffix
        args = ['run', '--pull', 'never', '-d', '--rm', '--name', broker_name, '-p', '127.0.0.1::1883',
                '--entrypoint', '/bin/sh',
                '-v', str(ROOT / 'deploy/native-mosquitto-entrypoint.sh') + ':/esp/start.sh:ro',
                '-v', str(ROOT / 'mqtt/mosquitto.conf') + ':/mosquitto/config/mosquitto.conf:ro']
        env = {'MQTT_TOPIC_ROOT': topic, 'ESP_DEVICE_MQTT_USERNAME': 'device-test',
               'ESP_DEVICE_MQTT_PASSWORD': 'isolated-device-test', 'ESP_ARCHIVE_MQTT_USERNAME': 'archive-test',
               'ESP_ARCHIVE_MQTT_PASSWORD': 'isolated-archive-test', 'ESP_DASHBOARD_MQTT_USERNAME': 'dashboard-test',
               'ESP_DASHBOARD_MQTT_PASSWORD': 'isolated-dashboard-test'}
        for key, value in env.items():
            args += ['-e', key + '=' + value]
        docker(*args, 'eclipse-mosquitto:2', '/esp/start.sh')
        created.append(broker_name)
        mongo_port = int(docker('port', mongo_name, '27017/tcp').stdout.strip().rsplit(':', 1)[1])
        mqtt_port = int(docker('port', broker_name, '1883/tcp').stdout.strip().rsplit(':', 1)[1])
        uri = f'mongodb://127.0.0.1:{mongo_port}'
        probe = MongoClient(uri, serverSelectionTimeoutMS=200, connectTimeoutMS=200)
        try:
            wait_for(lambda: probe.admin.command('ping')['ok'], 'temporary MongoDB ready')
        finally:
            probe.close()
        yield mongo_name, uri, mqtt_port, topic
    finally:
        for name in reversed(created):
            docker('unpause', name, check=False)
            docker('rm', '-f', '-v', name, check=False)


class NoMQTT:
    def __init__(self):
        self.connected = threading.Event()
        self.connected.set()
        self.archive_error = False

    def start(self):
        pass

    def stop(self):
        pass

    def pending(self):
        return 0


class ProviderHandler(BaseHTTPRequestHandler):
    mode = 'ok'
    requests = []

    def log_message(self, *_):
        pass

    def do_POST(self):
        self.__class__.requests.append(json.loads(self.rfile.read(int(self.headers['Content-Length']))))
        if self.mode == 'timeout':
            time.sleep(1.5)
        payload = (b'invalid-json' if self.mode == 'malformed' else
                   json.dumps({'message': {'content': '测试参考解释，未执行任何设备动作。'}}).encode())
        self.send_response(500 if self.mode == 'error' else 200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass


def run():
    checks = []
    def passed(name):
        checks.append(name)
        print('PASS', name, flush=True)

    with services() as (mongo_name, uri, mqtt_port, topic), tempfile.TemporaryDirectory(prefix='esp-native-test-') as directory:
        print('Temporary MQTT and MongoDB ready on random loopback ports', flush=True)
        # Exercise the repository integration suite on the temporary database.
        previous_skip = NativeRepositoryTest.__unittest_skip__
        NativeRepositoryTest.__unittest_skip__ = False
        try:
            with patch.dict(os.environ, {'MONGODB_URI': uri, 'MONGO_DATABASE': 'native_repository_test',
                                         'ESP_API_TOKEN': 'isolated-test-api-token-' * 2}):
                result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(
                    unittest.defaultTestLoader.loadTestsFromTestCase(NativeRepositoryTest))
            if not result.wasSuccessful():
                raise AssertionError('native repository integration suite failed')
        finally:
            NativeRepositoryTest.__unittest_skip__ = previous_skip
        passed('native reboot, duplicate replay, epoch history pagination and telemetry API against real MongoDB')

        os.environ['ESP_NATIVE_TEST_MQTT_PASSWORD'] = 'isolated-archive-test'
        config = {'agent': {'topic_root': topic}, 'mqtt': {'host': '127.0.0.1', 'port': mqtt_port,
                  'client_id': 'archive-' + topic, 'username': 'archive-test', 'password_env': 'ESP_NATIVE_TEST_MQTT_PASSWORD'}}
        settings = Settings(config=config, token='isolated-test-api-token-' * 2, mongo_host='127.0.0.1', mongo_port=27017,
                            database='native_mqtt_test', username='', password='', mongo_uri=uri, history_days=0,
                            state_path=str(Path(directory) / 'inbox.sqlite3'), host='127.0.0.1', port=8080)
        repository = MongoRepository(settings)
        repository.initialize()
        intake = None
        publisher = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id='native-device-test')
        publisher.username_pw_set('device-test', 'isolated-device-test')
        connected = threading.Event()
        publisher.on_connect = lambda client, userdata, flags, reason, props: connected.set() if reason == 0 else None
        try:
            def start_intake():
                value = ReportIngestor(settings, repository)
                subscribed = threading.Event()
                value.client.on_subscribe = lambda *args: subscribed.set()
                value.start()
                wait_for(subscribed.is_set, 'archive MQTT subscriptions')
                return value

            intake = start_intake()
            publisher.connect('127.0.0.1', mqtt_port, keepalive=10)
            publisher.loop_start()
            wait_for(connected.is_set, 'device publisher connected')

            def publish(channel, payload, retained=False):
                body = payload if isinstance(payload, str) else json.dumps(payload)
                sent = publisher.publish(topic + '/edge-1/' + channel, body, qos=1, retain=retained)
                sent.wait_for_publish(timeout=5)
                if not sent.is_published():
                    raise AssertionError('publisher did not receive MQTT PUBACK')

            publish('availability', 'ONLINE', True)
            publish('status', status(), True)
            publish('report', native_report())
            wait_for(lambda: repository.reports.count_documents({'topic_root': topic}) == 1 and intake.pending() == 0,
                     'native report archived')
            assert repository.device_telemetry('edge-1')['status']['analysis']['accepted']
            passed('real Mosquitto ACL authenticated publish → durable SQLite inbox → native MongoDB evidence')

            publish('report', native_report('boot-b', 5))
            wait_for(lambda: repository.reports.count_documents({'topic_root': topic}) == 2, 'new boot archived')
            publish('report', native_report())
            wait_for(lambda: intake.pending() == 0, 'duplicate drained')
            page = repository.history_page('edge-1')
            assert page['total'] == 2 and page['reports'][0]['report']['sent_at_ms'] == 5
            assert repository.history_page('edge-1', scope='live')['total'] == 1
            passed('new boot uptime reset and MQTT duplicate keep exactly two reports and correct live boot')

            docker('pause', mongo_name)
            try:
                publish('report', native_report('boot-b', 10, 'during-outage'))
                def outage_receipt_committed():
                    with intake.store.lock:
                        return intake.store.db.execute('SELECT COUNT(*) FROM outbox WHERE payload LIKE ?',
                                                       ('%during-outage%',)).fetchone()[0] > 0
                wait_for(outage_receipt_committed, 'receipt durably queued during MongoDB outage')
                pending = intake.pending()
                intake.stop()
                intake = None
            finally:
                docker('unpause', mongo_name)
            intake = start_intake()
            wait_for(lambda: repository.saved_report('edge-1', 'during-outage') is not None and intake.pending() == 0,
                     'persisted receipt replayed after process restart', timeout=30)
            assert pending > 0
            passed('real MongoDB pause + ingestor restart preserves committed receipt and drains it after recovery')
            intake.stop()
            intake = None

            server = ThreadingHTTPServer(('127.0.0.1', 0), ProviderHandler)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            service = ExplanationService(AISettings('ollama', f'http://127.0.0.1:{server.server_port}', 'test-model', 1))
            try:
                with TestClient(create_app(settings, repository, NoMQTT(), service)) as api:
                    api.headers['Authorization'] = 'Bearer ' + settings.token
                    assert api.get('/api/devices/edge-1/telemetry').json()['status']['source'] == 'esp_native'
                    assert api.get('/api/reports?agent=edge-1').json()['total'] == 3
                    path = '/api/reports/during-outage/analyze?agent=edge-1&line=0'
                    for mode in ('error', 'malformed', 'timeout'):
                        ProviderHandler.mode = mode
                        assert api.post(path).status_code == 503, mode
                    ProviderHandler.mode = 'ok'
                    response = api.post(path)
                    assert response.status_code == 200, response.text
                    assert response.json()['device_control'] is False
                    assert all('tools' not in request for request in ProviderHandler.requests)
                    assert api.get('/api/devices/edge-1/telemetry').status_code == 200
                passed('real HTTP provider 500, malformed response and timeout return 503; telemetry survives; recovery stays advisory')
            finally:
                server.shutdown()
                server.server_close()
                thread.join(3)
        finally:
            publisher.disconnect()
            publisher.loop_stop()
            if intake is not None:
                intake.stop()
            repository.close()
            os.environ.pop('ESP_NATIVE_TEST_MQTT_PASSWORD', None)
    result = ROOT / 'test/.runtime/backend-native-integration.json'
    result.parent.mkdir(parents=True, exist_ok=True)
    result.write_text(json.dumps({'passed': checks, 'temporary_services_removed': True}, ensure_ascii=False, indent=2) + '\n')
    print('PASS temporary containers removed; result:', result, flush=True)


if __name__ == '__main__':
    run()
