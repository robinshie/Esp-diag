"""Real SSH collectors and an explicitly authorized action executor."""
from __future__ import annotations

import logging
import queue
import threading
import time
from types import SimpleNamespace

from .config import credential
from .ssh import SSHSession, collect, evaluate
from .store import fields
from .transport import GatewayTransport

log = logging.getLogger(__name__)


def transport_args(config):
    simulation = config.get("simulation", {})
    return SimpleNamespace(
        state_db=config["storage"]["state_db"], port=config["serial"]["port"], baud=config["serial"]["baud"],
        host=config["mqtt"]["host"], mqtt_port=config["mqtt"]["port"], user=config["mqtt"]["username"],
        password=credential(config["mqtt"]), gateway_id=config["mqtt"]["client_id"], tls=config["mqtt"]["tls"],
        agent_id=config["agent"]["id"], topic_root=config["agent"]["topic_root"], serial_log=True,
        # A deployed gateway must remain available when USB is plugged in later.
        # The reader retries missing ports and handshakes until shutdown.
        protocol=config["protocol"], publish_source_topics=False, handshake_timeout=0, worker_shutdown_timeout=120,
        batch_reports=True, report_batch_ms=config.get("reporting", {}).get("batch_ms", 250),
        report_interval_ms=config.get("reporting", {}).get("interval_ms", 5000),
        reporting_config_path=config.get("_config_path"),
        duration=0, skip_smoke=True, topology=None, min_interval=simulation.get("min_interval", 3),
        max_interval=simulation.get("max_interval", 5), fault_rate=simulation.get("fault_rate", 0.15),
        devices=simulation.get("devices", 1), seed=simulation.get("seed", 42), action_mode=simulation.get("action_mode", "realistic"),
        workload=simulation.get("workload", "standard"),
        normal_min=simulation.get("normal_min", 300), normal_max=simulation.get("normal_max", 900))


