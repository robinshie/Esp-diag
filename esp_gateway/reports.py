"""Bundle a USB output burst into one durable MQTT report with a state snapshot."""
from __future__ import annotations

import json
import threading
import time
import uuid

from .store import fields


class ReportBatcher:
    def __init__(self, store, topic, batch_ms=250, clock=time.monotonic, interval_ms=5000):
        self.store, self.topic, self.clock = store, topic, clock
        self.window = batch_ms / 1000
        self.interval = interval_ms / 1000
        self.lock = threading.RLock()
        with store.lock, store.db:
            store.db.execute("CREATE TABLE IF NOT EXISTS report_state(topic TEXT PRIMARY KEY, data TEXT)")
            row = store.db.execute("SELECT data FROM report_state WHERE topic=?", (topic,)).fetchone()
        self.data = json.loads(row[0]) if row else {"boot": None, "status": None, "observations": {}, "lines": [], "dirty": False}
        self.due = self.clock() if self.data["dirty"] else None
        self.next_periodic = self.clock() + self.interval

    def _save(self):
        self.store.db.execute("INSERT OR REPLACE INTO report_state VALUES(?,?)", (self.topic, json.dumps(self.data)))

    def ingest(self, channel, payload):
        if channel == "action" and payload.startswith("reason="):
            channel, payload = "serial", "[GATEWAY] " + payload
        # Routed event/action copies are already present in the actual USB lines.
        if channel not in {"serial", "status"} and not channel.startswith("observation/"):
            return
        f = fields(payload)
        if channel == "status" and not f.get("reasoning_cycle"):
            return  # A gateway connection notice is not ESP32 runtime status.
        with self.lock:
            if len(self.data["lines"]) >= 64:
                self.flush(force=True)
            with self.store.lock, self.store.db:
                urgent = False
                if channel == "serial":
                    urgent = payload.startswith(("[BDI][CMD]", "[BDI][ACK]", "[BDI][EVENT]", "[BDI][ACTION]",
                                                 "[BDI][RECOVERED]", "[BDI][REJECT", "[BDI][CAPACITY", "[BDI][BOOT]", "[GATEWAY]"))
                    if payload.startswith(("[BDI][BOOT]", "H3C PONG")) and f.get("boot") != self.data["boot"]:
                        self.data.update(boot=f.get("boot"), status=None, observations={})
                    self.data["lines"].append({"at_ms": int(time.time() * 1000), "text": payload})
                elif channel == "status":
                    urgent = self.data["status"] is None
                    self.data["status"] = f
                else:
                    previous = self.data["observations"].get(f["device"], {})
                    signature = lambda value: {key: item for key, item in value.items() if key not in {"seen_at_ms", "observed_ms"}}
                    urgent = signature(previous) != signature(f)
                    self.data["observations"][f["device"]] = f
                if not self.data["dirty"]:
                    self.due = self.next_periodic
                if urgent:
                    self.due = min(self.due, self.clock() + self.window)
                self.data["dirty"] = True
                self._save()

    def flush(self, force=False):
        with self.lock:
            if not self.data["dirty"] or (not force and self.clock() < self.due):
                return False
            report = {"schema_version": 1, "report_id": uuid.uuid4().hex, "sent_at_ms": int(time.time() * 1000),
                      "boot": self.data["boot"], "status": self.data["status"],
                      "observations": list(self.data["observations"].values()), "lines": self.data["lines"]}
            # Enqueue and clear the pending burst in one SQLite transaction.
            with self.store.lock, self.store.db:
                if self.store.db.execute("SELECT COUNT(*) FROM outbox").fetchone()[0] >= self.store.limit:
                    raise BufferError("MQTT_OUTBOX_CAPACITY")
                self.store.db.execute("INSERT INTO outbox(topic,payload,retain) VALUES(?,?,1)",
                                      (self.topic, json.dumps(report, separators=(",", ":"))))
                previous = self.data
                self.data = {**previous, "lines": [], "dirty": False}
                try:
                    self._save()
                except BaseException:
                    self.data = previous
                    raise
            self.due = None
            self.next_periodic = self.clock() + self.interval
            return True
