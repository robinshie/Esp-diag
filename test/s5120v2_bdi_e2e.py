#!/usr/bin/env python3
"""End-to-end simulated S5120V2-LI fault monitoring and recovery test."""

from __future__ import annotations

import argparse
import re
import threading
import time
import uuid

import paho.mqtt.client as mqtt
import paramiko
import serial


TOPIC_BASE = "h3c/switch/s5120v2-li"
TOPIC_LOG = f"{TOPIC_BASE}/log"
TOPIC_HEALTH = f"{TOPIC_BASE}/health"
TOPIC_CONTROL = f"{TOPIC_BASE}/control"
TOPIC_AVAILABILITY = "h3c/esp32-s3-eth/availability"
TOPIC_EVENT = "h3c/esp32-s3-eth/event"
TOPIC_ACTION = "h3c/esp32-s3-eth/action"
TOPIC_COMMAND = "h3c/esp32-s3-eth/cmd/recover-interface"


def receive_until(channel, marker: str, timeout: float = 5.0) -> str:
    deadline = time.monotonic() + timeout
    received = ""
    while marker not in received and time.monotonic() < deadline:
        if channel.recv_ready():
            received += channel.recv(65535).decode("utf-8", errors="replace")
        else:
            time.sleep(0.05)
    if marker not in received:
        raise TimeoutError(f"SSH shell timed out waiting for {marker!r}: {received!r}")
    return received


def run_shell_recovery(client: paramiko.SSHClient) -> str:
    shell = client.invoke_shell(width=120, height=40)
    transcript = receive_until(shell, "<H3C>")
    commands = [
        ("system-view", "[H3C]"),
        ("interface GigabitEthernet1/0/1", "[H3C-GigabitEthernet1/0/1]"),
        ("undo shutdown", "[H3C-GigabitEthernet1/0/1]"),
        ("return", "<H3C>"),
    ]
    for command, marker in commands:
        print(f"CONTROL -> SSH  {command}")
        shell.send(command + "\n")
        response = receive_until(shell, marker)
        transcript += response
        print(f"SSH -> CONTROL  prompt={marker}")
    shell.send("quit\n")
    receive_until(shell, "Bye")
    shell.close()
    return transcript


class MqttObserver:
    def __init__(self, args: argparse.Namespace) -> None:
        self.connected = threading.Event()
        self.messages: list[tuple[str, str]] = []
        self.client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"s5120v2-e2e-{uuid.uuid4().hex[:8]}",
        )
        self.client.username_pw_set(args.mqtt_user, args.mqtt_password)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.connect(args.mqtt_host, args.mqtt_port, keepalive=30)
        self.client.loop_start()
        if not self.connected.wait(5):
            raise TimeoutError("MQTT connection timed out")

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code == 0:
            client.subscribe("h3c/#", qos=1)
            self.connected.set()

    def _on_message(self, client, userdata, message) -> None:
        self.messages.append(
            (message.topic, message.payload.decode("utf-8", errors="replace"))
        )

    def publish(self, topic: str, payload: str, retain: bool = False) -> None:
        result = self.client.publish(topic, payload, qos=1, retain=retain)
        result.wait_for_publish(timeout=3)
        print(f"MQTT -> [{topic}] {payload}")

    def close(self) -> None:
        self.client.disconnect()
        self.client.loop_stop()


def ssh_exec(client: paramiko.SSHClient, command: str) -> str:
    print(f"MONITOR -> SSH  {command}")
    _, stdout, stderr = client.exec_command(command, timeout=5)
    output = stdout.read().decode("utf-8", errors="replace")
    error = stderr.read().decode("utf-8", errors="replace")
    if error:
        raise RuntimeError(error)
    print(f"SSH -> MONITOR  {next((line for line in output.splitlines() if line), '')}")
    return output


def serial_read_until(
    port: serial.Serial,
    predicate,
    timeout: float,
    observer: MqttObserver,
) -> list[str]:
    deadline = time.monotonic() + timeout
    lines: list[str] = []
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        print(f"ESP -> USB  {line}")
        if line.startswith("[BDI][EVENT]"):
            observer.publish(TOPIC_EVENT, line)
        elif line.startswith("[BDI][CMD] RECOVER_INTERFACE"):
            observer.publish(TOPIC_COMMAND, line)
        elif line.startswith("[BDI][ACTION]") or line.startswith("[BDI][ACK]"):
            observer.publish(TOPIC_ACTION, line)
        if predicate(line, lines):
            return lines
    raise TimeoutError(f"USB response timed out; received={lines!r}")


def send_usb(port: serial.Serial, payload: str) -> None:
    print(f"USB -> ESP  {payload}")
    port.write((payload + "\n").encode())
    port.flush()


