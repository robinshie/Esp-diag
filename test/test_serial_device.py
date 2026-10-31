import unittest
from types import SimpleNamespace
from unittest.mock import patch

import serial
from esp_gateway.serial_device import resolve_port


class SerialDeviceTest(unittest.TestCase):
    @patch("esp_gateway.serial_device.list_ports.comports")
    def test_explicit_path_without_usb_identity_is_unchanged(self, ports):
        self.assertEqual(resolve_port("/dev/test-pty"), "/dev/test-pty")
        ports.assert_not_called()

    @patch("esp_gateway.serial_device.list_ports.comports")
    def test_reconnect_follows_same_board_after_port_renumbering(self, ports):
        other = SimpleNamespace(device="/dev/other", serial_number="other-board")
        ports.side_effect = [
            [other, SimpleNamespace(device="/dev/old", serial_number="our-board")],
            [other, SimpleNamespace(device="/dev/new", serial_number="our-board")],
        ]
        self.assertEqual(resolve_port("/dev/old", "our-board"), "/dev/old")
        self.assertEqual(resolve_port("/dev/old", "our-board"), "/dev/new")

    @patch("esp_gateway.serial_device.list_ports.comports")
    def test_absent_or_ambiguous_identity_never_falls_back_to_other_board(self, ports):
        for candidates in (
            [],
            [SimpleNamespace(device="/dev/old", serial_number="other-board")],
            [SimpleNamespace(device=p, serial_number="our-board") for p in ("/dev/a", "/dev/b")],
        ):
            with self.subTest(candidates=candidates):
                ports.return_value = candidates
                with self.assertRaises(serial.SerialException):
                    resolve_port("/dev/old", "our-board")
