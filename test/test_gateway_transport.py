"""Real pyserial PTY fragmentation/reconnect and MQTT outage/retry plumbing."""
import os
import pty
import random
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch
import serial
from h3c_server_sim import UsbMqttGateway
from reliable_gateway import Store


class GatewayTransportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.master, self.slave = pty.openpty()
        self.addCleanup(os.close, self.master)
        self.addCleanup(os.close, self.slave)
        self.gateway = UsbMqttGateway(SimpleNamespace(
            state_db=str(Path(self.temp.name) / "gateway.db"), port=os.ttyname(self.slave),
            baud=115200, user="", password="", gateway_id="test-transport"))
        self.g = self.gateway
        self.addCleanup(self.g.instance_lock.close)
        self.addCleanup(self.g.store.close)
        self.thread = None

    def wait_for(self, predicate):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.01)
        self.fail("transport condition timed out")

    def start_reader(self):
        # macOS PTYs do not implement modem-control ioctls. The byte transport
        # remains real pyserial; bypass only DTR/RTS manipulation.
        def open_port():
            self.g.serial = serial.Serial(os.ttyname(self.slave), timeout=0.02, write_timeout=0.2)
            self.g.nonce = "test-nonce"
        self.g._open_serial = open_port
        self.thread = threading.Thread(target=self.g._reader)
        self.thread.start()
        def cleanup():
            self.g.stop_event.set()
            self.thread.join(3)
            if self.g.serial:
                self.g.serial.close()
        self.addCleanup(cleanup)
        self.wait_for(lambda: self.g.serial is not None)

    def test_fragmentation_and_wrong_nonce_then_valid_handshake(self):
        self.start_reader()
        os.write(self.master, b"H3C PONG nonce=wrong boot=x\n")
        time.sleep(0.04)
        self.assertFalse(self.g.pong_seen.is_set())
        os.write(self.master, b"H3C PONG nonce=test-")
        time.sleep(0.04)
        self.assertFalse(self.g.pong_seen.is_set())
        os.write(self.master, b"nonce boot=x\n")
        self.wait_for(self.g.pong_seen.is_set)
        self.assertIsNone(self.g.error)

    def test_serial_exception_reopens_port(self):
        self.start_reader()
        old = self.g.serial
        with self.g.serial_lock:
            old.close()
        self.wait_for(lambda: self.g.serial is not None and self.g.serial is not old and self.g.serial.is_open)
        os.write(self.master, b"H3C PONG nonce=test-nonce boot=new\n")
        self.wait_for(self.g.pong_seen.is_set)

    def test_usb_status_identifies_the_current_serial_connection(self):
        self.g.nonce = "usb-one"
        self.g.publish_mqtt = Mock()
        self.g._handle_line("[BDI][STATUS] reasoning_cycle=ACTIVE transport=USB uptime_ms=1000")
        status = next(call.args[1] for call in self.g.publish_mqtt.call_args_list if call.args[0].endswith("/status"))
        self.assertIn("connection_id=usb-one", status)

    def test_reopened_usb_connection_is_unique_even_with_repeated_demo_seed(self):
        previous = random.getstate()
        try:
            with patch("esp_gateway.transport.serial.Serial"):
                self.g.send_usb = Mock()
                random.seed(42)
                self.g._open_serial()
                first = self.g.nonce
                random.seed(42)
                self.g._open_serial()
                self.assertNotEqual(self.g.nonce, first)
        finally:
            random.setstate(previous)

    def test_long_output_frame_is_discarded_until_newline(self):
        self.start_reader()
        os.write(self.master, b"x" * 9000 + b"\nH3C PONG nonce=test-nonce boot=x\n")
        self.wait_for(self.g.pong_seen.is_set)
        self.assertIsNone(self.g.error)
        rows = self.g.store.db.execute("SELECT payload FROM outbox").fetchall()
        self.assertTrue(any("USB_OUTPUT_TOO_LONG" in r[0] for r in rows))

    def test_mqtt_failed_puback_preserves_row_until_reconnect(self):
        self.g.store.enqueue("test", "first")
        self.g.store.enqueue("test", "second")
        delivered = []
        class Result:
            def wait_for_publish(self, timeout):
                pass
            def is_published(self):
                return True
        def publish(topic, payload, **kwargs):
            delivered.append(payload)
            if len(delivered) == 1:
                self.g.mqtt_connected.clear()
                raise RuntimeError("broker restarted before PUBACK")
            return Result()
        self.g.mqtt = Mock(publish=publish)
        thread = threading.Thread(target=self.g._publisher)
        thread.start()
        self.g.mqtt_connected.set()
        self.wait_for(lambda: len(delivered) == 1)
        self.assertEqual(self.g.store.first()[2], "first")
        self.g.mqtt_connected.set()
        self.wait_for(lambda: self.g.store.first() is None)
        self.g.stop_event.set()
        thread.join(3)
        self.assertEqual(delivered, ["first", "first", "second"])

    def test_usb_lost_receipt_retransmits_same_sequence(self):
        payload = "H3C LOG device=a kind=server epoch=1 seq=1 event=HEARTBEAT status=OK"
        self.g.store.queue_input(payload)
        sent = []
        def send(line):
            sent.append(line)
            if len(sent) == 2:
                self.g._handle_line("[BDI][RECEIPT] device=a epoch=1 seq=1 result=DUPLICATE")
            return True
        self.g.send_usb = send
        self.g.pong_seen.set()
        thread = threading.Thread(target=self.g._sender)
        thread.start()
        try:
            self.wait_for(lambda: self.g.store.first_input() is None)
            self.assertEqual(sent, [payload, payload])
            self.assertIsNone(self.g.error)
        finally:
            self.g.stop_event.set()
            thread.join(3)

    def test_usb_rejection_is_visible_and_retains_input(self):
        payload = "H3C LOG device=a kind=server epoch=1 seq=1 event=HEARTBEAT status=OK"
        self.g.store.queue_input(payload)
        def send(line):
            self.g._handle_line("[BDI][RECEIPT] device=a epoch=1 seq=1 result=REJECTED")
            return True
        self.g.send_usb = send
        self.g.pong_seen.set()
        thread = threading.Thread(target=self.g._sender)
        thread.start()
        self.wait_for(self.g.stop_event.is_set)
        thread.join(3)
        self.assertIsNotNone(self.g.store.first_input())
        self.assertIn("FIRMWARE_REJECTED_INPUT", str(self.g.error))
