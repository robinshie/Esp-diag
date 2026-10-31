import json
import tempfile
import unittest
from pathlib import Path

from esp_gateway.reports import ReportBatcher
from esp_gateway.store import Store


class ReportBatchingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = str(Path(self.temp.name) / "reports.sqlite3")
        self.store = Store(self.path)
        self.addCleanup(self.store.close)
        self.now = 0
        self.topic = "site/esp/report"
        self.batch = ReportBatcher(self.store, self.topic, clock=lambda: self.now)

    def test_sample_and_receipt_and_runtime_are_one_packet_without_duplicate_lines(self):
        state = "[BDI][STATE] device=a state=DEGRADED locked=true active_faults=1"
        receipt = "[BDI][RECEIPT] device=a epoch=1 seq=2 result=ACCEPTED"
        status = "[BDI][STATUS] reasoning_cycle=ACTIVE uptime_ms=1000"
        for line in (state, receipt, status):
            self.batch.ingest("serial", line)
        self.batch.ingest("observation/a", state + " seen_at_ms=1000")
        self.batch.ingest("status", status + " seen_at_ms=1100 connection_id=usb-one")
        self.assertIsNone(self.store.first())
        self.now = 0.26
        self.assertTrue(self.batch.flush())
        rows = self.store.db.execute("SELECT topic,payload,retain FROM outbox").fetchall()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][0], self.topic)
        report = json.loads(rows[0][1])
        self.assertEqual([line["text"] for line in report["lines"]], [state, receipt, status])
        self.assertEqual(report["observations"][0]["seen_at_ms"], "1000")
        self.assertEqual(report["status"]["seen_at_ms"], "1100")
        self.assertEqual(report["status"]["connection_id"], "usb-one")
        self.assertEqual(rows[0][2], 1)
        self.assertFalse(self.batch.flush())

    def test_pending_burst_survives_process_reopen(self):
        self.batch.ingest("serial", "[BDI][ACK] result=FAILED")
        other_store = Store(self.path)
        try:
            reopened = ReportBatcher(other_store, self.topic, clock=lambda: self.now)
            self.assertTrue(reopened.flush())
            self.assertIn("result=FAILED", other_store.first()[2])
            again = ReportBatcher(other_store, self.topic, clock=lambda: self.now)
            self.assertFalse(again.flush(force=True))
        finally:
            other_store.close()

    def test_capacity_failure_does_not_drop_pending_fault(self):
        self.store.limit = 1
        self.store.enqueue("other", "backlog")
        self.batch.ingest("serial", "[BDI][STATE] device=a state=FAULT")
        with self.assertRaises(BufferError):
            self.batch.flush(force=True)
        self.store.delivered(self.store.first()[0])
        self.batch.flush(force=True)
        self.assertIn("state=FAULT", self.store.first()[2])

    def test_new_boot_discards_old_runtime_and_observation_snapshots(self):
        self.batch.ingest("serial", "H3C PONG boot=old")
        self.batch.ingest("status", "reasoning_cycle=ACTIVE seen_at_ms=1000")
        self.batch.ingest("observation/a", "device=a state=NORMAL seen_at_ms=1000")
        self.batch.flush(force=True)
        self.batch.ingest("serial", "[BDI][BOOT] boot=new")
        self.batch.flush(force=True)
        report = json.loads(self.store.db.execute("SELECT payload FROM outbox ORDER BY id DESC").fetchone()[0])
        self.assertIsNone(report["status"])
        self.assertEqual(report["observations"], [])

    def test_busy_input_is_bounded_and_not_postponed_forever(self):
        for index in range(130):
            self.batch.ingest("serial", f"[BDI][EVENT] index={index}")
        self.batch.flush(force=True)
        reports = [json.loads(row[0]) for row in self.store.db.execute("SELECT payload FROM outbox ORDER BY id")]
        self.assertEqual([len(report["lines"]) for report in reports], [64, 64, 2])
        self.assertEqual(len({report["report_id"] for report in reports}), 3)

    def test_semantic_copy_does_not_duplicate_raw_action(self):
        raw = "[BDI][ACK] action=UNLOCK result=SUCCESS"
        self.batch.ingest("serial", raw)
        self.batch.ingest("action", raw)
        self.batch.flush(force=True)
        self.assertEqual(len(json.loads(self.store.first()[2])["lines"]), 1)

    def test_normal_updates_wait_for_one_interval_but_faults_flush_quickly(self):
        self.batch.ingest("status", "reasoning_cycle=ACTIVE seen_at_ms=1000")
        self.batch.flush(force=True)
        self.store.delivered(self.store.first()[0])
        self.batch.ingest("status", "reasoning_cycle=ACTIVE seen_at_ms=2000")
        self.batch.ingest("serial", "[BDI][RECEIPT] result=ACCEPTED")
        self.now = 1
        self.assertFalse(self.batch.flush())
        self.batch.ingest("serial", "[BDI][CMD] UNLOCK request_id=one")
        self.now = 1.26
        self.assertTrue(self.batch.flush())
        report = json.loads(self.store.first()[2])
        self.assertEqual(len(report["lines"]), 2)

    def test_gateway_diagnostic_is_preserved_and_marked_separately(self):
        self.batch.ingest("action", "reason=USB_OUTPUT_TOO_LONG")
        self.now = 0.26
        self.assertTrue(self.batch.flush())
        self.assertEqual(json.loads(self.store.first()[2])["lines"][0]["text"], "[GATEWAY] reason=USB_OUTPUT_TOO_LONG")

    def test_sixty_second_reports_include_interval_and_retain_urgent_events(self):
        self.batch.set_interval(60000)
        self.batch.ingest("status", "reasoning_cycle=ACTIVE seen_at_ms=1000")
        self.batch.flush(force=True)  # initial discovery remains immediate
        self.store.delivered(self.store.first()[0])
        self.batch.ingest("serial", "[BDI][STATUS] reasoning_cycle=ACTIVE")
        self.now = 59.9
        self.assertFalse(self.batch.flush())
        self.now = 60
        self.assertTrue(self.batch.flush())
        self.assertEqual(json.loads(self.store.first()[2])["report_interval_ms"], 60000)
        self.store.delivered(self.store.first()[0])
        self.batch.ingest("serial", "[BDI][RECOVERED] device=a component=admin fault=AUTH_LOCKED")
        self.now = 60.26
        self.assertTrue(self.batch.flush())

    def test_live_interval_changes_reschedule_normal_batches_without_delaying_alarm(self):
        self.batch.ingest("serial", "[BDI][STATUS] reasoning_cycle=ACTIVE")
        self.now = 1
        self.batch.set_interval(60000)
        self.now = 5
        self.assertFalse(self.batch.flush())
        self.batch.ingest("serial", "[BDI][CMD] UNLOCK request_id=one")
        self.batch.set_interval(120000)
        self.now = 5.26
        self.assertTrue(self.batch.flush())
        self.assertEqual(json.loads(self.store.first()[2])["report_interval_ms"], 120000)
