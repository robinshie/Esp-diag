"""Durable telemetry, source snapshots and action idempotency for the USB gateway."""
from __future__ import annotations

import json
import sqlite3
import threading
import time
from pathlib import Path


def fields(payload: str) -> dict[str, str]:
    return dict(token.split("=", 1) for token in payload.split() if "=" in token)


class Store:
    def __init__(self, path: str, limit: int = 50000):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.lock = threading.RLock()
        self.limit = limit
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS outbox(id INTEGER PRIMARY KEY, topic TEXT, payload TEXT, retain INTEGER);
            CREATE TABLE IF NOT EXISTS world(device TEXT PRIMARY KEY, data TEXT);
            CREATE TABLE IF NOT EXISTS actions(incident TEXT PRIMARY KEY, data TEXT);
            CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value INTEGER);
            CREATE TABLE IF NOT EXISTS usb_outbox(id INTEGER PRIMARY KEY AUTOINCREMENT, payload TEXT);
        """)
        with self.lock, self.db:
            epoch = max(time.time_ns() // 1000, self._meta("epoch") + 1)
            self.db.execute("INSERT OR REPLACE INTO meta VALUES('epoch',?)", (epoch,))
        self.epoch = epoch
        self.sequence = 0

    def _meta(self, key):
        row = self.db.execute("SELECT value FROM meta WHERE key=?", (key,)).fetchone()
        return row[0] if row else 0

    def envelope(self, device, kind="server", rack="rack-1"):
        with self.lock:
            self.sequence += 1
            return f"device={device} kind={kind} rack={rack} epoch={self.epoch} seq={self.sequence}"

    def enqueue(self, topic, payload, retain=False):
        with self.lock, self.db:
            if self.db.execute("SELECT COUNT(*) FROM outbox").fetchone()[0] >= self.limit:
                raise BufferError("MQTT_OUTBOX_CAPACITY: telemetry retained; stop input until drained")
            self.db.execute("INSERT INTO outbox(topic,payload,retain) VALUES(?,?,?)", (topic, payload, int(retain)))

    def first(self):
        with self.lock:
            return self.db.execute("SELECT id,topic,payload,retain FROM outbox ORDER BY id LIMIT 1").fetchone()

    def delivered(self, row_id):
        with self.lock, self.db:
            self.db.execute("DELETE FROM outbox WHERE id=?", (row_id,))

    def queue_input(self, payload):
        with self.lock, self.db:
            if self.db.execute("SELECT COUNT(*) FROM usb_outbox").fetchone()[0] >= self.limit:
                raise BufferError("USB_OUTBOX_CAPACITY")
            self.db.execute("INSERT INTO usb_outbox(payload) VALUES(?)", (payload,))

    def first_input(self):
        with self.lock:
            return self.db.execute("SELECT id,payload FROM usb_outbox ORDER BY id LIMIT 1").fetchone()

    def input_delivered(self, row_id):
        with self.lock, self.db:
            self.db.execute("DELETE FROM usb_outbox WHERE id=?", (row_id,))

    def reset_inputs(self, records):
        with self.lock, self.db:
            self.db.execute("DELETE FROM usb_outbox")
            self.db.executemany("INSERT INTO usb_outbox(payload) VALUES(?)", ((r,) for r in records))

    def observe(self, payload):
        f = fields(payload)
        device = f["device"]
        with self.lock, self.db:
            row = self.db.execute("SELECT data FROM world WHERE device=?", (device,)).fetchone()
            state = json.loads(row[0]) if row else {"faults": {}}
            state.update(kind=f["kind"], rack=f.get("rack", "rack-1"), seen=time.time())
            state["heartbeat"] = payload
            component = f.get("component", "system")
            if f["event"] in {"RECOVERY", "LINK_UP"}:
                key = component + ":" + f["fault"]
                # Retain both assertion and recovery through an ESP reboot so
                # the firmware can repeat the stabilization window.
                if key in state["faults"]:
                    state["faults"][key]["recovery"] = payload
            elif f["status"] in {"ALARM", "LOCKED"}:
                state["faults"][component + ":" + f["event"]] = {"alarm": payload}
            self.db.execute("INSERT OR REPLACE INTO world VALUES(?,?)", (device, json.dumps(state)))

    def reconciled(self, payload):
        # A partial snapshot (or an older heartbeat already in flight) cannot
        # confirm a queued recovery. Only the firmware's settled incident can.
        if not payload.startswith("[BDI][RECOVERED]"):
            return
        f = fields(payload)
        with self.lock, self.db:
            row = self.db.execute("SELECT data FROM world WHERE device=?", (f["device"],)).fetchone()
            if not row:
                return
            state = json.loads(row[0])
            key = f["component"] + ":" + f["fault"]
            fault = state["faults"].get(key)
            if fault and "recovery" in fault:
                alarm = fields(fault["alarm"])
                incident = alarm.get("incident", alarm["epoch"] + "-" + alarm["seq"])
                if incident == f.get("incident"):
                    del state["faults"][key]
            self.db.execute("UPDATE world SET data=? WHERE device=?", (json.dumps(state), f["device"]))

    def observed_ms(self, device):
        with self.lock:
            row = self.db.execute("SELECT data FROM world WHERE device=?", (device,)).fetchone()
            return int(json.loads(row[0])["seen"] * 1000) if row else 0

    def active_incident(self, device, component, event):
        f = self.active_fault(device, component, event)
        return f.get("incident", f["epoch"] + "-" + f["seq"]) if f else None

    def active_fault(self, device, component, event):
        with self.lock:
            row = self.db.execute("SELECT data FROM world WHERE device=?", (device,)).fetchone()
            fault = json.loads(row[0])["faults"].get(component + ":" + event) if row else None
            if not fault:
                return None
            return fields(fault["alarm"])

    def replay(self):
        with self.lock:
            rows = self.db.execute("SELECT device,data FROM world ORDER BY device").fetchall()
        for device, raw in rows:
            state = json.loads(raw)
            age = max(0, int((time.time() - state["seen"]) * 1000))
            records = []
            for fault in state["faults"].values():
                records.append(fault["alarm"])
                if "recovery" in fault:
                    records.append(fault["recovery"])
            # New sequence numbers preserve replay ordering, while incident
            # remains the original identity for action deduplication.
            if not records:
                records.append(state["heartbeat"])
            for payload in records:
                f = fields(payload)
                incident = f.get("incident", f["epoch"] + "-" + f["seq"])
                body = " ".join(f"{k}={v}" for k, v in f.items()
                                if k not in {"device", "kind", "rack", "epoch", "seq", "age_ms", "incident", "replay"})
                yield ("H3C LOG " + self.envelope(device, state["kind"], state["rack"]) +
                       f" age_ms={age} incident={incident} replay=true " + body)

    def action(self, incident):
        with self.lock:
            row = self.db.execute("SELECT data FROM actions WHERE incident=?", (incident,)).fetchone()
            return json.loads(row[0]) if row else None

    def bind_deployment(self, mode, identity):
        with self.lock, self.db:
            self.db.execute("CREATE TABLE IF NOT EXISTS deployment(id INTEGER PRIMARY KEY CHECK(id=1), mode TEXT, identity TEXT)")
            existing = self.db.execute("SELECT mode,identity FROM deployment WHERE id=1").fetchone()
            if existing and existing != (mode, identity):
                raise ValueError("state database belongs to another mode/agent; configure a separate state_db")
            self.db.execute("INSERT OR IGNORE INTO deployment VALUES(1,?,?)", (mode, identity))

    def save_action(self, incident, result):
        with self.lock, self.db:
            self.db.execute("INSERT OR REPLACE INTO actions VALUES(?,?)", (incident, json.dumps(result)))

    def close(self):
        with self.lock:
            self.db.close()
