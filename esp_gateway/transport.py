#!/usr/bin/env python3
"""Reliable USB/MQTT transport shared by SSH and demo gateways (protocol 2)."""
from __future__ import annotations

import argparse
import logging
import fcntl
import json
import random
import secrets
import signal
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial
from .store import Store, fields
from .topology import Topology

TOPIC_LOG = "h3c/server/sim-01/log"  # legacy import only; runtime topics use device identity
TOPIC_HEALTH = "h3c/server/sim-01/health"
AGENT = "h3c/esp32-s3-eth"
DEVICE_OFFLINE_TIMEOUT = 15.0
EVENTS = {
    "THERMAL": ("cpu1", "CRITICAL"), "FAN_FAILURE": ("fan1", "CRITICAL"),
    "PSU_FAILURE": ("psu1", "WARN"), "AUTH_LOCKED": ("admin", "WARN"),
}


class GatewayTransport:
    def __init__(self, args):
        self.args = args
        self.stop_event = threading.Event()
        self.mqtt_connected = threading.Event()
        self.pong_seen = threading.Event()
        self.serial_lock = threading.RLock()
        self.input_lock = threading.RLock()
        self.receipts = {}
        self.receipt_condition = threading.Condition()
        self.serial = None
        self.last_device_seen = 0.0
        self.device_online = False
        self.boot = None
        self.nonce = None
        self.last_ping_sent = 0.0
        self.logs_sent = self.mqtt_reports = self.unlock_acks = 0
        self.error = None
        self.topology = Topology.load(args.topology) if getattr(args, "topology", None) else None
        self.states = {}
        self.service_states = {}
        self.pending_replay = threading.Event()
        # Lock before opening the DB: two gateways may not share a serial link,
        # client ID or outbox. Release automatically when process exits.
        Path(args.state_db).parent.mkdir(parents=True, exist_ok=True)
        self.instance_lock = open(args.state_db + ".lock", "a")
        try:
            fcntl.flock(self.instance_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.instance_lock.close()
            raise
        self.store = Store(args.state_db)
        self.reports = None
        if getattr(args, "batch_reports", False):
            from .reports import ReportBatcher
            self.reports = ReportBatcher(self.store, self.agent_topic + "/report", getattr(args, "report_batch_ms", 250),
                                         interval_ms=getattr(args, "report_interval_ms", 5000))
        self.world = {}  # restored from the durable source snapshot on start
        for device, raw in self.store.db.execute("SELECT device,data FROM world"):
            self.world[device] = dict(json.loads(raw)["faults"])
        self.mqtt = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=args.gateway_id)
        self.mqtt.username_pw_set(args.user, args.password)
        tls = getattr(args, "tls", {})
        if tls.get("enabled"):
            self.mqtt.tls_set(ca_certs=tls.get("ca_file"), certfile=tls.get("cert_file"), keyfile=tls.get("key_file"))
        self.mqtt.will_set(self.agent_topic + "/availability", "state=OFFLINE reason=GATEWAY_DISCONNECTED", qos=1, retain=True)
        self.mqtt.reconnect_delay_set(1, 10)
        self.mqtt.on_connect = self._on_mqtt_connect
        self.mqtt.on_disconnect = self._on_mqtt_disconnect
        self.threads = []


    def _on_mqtt_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            self.mqtt_connected.set()
            # Queue fresh retained state after any backlog; publication order is
            # governed by a single outbox consumer.
            try:
                self.publish_mqtt(self.agent_topic + "/availability",
                                  "state=" + ("ONLINE" if time.monotonic() - self.last_device_seen < 15 else "OFFLINE"), True)
                settings = getattr(self.args, "protocol", {})
                self.publish_mqtt(self.agent_topic + "/status", "gateway=ONLINE transport=USB protocol=2 " +
                                  f"source_ttl_ms={settings.get('stale_ms', 90000)}", True)
            except Exception as exc:
                self.error = exc
                self.stop_event.set()
        else:
            print(f"MQTT rejected: {reason_code}")


    def _on_mqtt_disconnect(self, *args):
        self.mqtt_connected.clear()


    def publish_mqtt(self, topic, payload, retain=False, wait=True):
        reports = getattr(self, "reports", None)
        prefix = self.agent_topic + "/"
        if reports is not None and topic.startswith(prefix):
            channel = topic[len(prefix):]
            if channel not in {"availability", "report", "correlations"}:
                reports.ingest(channel, payload)
                return
        self.store.enqueue(topic, payload, retain)

    def _reporter(self):
        while not self.stop_event.wait(0.02):
            self.reports.flush()


    def _publisher(self):
        while not self.stop_event.is_set():
            if not self.mqtt_connected.wait(0.1):
                continue
            row = self.store.first()
            if not row:
                self.stop_event.wait(0.02)
                continue
            row_id, topic, payload, retain = row
            try:
                result = self.mqtt.publish(topic, payload, qos=1, retain=bool(retain))
                result.wait_for_publish(timeout=1)
                if result.is_published():
                    self.store.delivered(row_id)
                    self.mqtt_reports += 1
            except (RuntimeError, ValueError, OSError):
                self.stop_event.wait(0.1)


    def _open_serial(self):
        port = serial.Serial(port=None, baudrate=self.args.baud, timeout=0.01, write_timeout=1)
        port.dtr = True
        port.rts = False
        port.exclusive = True
        port.port = self.args.port
        port.open()
        self.serial = port
        # The simulator's deterministic seed must not reuse a USB connection
        # identity when the gateway process restarts.
        self.nonce = secrets.token_hex(16)
        self.last_ping_sent = time.monotonic()
        self.send_usb(f"H3C PING nonce={self.nonce}")


    def send_usb(self, line):
        with self.serial_lock:
            if self.serial is None or not self.serial.is_open:
                self.pending_replay.set()
                return False
            try:
                self.serial.write((line + "\n").encode())
                # write_timeout bounds OS writes. tcdrain()/flush() can block
                # indefinitely on a disconnected driver; RECEIPT provides the
                # stronger end-to-end delivery confirmation instead.
                return True
            except (serial.SerialException, OSError):
                self.serial.close()
                self.pending_replay.set()
                return False


    def queue_usb(self, line):
        self.store.queue_input(line)


    def _sender(self):
        try:
            while not self.stop_event.is_set():
                if not self.pong_seen.wait(0.1) or self.pending_replay.is_set():
                    self.stop_event.wait(0.02)
                    continue
                row = self.store.first_input()
                if row is None:
                    self.stop_event.wait(0.02)
                    continue
                row_id, payload = row
                f = fields(payload)
                key = (f["device"], f["epoch"], f["seq"])
                for attempt in range(3):
                    if self.stop_event.is_set():
                        break
                    if not self.send_usb(payload):
                        break
                    with self.receipt_condition:
                        self.receipt_condition.wait_for(lambda: key in self.receipts or self.stop_event.is_set(), timeout=2)
                        result = self.receipts.pop(key, None)
                    if result in {"ACCEPTED", "DUPLICATE"}:
                        self.store.input_delivered(row_id)
                        break
                    if result == "REJECTED":
                        raise RuntimeError("FIRMWARE_REJECTED_INPUT: " + payload)
                else:
                    # Force a new handshake and snapshot reconciliation. The
                    # original input remains durable until replay replaces it.
                    with self.serial_lock:
                        self.pong_seen.clear()
                        if self.serial:
                            self.serial.close()
                    self.pending_replay.set()
        except Exception as exc:
            self.error = exc
            self.stop_event.set()


    def _handle_line(self, line):
        logging.getLogger(__name__).info("ESP32 -> GATEWAY %s", line)
        if getattr(self.args, "serial_log", False):
            self.publish_mqtt(self.agent_topic + "/serial", line)
        self.last_device_seen = time.monotonic()
        if not self.device_online:
            self.device_online = True
            self.publish_mqtt(self.agent_topic + "/availability", "state=ONLINE reason=USB_ACTIVE", True)
        f = fields(line)
        if line.startswith("[BDI][RECEIPT]"):
            with self.receipt_condition:
                if len(self.receipts) < 128:
                    self.receipts[(f["device"], f["epoch"], f["seq"])] = f["result"]
                self.receipt_condition.notify_all()
            return
        if line.startswith("H3C PONG"):
            if f.get("nonce") == self.nonce:
                first = not self.pong_seen.is_set() or self.boot != f.get("boot")
                self.boot = f.get("boot")
                settings = getattr(self.args, "protocol", None)
                if settings:
                    self.pong_seen.clear()
                    self.send_usb("H3C CONFIG nonce=" + self.nonce + " " + " ".join(f"{key}={value}" for key, value in settings.items()))
                    return
                self.pong_seen.set()
                if first:
                    self.pending_replay.set()
            return
        if line.startswith("H3C CONFIGURED"):
            settings = getattr(self.args, "protocol", {})
            if f.get("nonce") == self.nonce and all(f.get(key) == str(value) for key, value in settings.items()):
                self.pong_seen.set()
                self.pending_replay.set()
            return
        if line.startswith("[BDI][BOOT]"):
            self.boot = f.get("boot")
            self.pong_seen.clear()
            self.last_ping_sent = 0
            self.pending_replay.set()
            return
        if line.startswith("[BDI][RECOVERED]"):
            self.store.reconciled(line)
            with self.input_lock:
                self.world.get(f["device"], {}).pop(f["component"] + ":" + f["fault"], None)
            self.publish_mqtt(self.agent_topic + "/action", line)
            return
        if line.startswith("[BDI][STATE]"):
            device, kind = f["device"], f["kind"]
            seen = self.store.observed_ms(device)
            if getattr(self.args, "publish_source_topics", True):
                root = getattr(self.args, "topic_root", "h3c")
                self.publish_mqtt(f"{root}/{kind}/{device}/health", line + f" seen_at_ms={seen}", True)
                self.publish_mqtt(f"{root}/{kind}/{device}/availability",
                                  f"state={f['availability']} device={device} seen_at_ms={seen}", True)
            self.publish_mqtt(f"{self.agent_topic}/observation/{device}", line + f" seen_at_ms={seen}", True)
            self.states[device] = f
            if self.topology:
                self._publish_cluster()
        elif line.startswith("[BDI][CMD]"):
            self.publish_mqtt(self.agent_topic + "/cmd/" + line.split()[1].lower(), line)
            self.execute_action(line)
        elif line.startswith("[BDI][EVENT]"):
            self.publish_mqtt(self.agent_topic + "/event", line)
        elif line.startswith("[BDI][STATUS]"):
            ttl = getattr(self.args, "protocol", {}).get("stale_ms", 90000)
            self.publish_mqtt(self.agent_topic + "/status", line +
                              f" seen_at_ms={int(time.time() * 1000)} source_ttl_ms={ttl} connection_id={self.nonce}", True)
        else:
            self.publish_mqtt(self.agent_topic + "/action", line)


    def _publish_cluster(self):
        result = self.topology.evaluate(self.states)
        for service, state in result["services"].items():
            payload = (f"state={'NORMAL' if state['state'] == 'AVAILABLE' else 'FAULT'} "
                       f"service_state={state['state']} availability=ONLINE healthy={state['healthy']} "
                       f"required={state['required']}")
            # Publish periodically as well as on change, so retained snapshots
            # cannot stay apparently fresh after the gateway has stopped.
            previous, seen = self.service_states.get(service, (None, 0))
            if previous != payload or time.monotonic() - seen >= 20:
                self.service_states[service] = (payload, time.monotonic())
                self.publish_mqtt(f"{getattr(self.args, 'topic_root', 'h3c')}/service/{service}/health", payload + f" seen_at_ms={int(time.time() * 1000)}", True)
        correlations = json.dumps(result["correlations"], sort_keys=True, separators=(",", ":"))
        if correlations != getattr(self, "last_correlations", None):
            self.last_correlations = correlations
            self.publish_mqtt(self.agent_topic + "/correlations", correlations, True)


    def _reader(self):
        buffer = bytearray()
        discard = False
        while not self.stop_event.is_set():
            try:
                if self.serial is None or not self.serial.is_open:
                    with self.serial_lock:
                        self._open_serial()
                    buffer.clear()
                    discard = False
                with self.serial_lock:
                    data = self.serial.read(min(1024, self.serial.in_waiting or 1))
                for byte in data:
                    if byte == 10:
                        if not discard and buffer:
                            self._handle_line(buffer.decode(errors="replace").strip())
                        buffer.clear()
                        discard = False
                    elif byte != 13 and not discard:
                        if len(buffer) >= 8192:
                            buffer.clear()
                            discard = True
                            self.publish_mqtt(self.agent_topic + "/action", "reason=USB_OUTPUT_TOO_LONG")
                        else:
                            buffer.append(byte)
                if self.pending_replay.is_set() and self.pong_seen.is_set():
                    with self.input_lock:
                        self.store.reset_inputs(list(self.store.replay()))
                        self.pending_replay.clear()
                if not self.pong_seen.is_set() and time.monotonic() - self.last_ping_sent >= 1:
                    self.last_ping_sent = time.monotonic()
                    self.send_usb(f"H3C PING nonce={self.nonce}")
                if self.device_online and time.monotonic() - self.last_device_seen >= 15:
                    self.device_online = False
                    self.pong_seen.clear()
                    self.publish_mqtt(self.agent_topic + "/availability", "state=OFFLINE reason=USB_STATUS_TIMEOUT", True)
                # Yield outside the serial lock so writers cannot be starved
                # by an idle read loop reacquiring the lock immediately.
                self.stop_event.wait(0.001)
            except (serial.SerialException, OSError):
                with self.serial_lock:
                    if self.serial:
                        self.serial.close()
                    self.serial = None
                    self.pong_seen.clear()
                self.device_online = False
                self.publish_mqtt(self.agent_topic + "/availability", "state=OFFLINE reason=USB_DISCONNECTED", True)
                self.stop_event.wait(1)
            except Exception as exc:
                self.error = exc
                self.stop_event.set()


    def publish_log(self, level, event, status, extra="", *, device="sim-01", component=None,
                    fault=None, kind="server", rack="rack-1", cause=""):
        with self.input_lock:
            return self._publish_log(level, event, status, extra, device=device, component=component,
                                     fault=fault, kind=kind, rack=rack, cause=cause)


    def _publish_log(self, level, event, status, extra="", *, device="sim-01", component=None,
                     fault=None, kind="server", rack="rack-1", cause=""):
        component = component or EVENTS.get(event, ("system", "INFO"))[0]
        envelope = self.store.envelope(device, kind, rack)
        payload = f"H3C LOG {envelope} level={level} event={event} status={status} component={component}"
        if fault:
            payload += f" fault={fault}"
        if cause:
            payload += f" cause={cause}"
        if extra:
            payload += " " + extra
        f = fields(payload)
        if status in {"ALARM", "LOCKED"}:
            incident = self.store.active_incident(device, component, event) or f"{f['epoch']}-{f['seq']}"
            payload += f" incident={incident}"
        self.store.observe(payload)
        active = self.world.setdefault(device, {})
        if status in {"ALARM", "LOCKED"}:
            active[component + ":" + event] = True
        if getattr(self.args, "publish_source_topics", True) and (status in {"ALARM", "LOCKED"} or event in {"RECOVERY", "LINK_UP"}):
            self.publish_mqtt(f"{getattr(self.args, 'topic_root', 'h3c')}/{kind}/{device}/log", payload)
        self.queue_usb(payload)
        self.logs_sent += 1
        return payload


    def run(self):
        random.seed(self.args.seed)
        self.mqtt.connect_async(self.args.host, self.args.mqtt_port, keepalive=30)
        self.mqtt.loop_start()
        def worker(target):
            try:
                target()
            except Exception as exc:
                self.error = exc
                self.stop_event.set()
        targets = [self._reader, self._publisher, self._sender, *self.extra_workers()]
        if self.reports is not None:
            targets.append(self._reporter)
        self.threads = [threading.Thread(target=worker, args=(target,), daemon=True) for target in targets]
        for thread in self.threads:
            thread.start()
        started = time.monotonic()
        try:
            while not self.stop_event.is_set() and not self.pong_seen.wait(0.2):
                if getattr(self.args, "handshake_timeout", 0) and time.monotonic() - started >= self.args.handshake_timeout:
                    raise TimeoutError("ESP32 handshake/configuration failed; check firmware and serial port")
                if self.args.duration and time.monotonic() - started >= self.args.duration:
                    return 0
            if self.stop_event.is_set():
                if self.error:
                    raise self.error
                return 0
            self.initial_sample()
            while not self.stop_event.is_set():
                if self.args.duration and time.monotonic() - started >= self.args.duration:
                    break
                self.sample()
                self.stop_event.wait(random.uniform(self.args.min_interval, self.args.max_interval))
            if self.error:
                raise self.error
        finally:
            self.stop_event.set()
            for thread in self.threads:
                thread.join(timeout=getattr(self.args, "worker_shutdown_timeout", 5))
            if self.reports is not None:
                try:
                    self.reports.flush(force=True)
                except Exception as exc:
                    logging.getLogger(__name__).error("pending report retained: %s", type(exc).__name__)
            if self.mqtt_connected.is_set():
                # Graceful MQTT disconnect does not send the Last Will.
                result = self.mqtt.publish(self.agent_topic + "/availability", "state=OFFLINE reason=GATEWAY_STOPPED", qos=1, retain=True)
                try:
                    result.wait_for_publish(timeout=1)
                except (RuntimeError, ValueError):
                    pass
            self.mqtt.disconnect()
            self.mqtt.loop_stop()
            if self.serial:
                self.serial.close()
            self.store.close()
            self.instance_lock.close()
            print(f"Summary: logs_sent={self.logs_sent} mqtt_reports={self.mqtt_reports} unlock_acks={self.unlock_acks}")
        return 0


    @property
    def agent_topic(self):
        return f"{getattr(self.args, 'topic_root', 'h3c')}/{getattr(self.args, 'agent_id', 'esp32-s3-eth')}"

    def extra_workers(self):
        return ()

    def initial_sample(self):
        pass

    def sample(self):
        raise NotImplementedError

    def execute_action(self, line):
        raise NotImplementedError