class SSHGateway(GatewayTransport):
    def __init__(self, config, session_factory=SSHSession):
        self.config = config
        self.devices = {device["id"]: device for device in config["devices"]}
        self.session_factory = session_factory
        self.actions = queue.Queue(maxsize=32)
        self.queued = set()
        self.device_locks = {device["id"]: threading.RLock() for device in config["devices"]}
        super().__init__(transport_args(config))
        try:
            if set(self.world) - set(self.devices):
                raise ValueError("state database contains unconfigured devices; use the correct production state_db")
            self.store.bind_deployment("ssh", self.agent_topic)
        except BaseException:
            self.store.close()
            self.instance_lock.close()
            raise

    def sample(self):
        pass  # Collection is isolated per device; a dead SSH host cannot stall USB/MQTT.

    def extra_workers(self):
        return tuple((lambda device=device: self.collector(device)) for device in self.devices.values()) + (self.executor,)

    def collector(self, device):
        while not self.stop_event.is_set():
            if not self.pong_seen.wait(0.2):
                continue
            started = time.monotonic()
            try:
                with self.device_locks[device["id"]]:
                    with self.session_factory(device["ssh"]) as session:
                        observations = collect(device, session)
                    # All checks must succeed before source freshness advances.
                    with self.input_lock:
                        for check, alarm, cause in observations:
                            self.observe(device, check, alarm, cause)
                        self.publish_log("INFO", "HEARTBEAT", "OK", device=device["id"], kind=device["kind"], rack=device["rack"])
            except Exception as exc:
                log.warning("collector device=%s failed=%s; source freshness not advanced", device["id"], type(exc).__name__)
            self.stop_event.wait(max(0.2, device["poll_interval"] - (time.monotonic() - started)))

    def observe(self, device, check, alarm, cause=""):
        active = self.world.get(device["id"], {})
        source = dict(device=device["id"], kind=device["kind"], rack=device["rack"], component=check["component"])
        if alarm:
            self.publish_log(check["severity"], check["event"], "LOCKED" if check["event"] == "AUTH_LOCKED" else "ALARM", cause=cause, **source)
        elif check["component"] + ":" + check["event"] in active:
            self.publish_log("INFO", "RECOVERY", "OK", fault=check["event"], cause="SSH_OBSERVATION", **source)

    def execute_action(self, line):
        f = fields(line)
        action = line.split()[1]
        device = self.devices.get(f.get("device"))
        policy = (device or {}).get("actions", {}).get(action, {})
        fault = "AUTH_LOCKED" if action == "UNLOCK" else "LINK_DOWN" if action == "RECOVER_INTERFACE" else None
        key = f"{f.get('device')}:{f.get('component')}:{f.get('fault')}:{f.get('incident')}"
        permitted = (policy.get("enabled") is True and f.get("component") in policy.get("allowed_components", [])
                     and fault == f.get("fault") and self.store.active_incident(f.get("device"), f.get("component"), fault) == f.get("incident"))
        if action == "RECOVER_INTERFACE":
            actual = self.store.active_fault(f.get("device"), f.get("component"), "LINK_DOWN") or {}
            permitted = permitted and actual.get("cause") == "ADMIN_DOWN"
        with self.input_lock:
            record = self.store.action(key)
            if record and record.get("adapter") != "ssh":
                permitted = False  # Never reuse a simulated action ledger on real equipment.
            if permitted and (not record or record.get("phase") not in {"FAILED", "DENIED"}):
                if key not in self.queued:
                    if self.actions.full():
                        permitted = False
                    else:
                        execute = record is None
                        record = record or {"adapter": "ssh", "phase": "CLAIMED", "at": time.time()}
                        # Claim before acknowledging. Ambiguous restarts verify
                        # the current state and never repeat a claimed mutation.
                        self.store.save_action(key, record)
                        self.queued.add(key)
                        f["_deadline"] = time.monotonic() + (self.config["protocol"]["verify_ms"] - self.config["protocol"]["settle_ms"]) / 1000
                        self.actions.put_nowait((key, f, action, execute))
            else:
                permitted = False
            self.send_usb(f"H3C ACK action={action} request_id={f['request_id']} device={f['device']} "
                          f"component={f['component']} status={'OK' if permitted else 'FAILED'}")
            log.info("action device=%s action=%s incident=%s accepted=%s", f.get("device"), action, f.get("incident"), permitted)

    def executor(self):
        while not self.stop_event.is_set():
            try:
                key, f, action, execute = self.actions.get(timeout=0.2)
            except queue.Empty:
                continue
            device = self.devices[f["device"]]
            try:
                with self.device_locks[device["id"]]:
                    def within_window():
                        if time.monotonic() >= f["_deadline"]:
                            raise TimeoutError("action verification window expired while queued/executing")

                    within_window()
                    if self.store.active_incident(device["id"], f["component"], f["fault"]) != f["incident"]:
                        raise RuntimeError("action incident is no longer active")
                    check = next(check for check in device["checks"] if check["component"] == f["component"] and check["event"] == f["fault"])
                    with self.session_factory(device["ssh"]) as session:
                        alarm, cause = evaluate(check, session.command(check["command"]))
                        if execute and alarm:
                            if action == "RECOVER_INTERFACE" and cause != "ADMIN_DOWN":
                                raise RuntimeError("current interface state does not permit administrative repair")
                            for template in device["actions"][action]["commands"]:
                                within_window()
                                if self.stop_event.is_set():
                                    raise InterruptedError("shutdown during action")
                                session.command(template.format(component=f["component"]))
                            within_window()
                            alarm, cause = evaluate(check, session.command(check["command"]))
                    within_window()
                    if alarm:
                        raise RuntimeError("configured recovery did not restore the monitored state")
                    with self.input_lock:
                        self.observe(device, check, False)
                self.store.save_action(key, {"adapter": "ssh", "phase": "VERIFIED", "at": time.time()})
                log.info("action device=%s incident=%s verification=HEALTHY", device["id"], f["incident"])
            except Exception as exc:
                self.store.save_action(key, {"adapter": "ssh", "phase": "FAILED", "at": time.time()})
                log.warning("action device=%s incident=%s verification=FAILED error=%s; firmware fault retained",
                            device["id"], f["incident"], type(exc).__name__)
            finally:
                with self.input_lock:
                    self.queued.discard(key)
                self.actions.task_done()


def build_gateway(config):
    if config["mode"] == "ssh":
        return SSHGateway(config)
    from .simulation import UsbMqttGateway
    gateway = UsbMqttGateway(transport_args(config))
    try:
        gateway.store.bind_deployment("demo", gateway.agent_topic)
    except BaseException:
        gateway.store.close()
        gateway.instance_lock.close()
        raise
    return gateway
