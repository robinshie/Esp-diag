#!/usr/bin/env python3
"""Continuous random H3C simulator and USB-to-MQTT gateway."""

from __future__ import annotations

import argparse
import random
import signal
import threading
import time
from datetime import datetime

import paho.mqtt.client as mqtt
import serial

TOPIC_LOG = "h3c/server/sim-01/log"
TOPIC_STATUS = "h3c/esp32-s3-eth/status"
TOPIC_AVAILABILITY = "h3c/esp32-s3-eth/availability"
TOPIC_HEALTH = "h3c/esp32-s3-eth/health"
TOPIC_EVENT = "h3c/esp32-s3-eth/event"
TOPIC_ACTION = "h3c/esp32-s3-eth/action"
TOPIC_UNLOCK = "h3c/esp32-s3-eth/cmd/unlock"
DEVICE_OFFLINE_TIMEOUT = 15.0

# H3C HDM event codes and message text are based on the official H3C server
# HDM alarm log reference. The trailing event/status fields are the local USB
# adapter fields consumed by the ESP32 BDI parser.
H3C_HDM_EVENTS = {
    "HEARTBEAT": {
        "code": "-",
        "type": "HEALTH",
        "sensor": "System_Health",
        "severity": "Info",
        "message": "System health is normal",
    },
    "THERMAL": {
        "code": "0x071000de",
        "sensor": "CPU1_Temp",
        "severity": "Critical",
        "message": "CPU Critical Temperature",
    },
    "FAN_FAILURE": {
        "code": "0x04600014",
        "sensor": "FAN",
        "severity": "Major",
        "message": "Transition to Degraded",
    },
    "PSU_FAILURE": {
        "code": "0x083000de",
        "sensor": "PSU",
        "severity": "Major",
        "message": "Power Supply input lost (AC/DC)",
    },
    "AUTH_LOCKED": {
        "code": "-",
        "type": "AUDIT",
        "sensor": "User",
        "severity": "Warning",
        "message": "User locked after five login failures",
    },
    "RECOVERY": {
        "code": "0x1800000e",
        "sensor": "System_Health",
        "severity": "Info",
        "message": "Transition to OK",
    },
}


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
        self.last_health_state: str | None = None
        self.last_health_payload: str | None = None
        self.last_device_seen: float | None = None
        self.device_online = False

        self.serial = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
        self.serial.dtr = True
        self.serial.rts = False
        self.serial.port = args.port

        self.mqtt = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id="h3c-usb-mqtt-gateway",
        )
        self.mqtt.username_pw_set(args.user, args.password)
        self.mqtt.will_set(
            TOPIC_AVAILABILITY,
            payload="state=OFFLINE reason=GATEWAY_DISCONNECTED",
            qos=1,
            retain=True,
        )
        self.mqtt.on_connect = self._on_mqtt_connect
        self.mqtt.on_disconnect = self._on_mqtt_disconnect
        self.mqtt.reconnect_delay_set(min_delay=1, max_delay=10)
        self.reader = threading.Thread(target=self._serial_reader, daemon=True)

    def _on_mqtt_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code == 0:
            print(f"MQTT connected: {self.args.host}:{self.args.mqtt_port}")
            self.mqtt_connected.set()
            recently_seen = (
                self.last_device_seen is not None
                and time.monotonic() - self.last_device_seen < DEVICE_OFFLINE_TIMEOUT
            )
            self._publish_availability(
                "ONLINE" if recently_seen else "OFFLINE",
                "USB_ACTIVE" if recently_seen else "WAITING_FOR_USB",
                force=True,
                wait=False,
            )
            self.mqtt.publish(
                TOPIC_STATUS,
                "gateway=ONLINE transport=USB mqtt_mode=STATE_AND_EXCEPTIONS",
                qos=1,
                retain=True,
            )
            if self.last_health_payload is not None:
                self.mqtt.publish(
                    TOPIC_HEALTH,
                    self.last_health_payload,
                    qos=1,
                    retain=True,
                )
        else:
            print(f"MQTT connection rejected: {reason_code}")

    def _on_mqtt_disconnect(
        self, client, userdata, disconnect_flags, reason_code, properties
    ) -> None:
        self.mqtt_connected.clear()
        self.device_online = False
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
        if self.mqtt_connected.is_set():
            self._publish_availability("OFFLINE", "GATEWAY_STOPPED", force=True)
        self.mqtt.disconnect()
        self.mqtt.loop_stop()

    def send_usb(self, line: str) -> None:
        print(f"USB  -> ESP  {line}")
        with self.serial_lock:
            self.serial.write((line + "\n").encode())
            self.serial.flush()

    def publish_mqtt(
        self, topic: str, payload: str, retain: bool = False, wait: bool = True
    ) -> None:
        if not self.mqtt_connected.is_set():
            print(f"MQTT DROP [{topic}] {payload}")
            return
        result = self.mqtt.publish(topic, payload, qos=1, retain=retain)
        if wait:
            result.wait_for_publish(timeout=3)
        self.mqtt_reports += 1
        print(f"MQTT -> [{topic}] {payload}")

    def _publish_availability(
        self, state: str, reason: str, force: bool = False, wait: bool = True
    ) -> None:
        online = state == "ONLINE"
        if not force and self.device_online == online:
            return
        self.device_online = online
        self.publish_mqtt(
            TOPIC_AVAILABILITY,
            f"state={state} reason={reason} transport=USB",
            retain=True,
            wait=wait,
        )

    def _mark_device_seen(self) -> None:
        self.last_device_seen = time.monotonic()
        if self.mqtt_connected.is_set() and not self.device_online:
            self._publish_availability("ONLINE", "USB_ACTIVE")

    def _check_device_timeout(self) -> None:
        if (
            self.device_online
            and self.last_device_seen is not None
            and time.monotonic() - self.last_device_seen >= DEVICE_OFFLINE_TIMEOUT
        ):
            self._publish_availability("OFFLINE", "USB_STATUS_TIMEOUT")

    def publish_health(self, state: str, event: str, status: str) -> None:
        if state == self.last_health_state:
            return
        previous = self.last_health_state or "UNKNOWN"
        self.last_health_state = state
        self.last_health_payload = (
            f"state={state} previous={previous} event={event} status={status}"
        )
        self.publish_mqtt(
            TOPIC_HEALTH,
            self.last_health_payload,
            retain=True,
        )

    def _serial_reader(self) -> None:
        while not self.stop_event.is_set():
            raw = self.serial.readline()
            if not raw:
                self._check_device_timeout()
                continue
            line = raw.decode(errors="replace").strip()
            if not line:
                continue
            self._mark_device_seen()
            print(f"ESP  -> USB  {line}")

            if line.startswith("H3C PONG"):
                self.pong_seen.set()
                topic = None
            elif line.startswith("[BDI][EVENT]"):
                topic = TOPIC_EVENT
            elif line.startswith("[BDI][CMD] UNLOCK"):
                topic = TOPIC_UNLOCK
            elif line.startswith("[BDI][STATUS]"):
                topic = None
            elif line.startswith("[BDI][RX][USB]"):
                topic = None
            elif line.startswith("[BDI][ACTION] server health accepted"):
                topic = None
            else:
                topic = TOPIC_ACTION

            if topic is not None:
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
        hdm_event = H3C_HDM_EVENTS[event]
        occurred_at = datetime.now().astimezone().isoformat(timespec="seconds")
        payload = (
            f"H3C LOG seq={self.sequence} time={occurred_at} "
            f"host=R4900-G5-01 src=HDM "
            f"type={hdm_event.get('type', 'EVENT')} "
            f"event_code={hdm_event['code']} sensor={hdm_event['sensor']} "
            f"severity={hdm_event['severity']} message=\"{hdm_event['message']}\" "
            f"level={level} event={event} status={status}"
        )
        if extra:
            payload += " " + extra
        is_fault = level in {"WARN", "CRITICAL"} or status in {"ALARM", "LOCKED"}
        if is_fault:
            self.publish_health("FAULT", event, status)
            self.publish_mqtt(TOPIC_LOG, payload)
        elif event in {"HEARTBEAT", "RECOVERY"} and status == "OK":
            self.publish_health("NORMAL", event, status)
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
        if random.random() >= self.args.fault_rate:
            self.publish_log(
                "INFO", "HEARTBEAT", "OK", f"cpu_pct={random.randint(8, 74)}"
            )
            return

        event = random.choices(
            ["thermal", "fan", "power", "auth_locked"],
            weights=[35, 30, 20, 15],
            k=1,
        )[0]
        if event == "thermal":
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
    parser.add_argument("--min-interval", type=float, default=20.0)
    parser.add_argument("--max-interval", type=float, default=60.0)
    parser.add_argument(
        "--fault-rate",
        type=float,
        default=0.05,
        help="fault probability per sample, from 0.0 to 1.0 (default: 0.05)",
    )
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--skip-smoke", action="store_true")
    args = parser.parse_args()
    if args.min_interval <= 0:
        parser.error("--min-interval must be greater than 0")
    if args.max_interval < args.min_interval:
        parser.error("--max-interval must be greater than or equal to --min-interval")
    if not 0.0 <= args.fault_rate <= 1.0:
        parser.error("--fault-rate must be between 0.0 and 1.0")
    return args


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
