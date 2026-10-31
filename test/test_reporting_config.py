import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from fastapi.testclient import TestClient
from esp_backend.api import create_app
from esp_gateway.reporting_config import ReportingConfig


class ReportingConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'gateway.json'
        self.original = {'agent': {'id': 'edge-1'}, 'reporting': {'interval_ms': 60000, 'batch_ms': 250},
                         'serial': {'port': '/dev/example'}, 'mqtt': {'password_env': 'SECRET_NAME'}}
        self.path.write_text(json.dumps(self.original))
        self.config = ReportingConfig(self.path)

    def test_atomic_save_preserves_other_settings_and_reopens_with_same_interval(self):
        self.config.save('edge-1', 120000)
        self.assertEqual(ReportingConfig(self.path).read('edge-1')['interval_ms'], 120000)
        saved = json.loads(self.path.read_text())
        self.original['reporting']['interval_ms'] = 120000
        self.assertEqual(saved, self.original)

    def test_wrong_agent_invalid_interval_and_failed_replace_preserve_original(self):
        before = self.path.read_bytes()
        with self.assertRaises(KeyError):
            self.config.save('edge-2', 60000)
        for value in (0, 999, 300001, True, 60000.5):
            with self.assertRaises(ValueError):
                self.config.save('edge-1', value)
        with patch('esp_gateway.reporting_config.os.replace', side_effect=PermissionError), self.assertRaises(PermissionError):
            self.config.save('edge-1', 120000)
        self.assertEqual(self.path.read_bytes(), before)
        self.assertEqual(list(self.path.parent.iterdir()), [self.path])

    def test_api_requires_authentication_validates_scope_and_reloads_saved_config(self):
        settings = SimpleNamespace(token='x' * 32, config_path=str(self.path))
        with TestClient(create_app(settings, Mock(), Mock())) as client:
            url = '/api/devices/edge-1/reporting'
            headers = {'Authorization': 'Bearer ' + settings.token}
            self.assertEqual(client.put(url, json={'interval_seconds': 60}).status_code, 401)
            for seconds in (0, 301, 1.5, True):
                self.assertEqual(client.put(url, headers=headers, json={'interval_seconds': seconds}).status_code, 422)
            self.assertEqual(client.put('/api/devices/edge-2/reporting', headers=headers, json={'interval_seconds': 60}).status_code, 404)
            saved = client.put(url, headers=headers, json={'interval_seconds': 120})
            self.assertEqual(saved.status_code, 200)
            self.assertTrue(saved.json()['saved'])
            self.assertEqual(client.get(url, headers=headers).json()['interval_ms'], 120000)
            self.assertNotIn('mqtt', saved.json())
            with patch('esp_gateway.reporting_config.os.replace', side_effect=PermissionError):
                self.assertEqual(client.put(url, headers=headers, json={'interval_seconds': 60}).status_code, 503)
            self.assertEqual(client.get(url, headers=headers).json()['interval_ms'], 120000)
