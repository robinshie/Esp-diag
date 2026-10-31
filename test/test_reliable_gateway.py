"""P1 durable restart, offline replay, bounds and executor ledger scenarios."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from native_support import NativeAgent
from reliable_gateway import Store, fields


class DurableGatewayTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = str(Path(self.temp.name) / "gateway.db")

    def test_mqtt_disconnect_restart_replays_in_order(self):
        s = Store(self.path)
        for i in range(100):
            s.enqueue("h3c/server/a/log", str(i))
        s.close()  # gateway killed before broker accepts anything
        s = Store(self.path)
        self.addCleanup(s.close)
        for i in range(100):
            row = s.first()
            self.assertEqual(row[2], str(i))
            if i == 0:
                self.assertEqual(s.first(), row)  # failed PUBACK: do not delete
            s.delivered(row[0])
        self.assertIsNone(s.first())

    def test_outbox_overflow_is_explicit(self):
        s = Store(self.path, limit=2)
        self.addCleanup(s.close)
        s.enqueue("topic", "first"); s.enqueue("topic", "second")
        with self.assertRaises(BufferError):
            s.enqueue("topic", "third")
        self.assertEqual(s.first()[2], "first")

    def test_esp_and_gateway_restart_reconcile_all_component_faults(self):
        s = Store(self.path)
        for fault, component in (("FAN_FAILURE", "fan1"), ("THERMAL", "cpu1")):
            s.observe(f"H3C LOG {s.envelope('a')} event={fault} component={component} level=CRITICAL status=ALARM")
        s.save_action("a:admin:AUTH_LOCKED:1-1", {"mode": "success"})
        original_epoch = s.epoch
        s.close()
        s = Store(self.path)
        self.addCleanup(s.close)
        self.assertGreater(s.epoch, original_epoch)
        self.assertEqual(s.action("a:admin:AUTH_LOCKED:1-1"), {"mode": "success"})
        agent = NativeAgent("after-reboot")
        self.addCleanup(agent.close)
        for record in s.replay():
            for output in agent.send(record):
                if output.startswith("[BDI][STATE]"):
                    s.reconciled(output)
        self.assertEqual(agent.states["a"]["active_faults"], "2")
        self.assertEqual(len(list(s.replay())), 2)
        self.assertFalse(agent.commands)

    def test_stale_source_not_resurrected_by_gateway_restart(self):
        s = Store(self.path)
        self.addCleanup(s.close)
        with patch("reliable_gateway.time.time", return_value=100):
            s.observe(f"H3C LOG {s.envelope('a')} event=HEARTBEAT level=INFO status=OK")
        with patch("reliable_gateway.time.time", return_value=200):
            records = list(s.replay())
        agent = NativeAgent()
        self.addCleanup(agent.close)
        agent.send(records[0])
        self.assertEqual(agent.states["a"]["availability"], "OFFLINE")

    def test_pending_recovery_replays_after_assertion(self):
        s = Store(self.path)
        self.addCleanup(s.close)
        s.observe(f"H3C LOG {s.envelope('a')} event=THERMAL component=cpu1 level=CRITICAL status=ALARM")
        s.observe(f"H3C LOG {s.envelope('a')} event=RECOVERY fault=THERMAL component=cpu1 level=INFO status=OK")
        records = list(s.replay())
        self.assertEqual([fields(r)["event"] for r in records], ["THERMAL", "RECOVERY"])
        a = NativeAgent()
        self.addCleanup(a.close)
        for record in records:
            a.send(record)
        self.assertEqual(a.states["a"]["state"], "FAULT")
        a.tick(2001)
        self.assertEqual(a.states["a"]["state"], "NORMAL")

    def test_old_snapshot_cannot_erase_queued_recovery(self):
        s = Store(self.path)
        self.addCleanup(s.close)
        s.observe(f"H3C LOG {s.envelope('a')} event=THERMAL component=cpu1 level=CRITICAL status=ALARM incident=incident-1")
        s.observe(f"H3C LOG {s.envelope('a')} event=RECOVERY fault=THERMAL component=cpu1 level=INFO status=OK")
        s.reconciled("[BDI][STATE] device=a faults=none active_faults=0")
        self.assertEqual(len(list(s.replay())), 2)
        s.reconciled("[BDI][RECOVERED] device=a component=cpu1 fault=THERMAL incident=wrong")
        self.assertEqual(len(list(s.replay())), 2)
        s.reconciled("[BDI][RECOVERED] device=a component=cpu1 fault=THERMAL incident=incident-1")
        self.assertEqual(len(list(s.replay())), 1)
