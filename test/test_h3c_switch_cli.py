#!/usr/bin/env python3
"""Unit tests for the S5120V2-LI fake Comware command engine."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


SIMULATOR_DIR = (
    Path(__file__).resolve().parents[1]
    / "productlines"
    / "h3c"
    / "switches"
    / "s5120v2-li"
    / "simulator"
)
sys.path.insert(0, str(SIMULATOR_DIR))

from fake_ssh_server import FakeComwareSwitch  # noqa: E402


class FakeComwareSwitchTest(unittest.TestCase):
    def setUp(self) -> None:
        self.switch = FakeComwareSwitch()

    def test_inventory_command(self) -> None:
        output, view, close_session, exit_code = self.switch.execute("display version")
        self.assertIn("H3C Comware Software", output)
        self.assertIn("SIMULATION NOTICE", output)
        self.assertEqual(view, "user")
        self.assertFalse(close_session)
        self.assertEqual(exit_code, 0)

    def test_system_view_round_trip(self) -> None:
        output, view, _, _ = self.switch.execute("system-view")
        self.assertIn("System View", output)
        self.assertEqual(view, "system")
        _, view, close_session, _ = self.switch.execute("quit", view=view)
        self.assertEqual(view, "user")
        self.assertFalse(close_session)

    def test_unknown_command_fails(self) -> None:
        output, _, _, exit_code = self.switch.execute("delete startup.cfg")
        self.assertIn("Unrecognized command", output)
        self.assertEqual(exit_code, 1)

    def test_command_is_written_to_logbuffer(self) -> None:
        self.switch.execute("display environment")
        output, _, _, _ = self.switch.execute("display logbuffer reverse")
        self.assertIn("SHELL/6/SHELL_CMD", output)
        self.assertIn("Command is display environment", output)

    def test_interface_fault_and_recovery(self) -> None:
        output, view, _, _ = self.switch.execute(
            "simulate admin-down GigabitEthernet1/0/1"
        )
        self.assertIn("PHY_UPDOWN", output)
        self.assertIn("changed to down", output)
        status, _, _, _ = self.switch.execute("display interface brief")
        self.assertIn("GE1/0/1              ADM", status)

        _, view, _, _ = self.switch.execute("system-view", view=view)
        _, view, _, _ = self.switch.execute(
            "interface GigabitEthernet1/0/1", view=view
        )
        self.assertEqual(view, "interface:GigabitEthernet1/0/1")
        output, view, _, _ = self.switch.execute("undo shutdown", view=view)
        self.assertIn("changed to up", output)
        status, _, _, _ = self.switch.execute("display interface brief")
        self.assertIn("GE1/0/1              UP", status)

    def test_cable_fault_cannot_be_repaired_by_undo_shutdown(self):
        self.switch.execute("simulate interface-down GigabitEthernet1/0/2")
        _, view, _, _ = self.switch.execute("interface GigabitEthernet1/0/2", view="system")
        self.switch.execute("undo shutdown", view=view)
        output, _, _, _ = self.switch.execute("display interface brief")
        self.assertIn("GE1/0/2              DOWN", output)
        self.assertIn("GE1/0/1              UP", output)
        self.switch.execute("simulate cable-up GigabitEthernet1/0/2")
        output, _, _, _ = self.switch.execute("display interface brief")
        self.assertIn("GE1/0/2              UP", output)


if __name__ == "__main__":
    unittest.main()
