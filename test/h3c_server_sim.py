#!/usr/bin/env python3
"""Continuous random H3C simulator and USB-to-MQTT gateway."""

from __future__ import annotations

import argparse
import random
import signal
import threading
import time

import paho.mqtt.client as mqtt
import serial

TOPIC_LOG = "h3c/server/sim-01/log"
TOPIC_STATUS = "h3c/esp32-s3-eth/status"
TOPIC_EVENT = "h3c/esp32-s3-eth/event"
TOPIC_ACTION = "h3c/esp32-s3-eth/action"
TOPIC_UNLOCK = "h3c/esp32-s3-eth/cmd/unlock"


class UsbMqttGateway:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.stop_event = threading.Event()
        self.mqtt_connected = threading.Event()
        self.pong_seen = threading.Event()
        self.sequence = 0
        self.logs_sent = 0
        self.mqtt_reports = 0
        self.unlock_acks = 0
        self.serial_lock = threading.Lock()

        self.serial = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
        self.serial.dtr = True
        self.serial.rts = False
        self.serial.port = args.port

        self.mqtt = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id="h3c-usb-mqtt-gateway",
        )
        self.mqtt.username_pw_set(args.user, args.password)
        self.mqtt.on_connect = self._on_mqtt_connect
        self.mqtt.on_disconnect = self._on_mqtt_disconnect
        self.mqtt.reconnect_delay_set(min_delay=1, max_delay=10)
        self.reader = threading.Thread(target=self._serial_reader, daemon=True)

    def _on_mqtt_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code == 0:
            print(f"MQTT connected: {self.args.host}:{self.args.mqtt_port}")
            self.mqtt_connected.set()
        else:
            print(f"MQTT connection rejected: {reason_code}")

    def _on_mqtt_disconnect(
        self, client, userdata, disconnect_flags, reason_code, properties
    ) -> None:
        self.mqtt_connected.clear()
        if not self.stop_event.is_set():
            print(f"MQTT disconnected: {reason_code}; reconnecting...")

    def start(self) -> bool:
        self.serial.open()
        print(f"USB connected: {self.args.port} @ {self.args.baud}")
        self.reader.start()
        self.mqtt.connect(self.args.host, self.args.mqtt_port, keepalive=30)
        self.mqtt.loop_start()
        return self.mqtt_connected.wait(8)

    def close(self) -> None:
        self.stop_event.set()
        self.reader.join(timeout=1)
        if self.serial.is_open:
            self.serial.close()
        self.mqtt.disconnect()
        self.mqtt.loop_stop()

    def send_usb(self, line: str) -> None:
        print(f"USB  -> ESP  {line}")
        with self.serial_lock:
            self.serial.write((line + "\n").encode())
            self.serial.flush()

    def publish_mqtt(self, topic: str, payload: str, retain: bool = False) -> None:
        if not self.mqtt_connected.is_set():
            print(f"MQTT DROP [{topic}] {payload}")
            return
        result = self.mqtt.publish(topic, payload, qos=1, retain=retain)
        result.wait_for_publish(timeout=3)
        self.mqtt_reports += 1
        print(f"MQTT -> [{topic}] {payload}")

    def _serial_reader(self) -> None:
        while not self.stop_event.is_set():
            raw = self.serial.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            print(f"ESP  -> USB  {line}")

            if line.startswith("H3C PONG"):
                self.pong_seen.set()
                topic = TOPIC_STATUS
            elif line.startswith("[BDI][EVENT]"):
                topic = TOPIC_EVENT
            elif line.startswith("[BDI][CMD] UNLOCK"):
                topic = TOPIC_UNLOCK
            elif line.startswith("[BDI][STATUS]"):
                topic = TOPIC_STATUS
            else:
                topic = TOPIC_ACTION

            self.publish_mqtt(topic, line, retain=topic == TOPIC_STATUS)

            if line.startswith("[BDI][CMD] UNLOCK"):
                request_id = self._field(line, "request_id")
                time.sleep(0.25)
                ack = (
                    "H3C ACK action=UNLOCK "
                    f"request_id={request_id} status=OK mode=SIMULATED_USB"
                )
                self.send_usb(ack)
                self.unlock_acks += 1

    @staticmethod
    def _field(payload: str, name: str) -> str:
        prefix = name + "="
        for item in payload.split():
            if item.startswith(prefix):
                return item[len(prefix) :]
        return "unknown"

    def publish_log(self, level: str, event: str, status: str, extra: str = "") -> None:
        self.sequence += 1
        payload = (
            f"H3C LOG seq={self.sequence} level={level} "
            f"event={event} status={status}"
        )
        if extra:
            payload += " " + extra
        self.publish_mqtt(TOPIC_LOG, payload)
        self.send_usb(payload)
        self.logs_sent += 1

    def smoke_cycle(self) -> bool:
        nonce = f"{random.getrandbits(32):08x}"
        self.send_usb(f"H3C PING nonce={nonce}")
        if not self.pong_seen.wait(3):
            print("FAIL: USB loopback timed out")
            return False

        print("Running deterministic startup smoke cycle...")
        events = [
            ("INFO", "HEARTBEAT", "OK", "cpu_pct=18"),
            ("CRITICAL", "THERMAL", "ALARM", "temp_c=92"),
            ("INFO", "RECOVERY", "OK", "temp_c=46"),
            ("WARN", "AUTH_LOCKED", "LOCKED", "user=admin"),
        ]
        for event in events:
            self.publish_log(*event)
            if self.stop_event.wait(1):
                return False
        return True

    def random_event(self) -> None:
        event = random.choices(
            ["heartbeat", "thermal", "fan", "power", "auth_locked"],
            weights=[45, 15, 15, 10, 15],
            k=1,
        )[0]
        if event == "heartbeat":
            self.publish_log(
                "INFO", "HEARTBEAT", "OK", f"cpu_pct={random.randint(8, 74)}"
            )
        elif event == "thermal":
            self.publish_log(
                "CRITICAL", "THERMAL", "ALARM", f"temp_c={random.randint(86, 103)}"
            )
            if not self.stop_event.wait(0.5):
                self.publish_log(
                    "INFO", "RECOVERY", "OK", f"temp_c={random.randint(41, 58)}"
                )
        elif event == "fan":
            self.publish_log(
                "CRITICAL", "FAN_FAILURE", "ALARM",
                f"fan={random.randint(1, 6)} rpm=0",
            )
        elif event == "power":
            self.publish_log(
                "CRITICAL", "PSU_FAILURE", "ALARM", f"psu={random.randint(1, 2)}"
            )
        else:
            self.publish_log(
                "WARN", "AUTH_LOCKED", "LOCKED",
                f"user=admin attempts={random.randint(3, 9)}",
            )

    def run(self) -> int:
        random.seed(self.args.seed)
        if not self.start():
            print("FAIL: MQTT broker connection timed out")
            self.close()
            return 1

        started = time.monotonic()
        try:
            if not self.args.skip_smoke and not self.smoke_cycle():
                return 1
            print("Continuous USB -> BDI -> USB -> MQTT test started; Ctrl+C to stop.")
            while not self.stop_event.is_set():
                if self.args.duration and time.monotonic() - started >= self.args.duration:
                    break
                self.random_event()
                delay = random.uniform(self.args.min_interval, self.args.max_interval)
                self.stop_event.wait(delay)
        finally:
            print(
                f"Summary: logs_sent={self.logs_sent} mqtt_reports={self.mqtt_reports} "
                f"unlock_acks={self.unlock_acks}"
            )
            self.close()
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Random H3C faults over USB with MQTT forwarding"
    )
    parser.add_argument("--port", default="/dev/cu.usbmodem101")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--user", default="esp32")
    parser.add_argument("--password", default="esp32-bdi-test")
    parser.add_argument("--min-interval", type=float, default=2.0)
    parser.add_argument("--max-interval", type=float, default=6.0)
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--skip-smoke", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    gateway = UsbMqttGateway(args)

    def stop_handler(signum, frame) -> None:
        gateway.stop_event.set()

    signal.signal(signal.SIGINT, stop_handler)
    signal.signal(signal.SIGTERM, stop_handler)
    return gateway.run()


if __name__ == "__main__":
    raise SystemExit(main())
