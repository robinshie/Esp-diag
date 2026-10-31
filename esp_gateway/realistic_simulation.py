"""Timed lab workload: command acceptance is separate from observed recovery."""
from __future__ import annotations

import json
import random
import time

from .store import fields


class RealisticSimulation:
    # Normalized lab events. Only account locks and administrative shutdowns
    # have implemented command executors; other recoveries represent maintenance.
    SCENARIOS = (
        ("AUTH_LOCKED", "admin", "WARN", "LOGIN_LOCKOUT", "failures=5 policy=LOGIN_LOCKOUT"),
        ("THERMAL", "cpu1", "CRITICAL", "HIGH_TEMPERATURE", "temp_c=88"),
        ("FAN_FAILURE", "fan1", "CRITICAL", "FAN_STOPPED", ""),
        ("PSU_FAILURE", "psu1", "WARN", "POWER_MODULE_FAILURE", ""),
        ("LINK_DOWN", "GigabitEthernet1/0/8", "WARN", "ADMIN_DOWN", ""),
        ("LINK_DOWN", "GigabitEthernet1/0/9", "WARN", "PHYSICAL_DOWN", ""),
        ("LOOP_DETECTED", "GigabitEthernet1/0/10", "CRITICAL", "LOOP_DETECTION", ""),
        ("STP_PROTECTION", "GigabitEthernet1/0/11", "WARN", "BPDU_TIMEOUT", ""),
        ("MAC_MOVE", "GigabitEthernet1/0/12", "WARN", "MAC_FLAPPING", ""),
        ("CPU_HIGH", "cpu_usage", "WARN", "CPU_THRESHOLD", ""),
        ("MEMORY_HIGH", "memory", "WARN", "MEMORY_THRESHOLD", ""),
        ("LOGIN_FAILURE", "login_service", "WARN", "AAA_FAILURE", ""),
    )
    # Lab probabilities, not measured H3C field failure rates. Transient load
    # and login issues are more common than a failed fan or power supply.
    OVERNIGHT_WEIGHTS = (8, 3, 1, 1, 4, 8, 2, 2, 5, 12, 6, 10)

    def __init__(self, gateway, clock=time.time):
        self.gateway = gateway
        self.clock = clock
        self.random = random.Random(getattr(gateway.args, "seed", 42))
        self.pending = {}
        self.next_lock = self.clock() + 8
        self.next_thermal = self.clock() + 120
        self.maintenance = {}
        self.workload = getattr(gateway.args, "workload", "standard")
        self.scenario_index = {}
        self.next_scenario = {}
        self.normal_min = getattr(gateway.args, "normal_min", 300)
        self.normal_max = getattr(gateway.args, "normal_max", 900)
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
                profile = (self.random.choices(("delayed", "retry", "cooldown"), weights=(85, 12, 3))[0]
                           if self.workload == "overnight" else ("delayed", "delayed", "retry", "cooldown")[count % 4])
                expiry = now + (self.random.uniform(60, 180)
                                if self.workload == "overnight" and action == "RECOVER_INTERFACE" else 30)
                record = dict(profile=profile, action=action, device=f["device"], component=f["component"],
                              fault=f["fault"], incident=f["incident"], started=now,
                              expires_at=expiry,
                              recovery_due=expiry if profile == "cooldown" else 0,
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
                    cause = ("POST_COMMAND_CHECK" if record["accepted"] else
                             "LOCKOUT_TIMER_EXPIRED" if record["action"] == "UNLOCK" else "SIMULATED_MAINTENANCE")
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
        if self.workload in {"all_faults", "overnight"}:
            self.sample_all_faults(overnight=self.workload == "overnight")
            return
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

    def maintenance_delay(self, fault, overnight):
        if not overnight:
            return self.random.uniform(20, 35)
        if fault in {"FAN_FAILURE", "PSU_FAILURE", "THERMAL"}:
            return self.random.uniform(60, 180)
        return self.random.uniform(20, 60)

    def sample_all_faults(self, overnight=False):
        g, now = self.gateway, self.clock()
        sources = g.topology.nodes if g.topology else {f"sim-{i + 1:02d}": {} for i in range(g.args.devices)}
        for device, metadata in sources.items():
            source = dict(device=device, kind=metadata.get("kind", "server"), rack=metadata.get("rack", "network"))
            active = g.world.setdefault(device, {})
            recovered = False
            # At most one maintenance event per sample avoids a log burst. The
            # independent command ledger handles locks and ADMIN_DOWN instead.
            for fault_key in list(active):
                component, fault = fault_key.split(":", 1)
                if fault == "AUTH_LOCKED" or (fault == "LINK_DOWN" and component == "GigabitEthernet1/0/8"):
                    continue
                maintenance_key = (device, fault_key)
                if maintenance_key not in self.maintenance:
                    self.maintenance[maintenance_key] = now + self.maintenance_delay(fault, overnight)
                due = self.maintenance[maintenance_key]
                if now >= due:
                    g.publish_log("INFO", "RECOVERY", "OK", component=component, fault=fault,
                                  cause="COOLING_COMPLETE" if fault == "THERMAL" else "SIMULATED_MAINTENANCE", **source)
                    self.maintenance[(device, fault_key)] = float("inf")
                    recovered = True
                    break
            if overnight and active:
                # No new faults while awaiting the ESP's settled recovery.
                # Start the full healthy interval only after confirmation.
                self.next_scenario.pop(device, None)
                g.publish_log("INFO", "HEARTBEAT", "OK", **source)
                continue
            if device not in self.next_scenario:
                self.next_scenario[device] = now + (self.random.uniform(self.normal_min, self.normal_max) if overnight else 4)
            due = self.next_scenario[device]
            if not recovered and now >= due:
                index = (self.random.choices(range(len(self.SCENARIOS)), weights=self.OVERNIGHT_WEIGHTS)[0]
                         if overnight else self.scenario_index.get(device, 0))
                fault, component, severity, cause, extra = self.SCENARIOS[index]
                fault_key = component + ":" + fault
                if fault_key not in active:
                    g.publish_log(severity, fault, "LOCKED" if fault == "AUTH_LOCKED" else "ALARM",
                                  extra, component=component, cause=cause, **source)
                    if fault != "AUTH_LOCKED" and cause != "ADMIN_DOWN":
                        self.maintenance[(device, fault_key)] = now + self.maintenance_delay(fault, overnight)
                self.scenario_index[device] = (index + 1) % len(self.SCENARIOS)
                if overnight:
                    self.next_scenario.pop(device, None)
                else:
                    self.next_scenario[device] = now + self.random.uniform(12, 18)
            g.publish_log("INFO", "HEARTBEAT", "OK", **source)

    def run(self):
        while not self.gateway.stop_event.wait(0.05):
            self.poll()
