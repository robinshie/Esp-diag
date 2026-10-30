#!/usr/bin/env python3
"""Unit tests for MQTT state-and-exception reporting."""

from __future__ import annotations

import unittest
from types import SimpleNamespace

from h3c_server_sim import TOPIC_HEALTH, TOPIC_LOG, UsbMqttGateway


class ReportingPolicyTest(unittest.TestCase):
    def setUp(self) -> None:
        self.gateway = UsbMqttGateway.__new__(UsbMqttGateway)
        self.gateway.sequence = 0
        self.gateway.logs_sent = 0
        self.gateway.last_health_state = None
        self.published: list[tuple[str, str, bool]] = []
        self.usb_lines: list[str] = []
        self.gateway.publish_mqtt = (
            lambda topic, payload, retain=False: self.published.append(
                (topic, payload, retain)
            )
        )
        self.gateway.send_usb = self.usb_lines.append

    def test_normal_heartbeat_is_not_published_repeatedly(self) -> None:
        self.gateway.publish_log("INFO", "HEARTBEAT", "OK", "cpu_pct=12")
        self.gateway.publish_log("INFO", "HEARTBEAT", "OK", "cpu_pct=18")

        self.assertEqual(len(self.usb_lines), 2)
        self.assertEqual(
            self.published,
            [(TOPIC_HEALTH, "state=NORMAL previous=UNKNOWN event=HEARTBEAT status=OK", True)],
        )

    def test_zero_fault_rate_generates_heartbeat(self) -> None:
        self.gateway.args = SimpleNamespace(fault_rate=0.0)

        self.gateway.random_event()

        self.assertEqual(len(self.usb_lines), 1)
        self.assertIn("host=R4900-G5-01", self.usb_lines[0])
        self.assertIn("type=HEALTH", self.usb_lines[0])
        self.assertIn("event_code=-", self.usb_lines[0])
        self.assertIn("event=HEARTBEAT status=OK", self.usb_lines[0])

    def test_faults_publish_and_recovery_updates_state_once(self) -> None:
        self.gateway.publish_log("INFO", "HEARTBEAT", "OK")
        self.published.clear()

        self.gateway.publish_log("CRITICAL", "FAN_FAILURE", "ALARM", "fan=2 rpm=0")
        self.gateway.publish_log("CRITICAL", "PSU_FAILURE", "ALARM", "psu=1")
        self.gateway.publish_log("INFO", "RECOVERY", "OK", "temp_c=45")

        self.assertIn("event_code=0x04600014", self.usb_lines[1])
        self.assertIn('message="Transition to Degraded"', self.usb_lines[1])
        self.assertIn("event_code=0x083000de", self.usb_lines[2])
        self.assertIn('message="Power Supply input lost (AC/DC)"', self.usb_lines[2])

        topics = [item[0] for item in self.published]
        self.assertEqual(topics.count(TOPIC_LOG), 2)
        self.assertEqual(topics.count(TOPIC_HEALTH), 2)
        self.assertIn("state=FAULT", self.published[0][1])
        self.assertIn("state=NORMAL", self.published[-1][1])

    def test_hdm_records_fit_usb_serial_frame(self) -> None:
        records = [
            ("INFO", "HEARTBEAT", "OK", "cpu_pct=18"),
            ("CRITICAL", "THERMAL", "ALARM", "temp_c=92 threshold_c=85"),
            ("CRITICAL", "FAN_FAILURE", "ALARM", "fan=6 rpm=0"),
            ("CRITICAL", "PSU_FAILURE", "ALARM", "psu=2 input=LOST"),
            ("WARN", "AUTH_LOCKED", "LOCKED", "user=admin attempts=5"),
            ("INFO", "RECOVERY", "OK", "temp_c=46"),
        ]

        for record in records:
            self.gateway.publish_log(*record)

        self.assertLessEqual(max(map(len, self.usb_lines)), 240)


if __name__ == "__main__":
    unittest.main()
