#!/usr/bin/env python3
"""Physical ESP32 USB acceptance; uses synthetic nodes, never real servers.

Flash a clean firmware instance before this test. No MQTT broker is required.
"""
import argparse
import json
import time
import uuid
from pathlib import Path
import serial
from reliable_gateway import fields


class UsbAgent:
    def __init__(self, port):
        self.port = serial.Serial(port, 115200, timeout=0.05, write_timeout=2)
        self.epoch = time.time_ns() // 1000
        self.sequence = 0
        self.states = {}
        self.commands = []
        self.lines = []

    def pump(self, seconds=0.15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            raw = self.port.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            self.lines.append(line)
            f = fields(line)
            if line.startswith("[BDI][STATE]"):
                self.states[f["device"]] = f
            if line.startswith("[BDI][CMD]"):
                self.commands.append(f | {"action": line.split()[1]})

    def send(self, line):
        self.port.write((line + "\n").encode())
        self.port.flush()
        self.pump()

    def event(self, device="usb-a", event="HEARTBEAT", component="system", **extra):
        self.sequence += 1
        f = dict(device=device, kind="server", rack="test", epoch=self.epoch, seq=self.sequence,
                 event=event, component=component, level="INFO", status="OK")
        f.update(extra)
        line = "H3C LOG " + " ".join(f"{k}={v}" for k, v in f.items())
        self.send(line)
        return line

    def expect(self, device, state, faults):
        actual = self.states[device]
        if (actual["state"], int(actual["active_faults"])) != (state, faults):
            raise AssertionError(f"{device}: expected {state}/{faults}, got {actual}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="/dev/cu.usbmodem101")
    p.add_argument("--report", default=str(Path(__file__).parent / ".runtime/usb-cluster-report.json"))
    args = p.parse_args()
    a = UsbAgent(args.port)
    checks = []
    started = time.monotonic()
    try:
        a.pump(3.5)
        nonce = uuid.uuid4().hex
        a.send("H3C PING nonce=" + nonce)
        if not any("nonce=" + nonce in l and "H3C PONG" in l for l in a.lines):
            raise AssertionError("USB protocol-2 handshake failed")
        checks.append("usb-handshake")
        a.event(event="FAN_FAILURE", component="fan1", level="CRITICAL", status="ALARM")
        a.event(event="THERMAL", component="cpu1", level="CRITICAL", status="ALARM")
        a.event("usb-b")
        a.event()
        a.expect("usb-a", "FAULT", 2)
        a.expect("usb-b", "NORMAL", 0)
        checks.append("device-isolation-heartbeat-does-not-clear-faults")
        a.event(event="RECOVERY", component="cpu1", fault="THERMAL")
        a.pump(2.2)
        a.expect("usb-a", "FAULT", 1)
        checks.append("partial-recovery")
        duplicate = a.event("usb-b")
        a.send(duplicate)
        if not any("STALE_OR_DUPLICATE" in l for l in a.lines):
            raise AssertionError("duplicate event was accepted")
        checks.append("duplicate-rejection")
        a.event("usb-b", "AUTH_LOCKED", "admin", level="WARN", status="LOCKED")
        command = a.commands[-1]
        def ack(device):
            a.send(f"H3C ACK action=UNLOCK request_id={command['request_id']} device={device} component=admin status=OK")
        ack("usb-a")
        a.expect("usb-b", "DEGRADED", 1)
        ack("usb-b")
        a.expect("usb-b", "DEGRADED", 1)
        a.event("usb-b", "RECOVERY", "admin", fault="AUTH_LOCKED")
        a.pump(2.2)
        a.expect("usb-b", "NORMAL", 0)
        if not any("result=SUCCESS" in l for l in a.lines):
            raise AssertionError("verified operation was not completed")
        checks.append("ack-correlation-and-observed-recovery")
        # Seed 30 more contexts: firmware default is a bounded 32 devices.
        for i in range(30):
            a.event(f"usb-node-{i}")
        a.event("usb-overflow")
        if not any("DEVICE_CAPACITY" in l for l in a.lines):
            raise AssertionError("device limit was not enforced")
        checks.append("32-device-capacity")
        a.send("H3C LOG " + "x" * 600)
        nonce = uuid.uuid4().hex
        a.send("H3C PING nonce=" + nonce)
        if not any("nonce=" + nonce in l and "H3C PONG" in l for l in a.lines):
            raise AssertionError("oversize-frame recovery failed")
        checks.append("oversize-frame-resynchronization")
        a.pump(5.1)
        statuses = [fields(l) for l in a.lines if l.startswith("[BDI][STATUS]")]
        report = {"passed": checks, "duration_seconds": round(time.monotonic() - started, 2),
                  "devices": len(a.states), "last_status": statuses[-1] if statuses else {}, "hardware": True}
        destination = Path(args.report)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2))
        print(json.dumps(report, indent=2))
    finally:
        a.port.close()


if __name__ == "__main__":
    main()
