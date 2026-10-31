#!/usr/bin/env python3
"""Real SSH + configured gateway + PTY/native firmware + MQTT acceptance."""
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "productlines/h3c/switches/s5120v2-li/simulator"))

import paho.mqtt.client as mqtt
import paramiko
import serial
from fake_ssh_server import FakeSSHServer
from esp_gateway.config import load_config
from esp_gateway.runtime import SSHGateway
from esp_gateway.ssh import SSHSession, collect
from esp_gateway.store import fields
from mqtt_acceptance import Bridge, wait_for


def free_port():
    with socket.socket() as candidate:
        candidate.bind(("127.0.0.1", 0))
        return candidate.getsockname()[1]


class TimedBridge(Bridge):
    def __init__(self):
        super().__init__()
        self.timer = threading.Thread(target=self.clock)
        self.timer.start()

    def clock(self):
        previous = time.monotonic()
        while not self.stop.wait(0.05):
            now = time.monotonic()
            with self.lock:
                for output in self.agent.tick(max(1, int((now - previous) * 1000))):
                    os.write(self.master, (output + "\n").encode())
            previous = now

    def close(self):
        self.stop.set()
        self.timer.join(3)
        super().close()


def main():
    checks, messages = [], []
    runtime = ROOT / "test/.runtime"
    runtime.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=runtime) as temporary:
        folder = Path(temporary)
        mqtt_port, ssh_port = free_port(), free_port()
        server = FakeSSHServer(SimpleNamespace(host="127.0.0.1", port=ssh_port, user="admin", password="h3c-test",
                                               sysname="H3C", host_key=str(folder / "host-key")))
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        keys = paramiko.HostKeys()
        keys.add(f"[127.0.0.1]:{ssh_port}", server.host_key.get_name(), server.host_key)
        keys.save(str(folder / "known_hosts"))
        broker_log = (folder / "broker.log").open("w")
        broker = subprocess.Popen([sys.executable, str(ROOT / "test/local_mqtt_broker.py"), "--port", str(mqtt_port)], stdout=broker_log, stderr=broker_log)
        bridge = observer = gateway = gateway_thread = None
        error = []
        try:
            def ready():
                if broker.poll() is not None:
                    raise RuntimeError("broker exited")
                try:
                    with socket.create_connection(("127.0.0.1", mqtt_port), timeout=0.1):
                        return server.listener is not None
                except OSError:
                    return False
            wait_for(ready)
            bridge = TimedBridge()
            config = json.loads((ROOT / "config/ssh.example.json").read_text())
            config["agent"]["id"], config["agent"]["topic_root"] = "edge-config-test", "site-config-test"
            config["serial"]["port"] = bridge.path
            config["storage"]["state_db"] = str(folder / "state.sqlite3")
            config["mqtt"].update(host="127.0.0.1", port=mqtt_port, client_id="ssh-config-test", tls={"enabled": False})
            config["web"]["agent_ids"] = ["edge-config-test"]
            config["protocol"].update(stale_ms=5000, verify_ms=20000)
            device = config["devices"][0]
            device["poll_interval"] = 1
            device["ssh"].update(host="127.0.0.1", port=ssh_port, username="admin", known_hosts=str(folder / "known_hosts"), connect_timeout=0.5, command_timeout=1)
            device["actions"]["RECOVER_INTERFACE"]["enabled"] = True
            os.environ.update(MQTT_GATEWAY_PASSWORD="test", MQTT_WEB_PASSWORD="readonly-test", SSH_ACCESS01_PASSWORD="h3c-test", ESP_API_TOKEN="acceptance-api-" + "x" * 32)
            config_path = folder / "config.json"
            config_path.write_text(json.dumps(config))
            config = load_config(config_path)
            with SSHSession(config["devices"][0]["ssh"]) as session:
                assert collect(config["devices"][0], session)[0][1] is False
            checks.append("config-only SSH login, trusted host key, shell setup and healthy parsing")

            observer = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="config-observer")
            subscribed = threading.Event()
            observer.on_connect = lambda client, *_: client.subscribe("site-config-test/#", qos=1)
            observer.on_subscribe = lambda *_: subscribed.set()
            observer.on_message = lambda client, userdata, message: messages.append((message.topic, message.payload.decode()))
            observer.connect("127.0.0.1", mqtt_port)
            observer.loop_start()
            wait_for(subscribed.is_set)
            gateway = SSHGateway(config)
            def open_port():
                gateway.serial = serial.Serial(bridge.path, timeout=0.02, write_timeout=1)
                gateway.nonce = "configured-native-test"
                gateway.send_usb("H3C PING nonce=" + gateway.nonce)
            gateway._open_serial = open_port  # PTYs have no modem-control pins.
            def run():
                try:
                    gateway.run()
                except BaseException as exc:
                    error.append(repr(exc))
            gateway_thread = threading.Thread(target=run)
            gateway_thread.start()
            wait_for(gateway.pong_seen.is_set)
            wait_for(lambda: "access-01" in bridge.agent.states)
            wait_for(lambda: any("H3C CONFIGURED" in payload and "stale_ms=5000" in payload for _, payload in messages))
            checks.append("USB handshake applies per-environment firmware timers without rebuilding")

            server.switch.set_interface_state(False, "GigabitEthernet1/0/1", "admin")
            wait_for(lambda: any("[BDI][CMD] RECOVER_INTERFACE" in payload for _, payload in messages))
            wait_for(lambda: any("state=DONE result=SUCCESS" in payload for _, payload in messages), 15)
            audits = lambda: sum("Command is undo shutdown" in line for line in server.switch.logs)
            assert audits() == 1
            assert bridge.agent.states["access-01"]["state"] == "NORMAL"
            assert all(topic.startswith("site-config-test/edge-config-test/") for topic, _ in messages)
            assert all(topic.rsplit("/", 1)[-1] in {"report", "availability"} for topic, _ in messages)
            checks.append("SSH ADM observation -> ESP decision -> one authorized SSH mutation -> actual UP query -> settled recovery -> agent-only MQTT")

            server.switch.set_interface_state(False, "GigabitEthernet1/0/1", "cable")
            wait_for(lambda: bridge.agent.states["access-01"]["state"] == "FAULT")
            time.sleep(1.2)
            assert audits() == 1
            server.switch.set_interface_state(True, "GigabitEthernet1/0/1", "cable")
            wait_for(lambda: bridge.agent.states["access-01"]["state"] == "NORMAL", 10)
            checks.append("cable loss is observed without running an administrative repair")

            server.stop()
            server_thread.join(3)
            wait_for(lambda: bridge.agent.states["access-01"]["availability"] == "OFFLINE", 10)
            checks.append("SSH collection failure emits no false heartbeat; ESP source freshness expires")
            assert not error and bridge.error is None
        finally:
            if gateway:
                gateway.stop_event.set()
            if gateway_thread:
                gateway_thread.join(20)
                if gateway_thread.is_alive():
                    raise RuntimeError("gateway did not shut down")
            if bridge:
                bridge.close()
            if observer:
                observer.disconnect()
                observer.loop_stop()
            server.stop()
            server_thread.join(3)
            broker.terminate()
            broker.wait(10)
            broker_log.close()
    report = runtime / "ssh-config-report.json"
    report.write_text(json.dumps({"passed": True, "checks": checks}, indent=2) + "\n")
    for check in checks:
        print("PASS:", check)
    print("Report:", report)


if __name__ == "__main__":
    main()
