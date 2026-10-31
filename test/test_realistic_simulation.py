import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace

from h3c_server_sim import UsbMqttGateway
from realistic_simulation import RealisticSimulation
from reliable_gateway import Store


class RealisticSimulationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.gateway = g = UsbMqttGateway.__new__(UsbMqttGateway)
        g.store = Store(str(Path(self.temp.name) / "gateway.sqlite3"))
        self.addCleanup(g.store.close)
        g.args = SimpleNamespace(seed=42, devices=1)
        g.input_lock = threading.RLock()
        g.world, g.states, g.topology = {}, {}, None
        g.logs_sent = 0
        self.usb = []
        g.send_usb = self.usb.append
        g.queue_usb = self.usb.append
        g.publish_mqtt = lambda *args: None
        self.now = 1000.0
        self.sim = RealisticSimulation(g, clock=lambda: self.now)
        self.command = "[BDI][CMD] UNLOCK device=sim-01 component=admin fault=AUTH_LOCKED incident=incident-1 request_id=boot-1 attempt=1"

    def profile_after(self, previous):
        for index in range(previous):
            self.gateway.store.save_action(f"previous-{index}", {"profile": "delayed"})
        self.sim.submit(self.command)

    def advance(self, seconds):
        self.now += seconds
        self.sim.poll()

    def test_command_return_does_not_immediately_claim_recovery(self):
        self.sim.submit(self.command)
        self.assertEqual(self.usb, [])
        self.advance(1.3)
        self.assertEqual(len(self.usb), 1)
        self.assertIn("H3C ACK", self.usb[0])
        self.assertIn("status=OK", self.usb[0])
        self.advance(5)
        self.assertFalse(any("RECOVERY" in line for line in self.usb))
        self.advance(2)
        self.assertEqual(sum("RECOVERY" in line for line in self.usb), 1)
        self.assertIn("cause=POST_COMMAND_CHECK", self.usb[-1])

    def test_lost_first_reply_requires_a_retry(self):
        self.profile_after(2)
        self.advance(1.3)
        self.assertEqual(self.usb, [])
        self.advance(2)
        self.sim.submit(self.command.replace("attempt=1", "attempt=2"))
        self.advance(1.3)
        self.assertIn("status=OK", self.usb[0])
        self.assertFalse(any("RECOVERY" in line for line in self.usb))

    def test_cooldown_rejects_commands_and_keeps_lock_until_expiry(self):
        self.profile_after(3)
        self.advance(1.3)
        self.assertIn("status=FAILED", self.usb[0])
        self.advance(20)
        self.assertFalse(any("RECOVERY" in line for line in self.usb))
        self.advance(10)
        self.assertIn("cause=LOCKOUT_TIMER_EXPIRED", self.usb[-1])

    def test_duplicate_command_and_restart_do_not_repeat_recovery(self):
        self.sim.submit(self.command)
        self.sim.submit(self.command)
        self.advance(1.3)
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)
        self.sim.submit(self.command.replace("boot-1", "boot-2"))
        self.advance(1.3)
        self.advance(7)
        self.assertEqual(sum("RECOVERY" in line for line in self.usb), 1)
        self.sim.submit(self.command.replace("boot-1", "boot-3"))
        self.advance(2)
        self.assertEqual(sum("RECOVERY" in line for line in self.usb), 1)
        self.assertFalse(self.sim.pending)

    def test_sampling_never_randomly_clears_lock(self):
        self.gateway.world = {"sim-01": {"admin:AUTH_LOCKED": True}}
        for _ in range(20):
            self.now += 5
            self.sim.sample()
        self.assertFalse(any("RECOVERY" in line for line in self.usb))
        self.assertTrue(all("HEARTBEAT" in line for line in self.usb))

    def test_restart_after_acceptance_resumes_observation_without_another_command(self):
        self.sim.submit(self.command)
        self.advance(1.3)
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)
        self.advance(7)
        self.assertEqual(sum("H3C ACK" in line for line in self.usb), 1)
        self.assertEqual(sum("RECOVERY" in line for line in self.usb), 1)

    def test_restart_before_acceptance_preserves_natural_lockout_expiry(self):
        self.sim.submit(self.command)
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)
        self.advance(31)
        self.assertFalse(any("H3C ACK" in line for line in self.usb))
        self.assertIn("cause=LOCKOUT_TIMER_EXPIRED", self.usb[-1])


if __name__ == "__main__":
    unittest.main()
