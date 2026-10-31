import unittest
from native_support import NativeAgent


class ProtocolConfigTest(unittest.TestCase):
    def setUp(self):
        self.agent = NativeAgent()
        self.addCleanup(self.agent.close)

    def test_timing_changes_are_applied_from_USB_without_a_rebuild(self):
        lines = self.agent.send("H3C CONFIG nonce=setup stale_ms=120000 settle_ms=3000 ack_ms=7000 verify_ms=30000")
        self.assertTrue(any(line.startswith("H3C CONFIGURED nonce=setup") for line in lines))
        self.agent.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        self.agent.tick(4000)
        self.assertEqual(len(self.agent.commands), 1)
        self.agent.ack()
        self.agent.tick(11000)
        self.assertFalse(any("result=FAILED" in line for line in self.agent.lines))
        self.agent.recover(fault="AUTH_LOCKED", component="admin")
        self.agent.tick(2100)
        self.assertEqual(self.agent.states["a"]["locked"], "true")
        self.agent.tick(1000)
        self.assertEqual(self.agent.states["a"]["locked"], "false")

    def test_bad_config_or_mid_action_change_is_rejected(self):
        self.assertTrue(any("INVALID_CONFIG" in line for line in self.agent.send("H3C CONFIG nonce=setup stale_ms=0 settle_ms=2000 ack_ms=3000 verify_ms=10000")))
        self.agent.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        self.assertTrue(any("CONFIG_DURING_ACTION" in line for line in self.agent.send("H3C CONFIG nonce=setup stale_ms=90000 settle_ms=2000 ack_ms=3000 verify_ms=30000")))
