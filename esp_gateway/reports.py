"""Persist one complete interval as one report; alarms do not bypass the clock."""
from __future__ import annotations

import json
import threading
import time
import uuid

from .store import fields
from esp_protocol.reporting_config import validate_interval
from esp_protocol.report_limits import MAX_REPORT_LINES, MAX_REPORT_BYTES


class ReportBatcher:
    def __init__(self, store, topic, batch_ms=250, clock=time.monotonic, interval_ms=60000, wall_clock=time.time):
        self.store, self.topic, self.clock = store, topic, clock
        self.wall_clock = wall_clock
        self.interval = validate_interval(interval_ms) / 1000
        self.lock = threading.RLock()
        with store.lock, store.db:
            store.db.execute("CREATE TABLE IF NOT EXISTS report_state(topic TEXT PRIMARY KEY, data TEXT)")
            row = store.db.execute("SELECT data FROM report_state WHERE topic=?", (topic,)).fetchone()
        self.data = json.loads(row[0]) if row else {"boot": None, "status": None, "observations": {}, "lines": [], "dirty": False}
        wall = self.wall_clock()
        deadline = self.data.get('next_report_at_ms', round((wall + self.interval) * 1000))
        self.next_periodic = self.clock() + max(0, min(self.interval, deadline / 1000 - wall))
        self.due = self.next_periodic if self.data['dirty'] else None
        self.data['next_report_at_ms'] = deadline
        with store.lock, store.db:
            self._save()

    def _save(self):
        self.store.db.execute("INSERT OR REPLACE INTO report_state VALUES(?,?)", (self.topic, json.dumps(self.data)))

    def set_interval(self, interval_ms):
        interval = validate_interval(interval_ms) / 1000
        with self.lock:
            if interval == self.interval:
                return False
            self.interval = interval
            self.next_periodic = self.clock() + interval
            if self.data["dirty"]:
                self.due = self.next_periodic
            self.data['next_report_at_ms'] = round((self.wall_clock() + interval) * 1000)
            with self.store.lock, self.store.db:
                self._save()
            return True

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
            with self.store.lock, self.store.db:
                previous = self.data
                self.data = {**previous, 'lines': list(previous['lines']), 'observations': dict(previous['observations'])}
                next_periodic = self.next_periodic
                if not previous['dirty'] and self.clock() > next_periodic:
                    # No data was collected during an unplugged/idle window.
                    # First data opens a full interval rather than an overdue burst.
                    next_periodic = self.clock() + self.interval
                    self.data['next_report_at_ms'] = round((self.wall_clock() + self.interval) * 1000)
                if channel == "serial":
                    if payload.startswith(("[BDI][BOOT]", "H3C PONG")) and f.get("boot") != self.data["boot"]:
                        self.data.update(boot=f.get("boot"), status=None, observations={})
                    self.data["lines"].append({"at_ms": round(self.wall_clock() * 1000), "text": payload})
                elif channel == "status":
                    self.data["status"] = f
                else:
                    self.data["observations"][f["device"]] = f
                self.data["dirty"] = True
                try:
                    if len(self.data['lines']) > MAX_REPORT_LINES or len(self._payload().encode('utf-8')) > MAX_REPORT_BYTES:
                        raise BufferError('REPORT_INTERVAL_CAPACITY: pending data retained; reduce interval or input rate')
                    self._save()
                except BaseException:
                    self.data = previous
                    raise
                self.next_periodic = self.due = next_periodic

    def _payload(self):
        return json.dumps({'schema_version': 1, 'report_id': uuid.uuid4().hex,
                          'sent_at_ms': round(self.wall_clock() * 1000),
                          'report_interval_ms': round(self.interval * 1000),
                          'boot': self.data['boot'], 'status': self.data['status'],
                          'observations': list(self.data['observations'].values()), 'lines': self.data['lines']},
                         ensure_ascii=False, separators=(',', ':'))

    def flush(self, force=False):
        with self.lock:
            if not self.data["dirty"] or (not force and self.clock() < self.due):
                return False
            payload = self._payload()
            # Enqueue and clear the pending burst in one SQLite transaction.
            with self.store.lock, self.store.db:
                if self.store.db.execute("SELECT COUNT(*) FROM outbox").fetchone()[0] >= self.store.limit:
                    raise BufferError("MQTT_OUTBOX_CAPACITY")
                self.store.db.execute("INSERT INTO outbox(topic,payload,retain) VALUES(?,?,1)",
                                      (self.topic, payload))
                previous = self.data
                self.data = {**previous, "lines": [], "dirty": False,
                             'next_report_at_ms': round((self.wall_clock() + self.interval) * 1000)}
                try:
                    self._save()
                except BaseException:
                    self.data = previous
                    raise
            self.due = None
            self.next_periodic = self.clock() + self.interval
            return True
