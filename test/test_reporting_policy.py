"""Telemetry comes from confirmed firmware state, never from an input heartbeat."""
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from h3c_server_sim import UsbMqttGateway, AGENT
from reliable_gateway import Store, fields


class ReportingPolicyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.gateway = UsbMqttGateway.__new__(UsbMqttGateway)
        self.gateway.store = Store(str(Path(self.temp.name) / "state.db"))
        self.addCleanup(self.gateway.store.close)
        self.gateway.args = SimpleNamespace(devices=3, fault_rate=0.0, action_mode="success")
        self.gateway.world = {}
        self.gateway.topology = None
        self.gateway.states = {}
        self.gateway.input_lock = threading.RLock()
        self.gateway.logs_sent = self.gateway.mqtt_reports = self.gateway.unlock_acks = 0
        self.gateway.device_online = True
        self.gateway.last_device_seen = 0
        self.gateway.pong_seen = threading.Event()
        self.gateway.pending_replay = threading.Event()
        self.gateway.nonce = "expected"
        self.published = []
        self.usb_lines = []
        self.gateway.publish_mqtt = lambda topic, payload, retain=False: self.published.append((topic, payload, retain))
        self.gateway.send_usb = self.usb_lines.append
        self.gateway.queue_usb = self.usb_lines.append

    def test_heartbeat_never_invents_healthy_state(self):
        self.gateway.publish_log("CRITICAL", "FAN_FAILURE", "ALARM")
        self.gateway.publish_log("INFO", "HEARTBEAT", "OK")
        self.assertEqual(len(self.published), 1)
        self.assertTrue(self.published[0][0].endswith("/log"))
        self.assertEqual(len(self.usb_lines), 2)

    def test_three_devices_have_distinct_identity(self):
        self.gateway.random_event()
        self.assertEqual({fields(s)["device"] for s in self.usb_lines}, {"sim-01", "sim-02", "sim-03"})
        self.assertFalse(self.published)

    def test_firmware_state_is_authoritative(self):
        line = "[BDI][STATE] device=a kind=server state=FAULT availability=ONLINE active_faults=1 faults=fan1:FAN_FAILURE"
        self.gateway._handle_line(line)
        self.assertEqual(self.published[0], ("h3c/server/a/health", line + " seen_at_ms=0", True))
        self.assertEqual(self.published[1][0], "h3c/server/a/availability")
        self.assertFalse(any("NORMAL" in p[1] for p in self.published))

    def test_pong_nonce_is_checked(self):
        self.gateway._handle_line("H3C PONG nonce=wrong boot=old")
        self.assertFalse(self.gateway.pong_seen.is_set())
        self.gateway._handle_line("H3C PONG nonce=expected boot=new")
        self.assertTrue(self.gateway.pong_seen.is_set())
        self.assertTrue(self.gateway.pending_replay.is_set())

    def test_simulated_executor_deduplicates_retries_and_reboot(self):
        base = "[BDI][CMD] UNLOCK device=sim-01 component=admin fault=AUTH_LOCKED incident=1-1 "
        self.gateway.execute_simulated(base + "request_id=boot1-1")
        self.gateway.execute_simulated(base + "request_id=boot1-1")
        self.gateway.execute_simulated(base + "request_id=boot2-1")
        self.assertEqual(self.gateway.store.db.execute("SELECT COUNT(*) FROM actions").fetchone()[0], 1)
        self.assertEqual(sum("H3C ACK" in s for s in self.usb_lines), 3)

    def test_partial_action_acknowledges_without_claiming_recovery(self):
        self.gateway.args.action_mode = "partial"
        self.gateway.execute_simulated("[BDI][CMD] UNLOCK device=a component=admin fault=AUTH_LOCKED incident=1-1 request_id=b-1")
        self.assertEqual(len(self.usb_lines), 1)
        self.assertIn("status=OK", self.usb_lines[0])
        self.assertFalse(any("RECOVERY" in s for s in self.usb_lines))

    def test_source_frames_fit_protocol(self):
        for device in ("a", "b", "c"):
            for event, component in (("FAN_FAILURE", "fan1"), ("PSU_FAILURE", "psu1"), ("THERMAL", "cpu1")):
                self.gateway.publish_log("CRITICAL", event, "ALARM", device=device, component=component)
        self.assertTrue(all(len(s.encode()) <= 511 for s in self.usb_lines))
        self.assertTrue(all("epoch=" in s and "seq=" in s for s in self.usb_lines))

    def test_repeated_alarm_preserves_incident_across_reboot(self):
        first = self.gateway.publish_log("WARN", "AUTH_LOCKED", "LOCKED")
        second = self.gateway.publish_log("WARN", "AUTH_LOCKED", "LOCKED")
        self.assertEqual(fields(first)["incident"], fields(second)["incident"])
        replay = list(self.gateway.store.replay())[0]
        self.assertEqual(fields(first)["incident"], fields(replay)["incident"])
