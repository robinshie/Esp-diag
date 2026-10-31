import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from esp_backend.ingestion import ReportIngestor
from esp_backend.repository import validate_report
from esp_gateway.environment import load_environment
from esp_backend.models import DeviceMetadata
from pydantic import ValidationError


def report():
    return {"schema_version": 1, "report_id": "test-report-1", "sent_at_ms": 123,
            "status": None, "observations": [], "lines": [{"at_ms": 123, "text": "[BDI][STATUS]"}]}


class BackendInboxTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        settings = SimpleNamespace(state_path=str(Path(self.temp.name) / "inbox.sqlite3"),
            config={"agent": {"id": "edge-1", "topic_root": "test"}, "mqtt": {
                "host": "127.0.0.1", "port": 1883, "client_id": "test", "username": "test", "password_env": "TEST_MQTT_PASSWORD"}})
        with patch.dict(os.environ, TEST_MQTT_PASSWORD="test"):
            self.intake = ReportIngestor(settings, Mock())
        self.addCleanup(self.intake.stop)
        self.client = Mock()

    def message(self, payload=None, topic="test/edge-1/report"):
        return SimpleNamespace(topic=topic, payload=json.dumps(report() if payload is None else payload).encode(),
                               mid=7, qos=1, retain=False)

    def test_receipt_is_committed_before_mqtt_ack_and_replays_after_database_failure(self):
        self.client.ack.side_effect = lambda *_: self.assertEqual(self.intake.pending(), 1)
        self.intake.on_message(self.client, None, self.message())
        self.client.ack.assert_called_once_with(7, 1)

        def unavailable(*args):
            self.intake.stop_event.set()
            raise ConnectionError("database offline")
        self.intake.repository.register_report.side_effect = unavailable
        self.intake.archive_loop()
        self.assertEqual(self.intake.pending(), 1)
        self.assertTrue(self.intake.archive_error)

        self.intake.stop_event.clear()
        self.intake.repository.register_report.side_effect = lambda *args: self.intake.stop_event.set()
        self.intake.archive_loop()
        self.assertEqual(self.intake.pending(), 0)
        self.assertFalse(self.intake.archive_error)

    def test_full_inbox_disconnects_without_ack(self):
        self.intake.store.limit = 0
        self.intake.on_message(self.client, None, self.message())
        self.client.ack.assert_not_called()
        self.client.disconnect.assert_called_once()

    def test_malformed_or_direct_server_reports_never_enter_archive(self):
        for message in (self.message({"schema_version": 99}), self.message(topic="test/server/report"),
                        self.message(topic="other/edge-1/report")):
            self.intake.on_message(self.client, None, message)
        self.assertEqual(self.intake.pending(), 0)
        self.assertEqual(self.client.ack.call_count, 3)

    def test_report_limits_and_identity_are_checked(self):
        for changes in ({"report_id": "$bad"}, {"sent_at_ms": True}, {"lines": [report()["lines"][0]] * 65},
                        {"observations": [{"device": "a"}] * 33}):
            with self.assertRaises(ValueError):
                validate_report({**report(), **changes})


class EnvironmentTest(unittest.TestCase):
    def test_file_is_not_executed_and_shell_values_take_precedence(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {"EXISTING": "shell"}):
            path = Path(directory) / "settings.env"
            path.write_text("EXISTING=file\nLITERAL='$(echo secret)'\nQUOTED='two words' # comment\n")
            load_environment(path)
            self.assertEqual(os.environ["EXISTING"], "shell")
            self.assertEqual(os.environ["LITERAL"], "$(echo secret)")
            self.assertEqual(os.environ["QUOTED"], "two words")


class DeviceMetadataTest(unittest.TestCase):
    def test_number_validation_and_old_client_preservation_contract(self):
        self.assertNotIn('device_number', DeviceMetadata(name='ESP').model_dump(exclude_none=True))
        self.assertEqual(DeviceMetadata(name='ESP', device_number='').model_dump(exclude_none=True)['device_number'], '')
        self.assertEqual(DeviceMetadata(name='ESP', device_number='ESP-001').device_number, 'ESP-001')
        for value in ('A' * 33, 'ESP 001', '../ESP/001', '<script>'):
            with self.assertRaises(ValidationError):
                DeviceMetadata(name='ESP', device_number=value)
        with self.assertRaises(ValidationError):
            DeviceMetadata(name='ESP', device_number='ESP-001', id='changed-connection')


if __name__ == "__main__":
    unittest.main()
