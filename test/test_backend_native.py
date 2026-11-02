"""Native intake tests. Set ESP_TEST_MONGO=1 for isolated real-MongoDB cases."""
import copy
from dataclasses import replace
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import uuid

from fastapi.testclient import TestClient
from esp_backend.api import create_app
from esp_backend.ingestion import ReportIngestor
from esp_backend.native_contract import availability, validate_status
from esp_backend.repository import MongoRepository, connection_session, validate_report
from esp_backend.settings import Settings


def native_report(boot="boot-a", uptime=90000, identity=None):
    return {"schema_version": 1, "source": "esp_native", "clock": "uptime", "transport": "ETH",
            "boot": boot, "report_id": identity or boot + "-1", "sent_at_ms": uptime,
            "lines": [{"at_ms": uptime, "text": "[BDI][HEARTBEAT] state=HEALTHY"}],
            "analysis": [{"line": 0, "model": "tiny-log-v3-protocol", "label": "heartbeat_ok",
                          "score": 0.99, "accepted": True}]}


def status(boot="boot-a", uptime=100):
    return {"schema_version": 1, "source": "esp_native", "boot": boot, "uptime_ms": uptime,
            "ip": "192.168.10.1", "ssh_state": "ready", "heartbeat": {"state": "HEALTHY", "rtt_ms": 1, "misses": 0},
            "bdi": {"state": "HEALTHY", "enabled": True},
            "analysis": {"model": "tiny-log-v3-protocol", "label": "heartbeat_ok", "score": 0.99, "accepted": True}}


