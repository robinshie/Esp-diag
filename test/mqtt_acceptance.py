#!/usr/bin/env python3
"""Actual MQTT broker restart + PTY USB + native firmware integration.

Optional: install requirements-integration.txt. No real server or ESP is needed.
"""
import argparse
import errno
import json
import os
import pty
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import SimpleNamespace
import paho.mqtt.client as mqtt
from h3c_server_sim import UsbMqttGateway
from native_support import NativeAgent
from reliable_gateway import fields

ROOT = Path(__file__).resolve().parent


def wait_for(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError("integration condition timed out")


class Bridge:
    def __init__(self):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.agent = NativeAgent("bridge-boot-1")
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.error = None
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        buffer = bytearray()
        try:
            while not self.stop.is_set():
                if not select.select([self.master], [], [], 0.05)[0]:
                    continue
                data = os.read(self.master, 4096)
                for byte in data:
                    if byte == 10:
                        line = buffer.decode().strip()
                        buffer.clear()
                        if not line:
                            continue
                        with self.lock:
                            for output in self.agent.send(line):
                                os.write(self.master, (output + "\n").encode())
                    elif byte != 13:
                        buffer.append(byte)
        except OSError as exc:
            if not self.stop.is_set() and exc.errno != errno.EIO:
                self.error = exc
        except Exception as exc:
            self.error = exc

    def reboot(self):
        with self.lock:
            self.agent.close()
            self.agent = NativeAgent("bridge-boot-2")
            os.write(self.master, b"[BDI][BOOT] boot=bridge-boot-2 protocol=2\n")

    def close(self):
        self.stop.set()
        self.thread.join(3)
        self.agent.close()
        os.close(self.master)
        os.close(self.slave)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mqtt-port", type=int, default=18884)
    args = p.parse_args()
    runtime = ROOT / ".runtime"
    runtime.mkdir(exist_ok=True)
    log = open(runtime / "integration-broker.log", "w")
    broker = None
    bridge = None
    gateway = None
    gateway_thread = None
    observer = None
    checks = []
    messages = []
    observed = threading.Event()
    def start_broker():
        nonlocal broker
        broker = subprocess.Popen([sys.executable, str(ROOT / "local_mqtt_broker.py"), "--port", str(args.mqtt_port)], stdout=log, stderr=log)
        def ready():
            if broker.poll() is not None:
                raise RuntimeError("test broker failed; inspect test/.runtime/integration-broker.log")
            try:
                with socket.create_connection(("127.0.0.1", args.mqtt_port), timeout=0.1):
                    return True
            except OSError:
                return False
        wait_for(ready)
    def stop_broker():
        nonlocal broker
        if broker:
            broker.terminate(); broker.wait(5); broker = None
    def start_gateway(state_db):
        nonlocal gateway, gateway_thread
        gateway = UsbMqttGateway(SimpleNamespace(
            state_db=state_db, port=bridge.path, baud=115200, user="", password="", gateway_id="cluster-acceptance",
            host="127.0.0.1", mqtt_port=args.mqtt_port, duration=0, seed=1, skip_smoke=True,
            min_interval=0.05, max_interval=0.05, devices=3, fault_rate=0, action_mode="success"))
        # PTYs have no modem-control hardware. Byte framing and pyserial I/O
        # remain real; bypass only DTR/RTS initialization.
        import serial
        def open_port():
            gateway.serial = serial.Serial(bridge.path, timeout=0.02, write_timeout=1)
            gateway.nonce = "integration-nonce"
            gateway.send_usb("H3C PING nonce=" + gateway.nonce)
        gateway._open_serial = open_port
        gateway.random_event = lambda: None
        def run():
            try:
                gateway.run()
            except Exception as exc:
                gateway.error = exc
        gateway_thread = threading.Thread(target=run)
        gateway_thread.start()
        wait_for(gateway.pong_seen.is_set)
        wait_for(gateway.mqtt_connected.is_set)
    def stop_gateway():
        nonlocal gateway_thread
        if gateway_thread:
            gateway.stop_event.set()
            gateway_thread.join(10)
            if gateway_thread.is_alive():
                raise AssertionError("gateway did not stop")
            gateway_thread = None
            if gateway.error:
                raise gateway.error
    def latest(device, state, count):
        return any(t == f"h3c/server/{device}/health" and fields(data).get("state") == state and
                   fields(data).get("active_faults") == str(count) for t, data in messages)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(dir=runtime) as temp:
        try:
            start_broker()
            observer = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="cluster-acceptance-observer")
            observer.reconnect_delay_set(1, 2)
            def connect(client, userdata, flags, reason, properties):
                client.subscribe("h3c/#", qos=1)
            observer.on_connect = connect
            observer.on_subscribe = lambda *args: observed.set()
            observer.on_message = lambda client, userdata, message: messages.append((message.topic, message.payload.decode()))
            observer.connect("127.0.0.1", args.mqtt_port)
            observer.loop_start()
            wait_for(observed.is_set)
            bridge = Bridge()
            start_gateway(str(Path(temp) / "gateway.db"))
            for device in ("a", "b", "c"):
                gateway.publish_log("INFO", "HEARTBEAT", "OK", device=device)
            wait_for(lambda: latest("c", "NORMAL", 0))
            gateway.publish_log("CRITICAL", "FAN_FAILURE", "ALARM", device="a", component="fan1")
            gateway.publish_log("CRITICAL", "THERMAL", "ALARM", device="a", component="cpu1")
            gateway.publish_log("WARN", "PSU_FAILURE", "ALARM", device="b", component="psu1")
            gateway.publish_log("INFO", "HEARTBEAT", "OK", device="a")
            wait_for(lambda: latest("a", "FAULT", 2) and latest("b", "DEGRADED", 1))
            checks.append("mqtt-device-isolation-and-persistent-faults")
            gateway.publish_log("INFO", "RECOVERY", "OK", device="a", component="cpu1", fault="THERMAL")
            # Native clock is explicit: exercise the same 2-second settle window.
            with bridge.lock:
                for line in bridge.agent.tick(2100):
                    os.write(bridge.master, (line + "\n").encode())
            # Wait for the recovery to reach the agent, then advance once more.
            wait_for(lambda: gateway.store.first_input() is None)
            with bridge.lock:
                for line in bridge.agent.tick(2100):
                    os.write(bridge.master, (line + "\n").encode())
            wait_for(lambda: latest("a", "FAULT", 1))
            checks.append("mqtt-partial-recovery")
            stop_broker()
            wait_for(lambda: not gateway.mqtt_connected.is_set())
            gateway.publish_log("CRITICAL", "PSU_FAILURE", "ALARM", device="b", component="psu2")
            wait_for(lambda: gateway.states.get("b", {}).get("active_faults") == "2")
            wait_for(lambda: gateway.store.first() is not None)
            start_broker()
            wait_for(lambda: latest("b", "FAULT", 2), 15)
            wait_for(lambda: gateway.store.first() is None, 15)
            checks.append("real-broker-restart-and-durable-replay")
            stop_gateway()
            start_gateway(str(Path(temp) / "gateway.db"))
            wait_for(lambda: gateway.states.get("a", {}).get("active_faults") == "1")
            checks.append("gateway-process-state-reopen")
            bridge.reboot()
            wait_for(lambda: gateway.boot == "bridge-boot-2")
            wait_for(lambda: len(bridge.agent.states) == 3 and bridge.agent.states["b"]["active_faults"] == "2")
            checks.append("esp-reboot-snapshot-reconciliation")
            gateway.publish_log("WARN", "AUTH_LOCKED", "LOCKED", device="c", component="admin")
            wait_for(lambda: gateway.unlock_acks == 1)
            wait_for(lambda: gateway.store.first_input() is None)
            with bridge.lock:
                for line in bridge.agent.tick(2100):
                    os.write(bridge.master, (line + "\n").encode())
            wait_for(lambda: gateway.states["c"].get("active_faults") == "0")
            checks.append("mqtt-action-and-observed-recovery")
            if bridge.error:
                raise bridge.error
            report = {"passed": checks, "duration_seconds": round(time.monotonic() - started, 2),
                      "mqtt_messages": len(messages), "real_mqtt": True, "physical_esp": False}
            (runtime / "mqtt-integration-report.json").write_text(json.dumps(report, indent=2))
            print(json.dumps(report, indent=2))
        finally:
            try:
                stop_gateway()
            finally:
                if bridge:
                    bridge.close()
                if observer:
                    observer.disconnect(); observer.loop_stop()
                stop_broker()
                log.close()


if __name__ == "__main__":
    main()
