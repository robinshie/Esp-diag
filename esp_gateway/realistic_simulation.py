"""Timed lab workload: command acceptance is separate from observed recovery."""
from __future__ import annotations

import json
import random
import time

from .store import fields


class RealisticSimulation:
    def __init__(self, gateway, clock=time.time):
        self.gateway = gateway
        self.clock = clock
        self.random = random.Random(getattr(gateway.args, "seed", 42))
        self.pending = {}
        self.next_lock = self.clock() + 8
        self.next_thermal = self.clock() + 120
        self.maintenance = {}
        # Reconnects must not lose a pending recovery or a policy expiry. USB
        # replies wait for a fresh request; accepted operations are not repeated.
        with gateway.store.lock:
            records = gateway.store.db.execute("SELECT incident,data FROM actions").fetchall()
        for key, raw in records:
            record = json.loads(raw)
            if "profile" in record and not record["recovery_sent"]:
                self.pending[key] = dict(record=record, request={}, attempt_key=None, ack_due=0, ack_sent=True)

    def submit(self, line):
        f = fields(line)
        action = line.split()[1]
        if action not in {"UNLOCK", "RECOVER_INTERFACE"}:
            return
        g = self.gateway
        key = f"{f['device']}:{f['component']}:{f['fault']}:{f['incident']}"
        with g.input_lock:
            now = self.clock()
            record = g.store.action(key)
            if not record or "profile" not in record:
                with g.store.lock:
                    count = g.store.db.execute("SELECT COUNT(*) FROM actions WHERE data LIKE '%\"profile\"%'").fetchone()[0]
                # Most operations work, some require a retry; a policy lock can
                # reject all three attempts and expire later on its own.
                profile = ("delayed", "delayed", "retry", "cooldown")[count % 4]
                record = dict(profile=profile, action=action, device=f["device"], component=f["component"],
                              fault=f["fault"], incident=f["incident"], started=now,
                              expires_at=now + 30,
                              recovery_due=now + 30 if profile == "cooldown" else 0,
                              recovery_sent=False, accepted=False)
                g.store.save_action(key, record)
            old = self.pending.get(key)
            attempt_key = (f["request_id"], f.get("attempt", "1"))
            if old and old["attempt_key"] == attempt_key:
                return
            self.pending[key] = dict(record=record, request=f, attempt_key=attempt_key,
                                     ack_due=now + self.random.uniform(0.7, 1.2), ack_sent=False)

    def poll(self):
        g = self.gateway
        with g.input_lock:
            now = self.clock()
            for key, pending in list(self.pending.items()):
                record, request = pending["record"], pending["request"]
                if not pending["ack_sent"] and now >= pending["ack_due"]:
                    pending["ack_sent"] = True
                    lost_first_reply = (record["profile"] == "retry" and not record["accepted"]
                                        and request.get("attempt", "1") == "1")
                    if not lost_first_reply:
                        failed = record["profile"] == "cooldown"
                        g.send_usb(f"H3C ACK action={record['action']} request_id={request['request_id']} "
                                   f"device={record['device']} component={record['component']} "
                                   f"status={'FAILED' if failed else 'OK'}")
                        if not failed and not record["accepted"]:
                            record["accepted"] = True
                            # The ESP's independent verification deadline is
                            # 10s; leave room for its 2s stabilization window.
                            record["recovery_due"] = now + self.random.uniform(5.5, 6.5)
                        g.store.save_action(key, record)
                due = record["recovery_due"] or record.get("expires_at", record["started"] + 30)
                if due and not record["recovery_sent"] and now >= due:
                    cause = "POST_COMMAND_CHECK" if record["accepted"] else "LOCKOUT_TIMER_EXPIRED"
                    metadata = g.states.get(record["device"], {})
                    g.publish_log("INFO", "RECOVERY", "OK", device=record["device"],
                                  component=record["component"], fault=record["fault"], cause=cause,
                                  kind=metadata.get("kind", "server"), rack=metadata.get("rack", "network"))
                    record["recovery_sent"] = True
                    g.store.save_action(key, record)
                    del self.pending[key]
                elif record["recovery_sent"] and pending["ack_sent"]:
                    del self.pending[key]

    def sample(self):
        g = self.gateway
        now = self.clock()
        sources = g.topology.nodes if g.topology else {f"sim-{i + 1:02d}": {} for i in range(g.args.devices)}
        for device, metadata in sources.items():
            source = dict(device=device, kind=metadata.get("kind", "server"), rack=metadata.get("rack", "network"))
            active = g.world.setdefault(device, {})
            if now >= self.next_lock and "admin:AUTH_LOCKED" not in active:
                g.publish_log("WARN", "AUTH_LOCKED", "LOCKED", "failures=5 policy=LOGIN_LOCKOUT",
                              component="admin", **source)
                self.next_lock = now + self.random.uniform(60, 100)
            if now >= self.next_thermal and "cpu1:THERMAL" not in active:
                g.publish_log("CRITICAL", "THERMAL", "ALARM", "temp_c=88", component="cpu1", **source)
                self.maintenance[(device, "cpu1:THERMAL")] = now + self.random.uniform(25, 40)
                self.next_thermal = now + self.random.uniform(150, 210)
            for fault_key in list(active):
                if fault_key.endswith(":AUTH_LOCKED"):
                    continue  # Never randomly clear an account lock.
                due = self.maintenance.setdefault((device, fault_key), now + 60)
                if now >= due:
                    component, fault = fault_key.split(":", 1)
                    g.publish_log("INFO", "RECOVERY", "OK", component=component, fault=fault,
                                  cause="COOLING_COMPLETE" if fault == "THERMAL" else "SIMULATED_MAINTENANCE", **source)
                    self.maintenance[(device, fault_key)] = float("inf")
            g.publish_log("INFO", "HEARTBEAT", "OK", **source)

    def run(self):
        while not self.gateway.stop_event.wait(0.05):
            self.poll()