class NativeContractTest(unittest.TestCase):
    def test_native_report_accepts_no_usb_observations_or_status(self):
        value = native_report()
        self.assertIs(validate_report(value), value)
        self.assertEqual(connection_session(value), "boot:boot-a")
        self.assertNotIn("observations", value)
        self.assertNotIn("status", value)

    def test_uptime_identity_and_analysis_are_validated(self):
        for changes in ({"boot": ""}, {"boot": "a/b"}, {"clock": "epoch"}, {"sent_at_ms": True},
                        {"sent_at_ms": 0}, {"schema_version": True}, {"lines": [{}]},
                        {"analysis": [{"line": 1}]}, {"analysis": [{"score": float("nan")}]},
                        {"analysis": [{"accepted": "yes"}]}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                validate_report({**native_report(), **changes})

    def test_status_accepts_device_analysis_and_rejects_legacy_experiment(self):
        self.assertEqual(validate_status(status())["analysis"]["label"], "heartbeat_ok")
        for value in ({"experiment": True, "uptime_ms": 5}, {**status(), "ip": "bad"},
                      {**status(), "uptime_ms": -1}, {**status(), "analysis": {"score": 1.1}},
                      {**status(), "extra": "x" * 17000}):
            with self.assertRaises(ValueError):
                validate_status(value)

    def test_native_and_legacy_availability(self):
        self.assertEqual(availability("ONLINE"), "ONLINE")
        self.assertEqual(availability("state=OFFLINE reason=disconnect"), "OFFLINE")
        with self.assertRaises(ValueError):
            availability("UNKNOWN")


class NativeInboxTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        settings = SimpleNamespace(state_path=str(Path(self.temp.name) / "inbox.sqlite3"),
            config={"agent": {"id": "edge-1", "topic_root": "test"}, "mqtt": {
                "client_id": "test", "username": "test", "password_env": "TEST_MQTT_PASSWORD"}})
        with patch.dict(os.environ, TEST_MQTT_PASSWORD="test"):
            self.intake = ReportIngestor(settings, Mock())
        self.addCleanup(self.intake.stop)
        self.client = Mock()

    def message(self, channel, value, retained=False):
        payload = value.encode() if isinstance(value, str) else json.dumps(value).encode()
        return SimpleNamespace(topic="test/edge-1/" + channel, payload=payload, mid=3, qos=1, retain=retained)

    def test_native_messages_are_committed_then_dispatched_without_usb(self):
        self.intake.on_connect(self.client, None, None, 0, None)
        self.assertIn(("test/+/status", 1), self.client.subscribe.call_args.args[0])
        messages = [self.message("availability", "ONLINE", True), self.message("status", status(), True),
                    self.message("report", native_report())]
        for message in messages:
            self.intake.on_message(self.client, None, message)
        self.assertEqual(self.intake.pending(), 3)
        self.intake.repository.register_report.side_effect = lambda *args: self.intake.stop_event.set()
        self.intake.archive_loop()
        self.assertEqual(self.intake.pending(), 0)
        self.intake.repository.record_availability.assert_called_once()
        args = self.intake.repository.record_telemetry.call_args.args
        self.assertEqual(args[:2], ("edge-1", status()))
        self.assertTrue(args[3])
        args = self.intake.repository.register_report.call_args.args
        self.assertEqual(args[1], native_report())
        self.assertGreater(args[4], args[1]["sent_at_ms"])
        tables = {row[0] for row in self.intake.store.db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        self.assertEqual(tables, {"outbox"})

    def test_malformed_status_and_command_channels_are_not_accepted(self):
        for message in (self.message("status", {"experiment": True}), self.message("command", "shutdown")):
            self.intake.on_message(self.client, None, message)
        self.assertEqual(self.intake.pending(), 0)
        self.assertEqual(self.client.ack.call_count, 2)

    def test_status_failure_preserves_receipt_for_replay(self):
        self.intake.on_message(self.client, None, self.message("status", status()))
        def unavailable(*args):
            self.intake.stop_event.set()
            raise ConnectionError("offline")
        self.intake.repository.record_telemetry.side_effect = unavailable
        self.intake.archive_loop()
        self.assertEqual(self.intake.pending(), 1)
        self.intake.stop_event.clear()
        self.intake.repository.record_telemetry.side_effect = lambda *args: self.intake.stop_event.set()
        self.intake.archive_loop()
        self.assertEqual(self.intake.pending(), 0)


@unittest.skipUnless(os.environ.get("ESP_TEST_MONGO") == "1", "set ESP_TEST_MONGO=1 for isolated MongoDB integration")
class NativeRepositoryTest(unittest.TestCase):
    def setUp(self):
        settings = Settings.load("config/demo.json", "deploy/mongodb.env")
        config = copy.deepcopy(settings.config)
        config["agent"]["topic_root"] = "native-test-" + uuid.uuid4().hex
        self.settings = replace(settings, config=config, history_days=0)
        self.repo = MongoRepository(self.settings)
        self.ready = False
        self.addCleanup(self.cleanup)
        self.repo.initialize()
        self.ready = True
        self.epoch = 1800000000000

    def cleanup(self):
        if self.ready:
            for collection in (self.repo.devices, self.repo.registrations, self.repo.reports, self.repo.copy_sequences):
                collection.delete_many({"topic_root": self.repo.root})
        self.repo.close()

    def test_reboot_replay_and_history_use_receipt_time_without_rewriting_evidence(self):
        first, second = native_report(), native_report("boot-b", 5)
        self.repo.register_report("edge-1", first, received_at_ms=self.epoch)
        self.repo.register_report("edge-1", second, received_at_ms=self.epoch + 1000)
        self.repo.register_report("edge-1", first, received_at_ms=self.epoch + 2000)
        page = self.repo.history_page("edge-1")
        self.assertEqual(page["total"], 2)
        self.assertEqual([row["report"]["sent_at_ms"] for row in page["reports"]], [5, 90000])
        self.assertEqual([row["archive_at_ms"] for row in page["reports"]], [self.epoch + 1000, self.epoch])
        self.assertEqual(self.repo.history_page("edge-1", scope="live")["reports"][0]["report"]["boot"], "boot-b")
        self.assertEqual(self.repo.list_devices()[0]["latest_report_at_ms"], self.epoch + 1000)
        self.assertEqual(self.repo.history_page("edge-1", start_ms=self.epoch + 500)["total"], 1)
        self.assertEqual(self.repo.saved_report("edge-1", first["report_id"]), first)
        late = native_report("boot-a", 95000, "late-a")
        self.repo.register_report("edge-1", late, received_at_ms=self.epoch + 3000)
        self.assertEqual(self.repo.history_page("edge-1", scope="live")["total"], 1)

    def test_availability_before_report_and_telemetry_api_are_preserved(self):
        self.repo.record_availability("edge-1", "ONLINE", self.epoch)
        self.repo.record_telemetry("edge-1", status(), self.epoch + 1)
        self.repo.record_telemetry("edge-1", status("old"), self.epoch - 1, True)
        with TestClient(create_app(self.settings, self.repo, Mock())) as client:
            self.assertEqual(client.get("/api/devices/edge-1/telemetry").status_code, 401)
            client.headers["Authorization"] = "Bearer " + self.settings.token
            payload = client.get("/api/devices/edge-1/telemetry").json()
            self.assertEqual(payload["status"], status())
            self.assertEqual(payload["availability"], "ONLINE")
            self.assertEqual(client.get("/api/devices/missing/telemetry").status_code, 404)
        # create_app closes its supplied repository at lifespan exit.
        self.repo = MongoRepository(self.settings)

    def test_equal_receipt_time_pagination_and_legacy_epoch_report(self):
        for index in range(5):
            self.repo.register_report("edge-1", native_report(identity=f"same-{index}"), received_at_ms=self.epoch)
        first = self.repo.history_page("edge-1", limit=2)
        second = self.repo.history_page("edge-1", limit=2, cursor=first["next_cursor"])
        third = self.repo.history_page("edge-1", limit=2, cursor=second["next_cursor"])
        ids = [row["report"]["report_id"] for page in (first, second, third) for row in page["reports"]]
        self.assertEqual(len(set(ids)), 5)
        self.assertIsNone(third["next_cursor"])
        legacy = {"schema_version": 1, "report_id": "legacy", "sent_at_ms": self.epoch - 1,
                  "observations": [], "lines": [], "status": None}
        self.repo.register_report("edge-2", legacy)
        self.assertEqual(self.repo.history_page("edge-2")["reports"][0]["archive_at_ms"], self.epoch - 1)


if __name__ == "__main__":
    unittest.main()