def run(args: argparse.Namespace) -> None:
    observer = MqttObserver(args)
    ssh_client = paramiko.SSHClient()
    ssh_client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh_client.connect(
        args.ssh_host,
        port=args.ssh_port,
        username=args.ssh_user,
        password=args.ssh_password,
        look_for_keys=False,
        allow_agent=False,
        timeout=5,
    )
    port = serial.Serial(args.serial_port, args.baud, timeout=0.2)
    port.dtr = True
    port.rts = False

    try:
        time.sleep(3.5)
        port.reset_input_buffer()
        nonce = uuid.uuid4().hex[:8]
        send_usb(port, f"H3C PING nonce={nonce}")
        serial_read_until(
            port,
            lambda line, lines: line.startswith("H3C PONG") and f"nonce={nonce}" in line,
            5,
            observer,
        )
        observer.publish(
            TOPIC_AVAILABILITY,
            "state=ONLINE reason=USB_ACTIVE transport=USB_SSH",
            retain=True,
        )

        injected = ssh_exec(
            ssh_client,
            "simulate interface-down GigabitEthernet1/0/1",
        )
        if "changed to down" not in injected:
            raise AssertionError(injected)
        raw_logs = ssh_exec(ssh_client, "display logbuffer reverse")
        fault_log = next(
            (
                line
                for line in raw_logs.splitlines()
                if "IFNET/3/PHY_UPDOWN" in line and "changed to down" in line
            ),
            "",
        )
        if not fault_log:
            raise AssertionError("PHY_UPDOWN fault was not present in logbuffer")
        observer.publish(TOPIC_LOG, fault_log)
        observer.publish(
            TOPIC_HEALTH,
            "state=FAULT event=LINK_DOWN interface=GigabitEthernet1/0/1",
            retain=True,
        )

        normalized_fault = (
            "H3C LOG source=S5120V2-LI transport=SSH module=IFNET severity=3 "
            "mnemonic=PHY_UPDOWN level=CRITICAL event=LINK_DOWN status=ALARM "
            "interface=GigabitEthernet1/0/1"
        )
        send_usb(port, normalized_fault)
        fault_lines = serial_read_until(
            port,
            lambda line, lines: line.startswith("[BDI][CMD] RECOVER_INTERFACE"),
            8,
            observer,
        )
        command_line = next(
            line for line in fault_lines if line.startswith("[BDI][CMD] RECOVER_INTERFACE")
        )
        request_match = re.search(r"request_id=(\d+)", command_line)
        if request_match is None:
            raise AssertionError(command_line)
        request_id = request_match.group(1)

        run_shell_recovery(ssh_client)
        status = ssh_exec(ssh_client, "display interface brief")
        if "GE1/0/1              UP" not in status:
            raise AssertionError(f"interface did not recover: {status}")
        observer.publish(
            TOPIC_CONTROL,
            f"request_id={request_id} action=RECOVER_INTERFACE result=SUCCESS "
            "interface=GigabitEthernet1/0/1 via=SSH",
        )

        send_usb(
            port,
            f"H3C ACK action=RECOVER_INTERFACE request_id={request_id} "
            "status=OK mode=SIMULATED_SSH",
        )
        normalized_recovery = (
            "H3C LOG source=S5120V2-LI transport=SSH module=IFNET severity=5 "
            "mnemonic=LINK_UPDOWN level=INFO event=LINK_UP status=OK "
            "interface=GigabitEthernet1/0/1"
        )
        send_usb(port, normalized_recovery)
        recovery_lines = serial_read_until(
            port,
            lambda line, lines: (
                any("recover_interface" in item and "result=SUCCESS" in item for item in lines)
                and any("belief=server_critical value=false" in item for item in lines)
                and any("belief=server_healthy value=true" in item for item in lines)
            ),
            8,
            observer,
        )
        observer.publish(TOPIC_LOG, normalized_recovery)
        observer.publish(
            TOPIC_HEALTH,
            "state=NORMAL event=LINK_UP interface=GigabitEthernet1/0/1",
            retain=True,
        )

        time.sleep(0.5)
        monitored_topics = {topic for topic, _ in observer.messages}
        required_topics = {
            TOPIC_LOG,
            TOPIC_HEALTH,
            TOPIC_EVENT,
            TOPIC_ACTION,
            TOPIC_COMMAND,
            TOPIC_CONTROL,
        }
        missing = required_topics - monitored_topics
        if missing:
            raise AssertionError(f"MQTT monitoring topics missing: {sorted(missing)}")
        if not any("belief=switch_link_fault value=true" in line for line in fault_lines):
            raise AssertionError("BDI did not assert switch_link_fault")
        if not any("belief=switch_link_fault value=false" in line for line in recovery_lines):
            raise AssertionError("BDI did not clear switch_link_fault")

        print(
            "PASS: SSH fault detected -> USB -> ESP32 BDI -> SSH recovery -> "
            "interface UP -> MQTT monitored"
        )
    finally:
        observer.publish(
            TOPIC_AVAILABILITY,
            "state=OFFLINE reason=E2E_TEST_FINISHED transport=USB_SSH",
            retain=True,
        )
        port.close()
        ssh_client.close()
        observer.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssh-host", default="127.0.0.1")
    parser.add_argument("--ssh-port", type=int, default=2222)
    parser.add_argument("--ssh-user", default="admin")
    parser.add_argument("--ssh-password", default="h3c-test")
    parser.add_argument("--serial-port", default="/dev/cu.usbmodem101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--mqtt-host", default="127.0.0.1")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--mqtt-user", default="esp32")
    parser.add_argument("--mqtt-password", default="esp32-bdi-test")
    return parser.parse_args()


if __name__ == "__main__":
    run(parse_args())
