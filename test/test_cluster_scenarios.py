"""P0/P1/P2 acceptance scenarios against firmware C++ + real Embedded-BDI."""
import unittest
from pathlib import Path
from cluster_topology import Topology, ClusterWorld
from native_support import NativeAgent, build_native


class ClusterScenarios(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build_native()

    def setUp(self):
        self.agent = NativeAgent()
        self.addCleanup(self.agent.close)

    def state(self, device="a", state=None, faults=None, online=None):
        s = self.agent.states[device]
        if state is not None:
            self.assertEqual(s["state"], state)
        if faults is not None:
            self.assertEqual(int(s["active_faults"]), faults)
        if online is not None:
            self.assertEqual(s["availability"], "ONLINE" if online else "OFFLINE")
        return s

    def test_p0_device_and_component_isolation(self):
        a = self.agent
        a.alarm("a", "FAN_FAILURE", "fan1")
        a.alarm("a", "PSU_FAILURE", "psu1", level="WARN")
        a.alarm("a")
        a.event("b")
        a.recover("b")
        a.recover("a")
        self.state("a", "FAULT", 3)
        a.tick(2001)
        self.state("a", "FAULT", 2)
        self.state("b", "NORMAL", 0)
        a.event("a")
        self.state("a", "FAULT", 2)

    def test_p0_partial_redundancy_is_degraded(self):
        self.agent.alarm(event="PSU_FAILURE", component="psu1", level="WARN")
        self.state(state="DEGRADED", faults=1)
        self.agent.alarm(event="PSU_FAILURE", component="psu2")
        self.state(state="FAULT", faults=2)
        self.assertFalse(self.agent.commands)

    def test_p0_ack_must_match_device_component_boot_and_request(self):
        a = self.agent
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        self.assertEqual(len(a.commands), 1)
        for mismatch in ({"device": "b"}, {"component": "other"}, {"request_id": "old-boot-1"}, {"action": "RECOVER_INTERFACE"}):
            self.assertTrue(any("WARN" in line for line in a.ack(**mismatch)))
        self.state(state="DEGRADED", faults=1)
        a.ack()
        a.ack()  # duplicate acknowledgement cannot complete the operation
        self.state(state="DEGRADED", faults=1)
        a.recover(fault="AUTH_LOCKED", component="admin")
        a.tick(2001)
        self.state(state="NORMAL", faults=0)
        self.assertTrue(any("result=SUCCESS" in l for l in a.lines))

    def test_p0_two_accounts_have_independent_intentions(self):
        a = self.agent
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        a.alarm(event="AUTH_LOCKED", component="operator", level="WARN")
        self.assertEqual({c["component"] for c in a.commands}, {"admin", "operator"})
        self.assertEqual(len({c["request_id"] for c in a.commands}), 2)

    def test_p0_timeout_retries_are_bounded_and_idempotent(self):
        a = self.agent
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        a.tick(3001); a.tick(3001); a.tick(3001); a.tick(3001)
        self.assertEqual(len(a.commands), 3)
        self.assertEqual(len({c["request_id"] for c in a.commands}), 1)
        self.assertTrue(any("state=FAILED" in l for l in a.lines))
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        self.assertEqual(len(a.commands), 3)
        self.state(faults=1)

    def test_p0_command_success_without_effect_fails_verification(self):
        a = self.agent
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        a.ack(); a.tick(10001)
        self.state(state="DEGRADED", faults=1)
        self.assertEqual(len(a.commands), 1)
        self.assertTrue(any("state=FAILED" in l for l in a.lines))

    def test_p0_physical_fault_does_not_issue_administrative_repair(self):
        a = self.agent
        a.alarm("tor", "LINK_DOWN", "GE1/0/2", kind="switch", cause="CABLE_LOSS")
        self.assertFalse(a.commands)
        a.alarm("tor", "LINK_DOWN", "GE1/0/3", kind="switch", cause="ADMIN_DOWN")
        self.assertEqual(a.commands[-1]["target"], "GE1/0/3")

    def test_p1_duplicate_reordered_and_old_epoch(self):
        a = self.agent
        original = a.alarm()
        a.event()
        before = dict(a.states["a"])
        a.send(original)
        a.event(epoch=2)
        a.event(epoch=1)
        self.state(state="FAULT", faults=1)
        self.assertTrue(any("STALE_OR_DUPLICATE" in l for l in a.lines))
        self.assertEqual(before["faults"], a.states["a"]["faults"])

    def test_p1_recovery_flap_requires_stability(self):
        a = self.agent
        a.alarm(); a.recover(); a.tick(1000); a.alarm(); a.tick(2000)
        self.state(state="FAULT", faults=1)
        a.recover(); a.tick(1999)
        self.state(faults=1)
        a.tick(1)
        self.state(state="NORMAL", faults=0)

    def test_p1_unknown_recovery_cannot_clear_another_fault(self):
        a = self.agent
        a.alarm(); a.recover(component="cpu2"); a.tick(3000)
        self.state(state="FAULT", faults=1)

    def test_p1_freshness_independent_of_other_devices(self):
        a = self.agent
        a.event("a"); a.event("b"); a.tick(80000); a.event("b"); a.tick(10001)
        self.state("a", "UNKNOWN", 0, False)
        self.state("b", "NORMAL", 0, True)
        a.event("a")
        self.state("a", "NORMAL", 0, True)

    def test_p1_stale_replay_retains_fault_without_executing(self):
        self.agent.alarm(event="AUTH_LOCKED", component="admin", level="WARN", age_ms=100000)
        self.state(state="UNKNOWN", faults=1, online=False)
        self.assertFalse(self.agent.commands)

    def test_p1_stale_recovery_cannot_clear_fault_after_reconnection(self):
        a = self.agent
        a.alarm(age_ms=100000)
        a.recover(age_ms=100000)
        a.event(); a.tick(3000)
        self.state(state="FAULT", faults=1)

    def test_p0_explicit_action_failure_opens_circuit(self):
        a = self.agent
        a.alarm(event="AUTH_LOCKED", component="admin", level="WARN")
        for _ in range(3):
            a.ack(status="FAILED"); a.tick(1)
        self.assertEqual(len(a.commands), 3)
        self.assertTrue(any("state=FAILED" in line for line in a.lines))
        self.state(state="DEGRADED", faults=1)

    def test_p2_fault_capacity_marks_snapshot_incomplete(self):
        a = self.agent
        for i in range(33):
            a.alarm(event="FAN_FAILURE", component=f"fan{i}")
        self.state(state="UNKNOWN", faults=32)
        self.assertEqual(a.states["a"]["incomplete"], "true")
        self.assertTrue(any("FAULT_CAPACITY" in line for line in a.lines))

    def test_p1_strict_parser_rejects_malformed_frames(self):
        a = self.agent
        a.event()
        for line in ("H3C LOG " + "x" * 600,
                     "H3C LOG device=a kind=server epoch=1 seq=10 event=THERMAL component=cpu1 status=ALARMING level=CRITICAL",
                     "H3C LOG device=a device=b kind=server epoch=1 seq=10 event=HEARTBEAT status=OK",
                     "H3C LOG device=a kind=server epoch=-1 seq=10 event=HEARTBEAT status=OK"):
            self.assertTrue(any("WARN" in l for l in a.send(line)))
        self.state(state="NORMAL", faults=0)

    def topology(self):
        t = Topology.load(Path(__file__).parent / "scenarios/topology.json")
        w = ClusterWorld(self.agent, t)
        w.pulse()
        return t, w

    def test_p2_rack_power_loss_partial_cluster_survives(self):
        t, w = self.topology()
        w.rack_power_loss("rack-1")
        w.pulse()
        status = t.evaluate(self.agent.states)
        self.assertEqual(status["services"]["database"]["state"], "UNAVAILABLE")
        self.assertTrue(any(c["domain"] == "rack" for c in status["correlations"]))
        self.state("c", "NORMAL", 0, True)
        self.agent.tick(90001); w.pulse()
        self.state("a", "UNKNOWN", 2, False)
        self.assertFalse(self.agent.commands)

    def test_p2_uplink_partition_is_correlated_not_mass_repair(self):
        t, w = self.topology()
        w.uplink_loss("tor-1")
        status = t.evaluate(self.agent.states)
        self.assertEqual(status["services"]["database"]["healthy"], 1)
        self.assertTrue(any(c["domain"] == "uplink" for c in status["correlations"]))
        self.assertFalse(self.agent.commands)

    def test_p2_shared_storage_failure(self):
        t, w = self.topology()
        w.storage_loss("san-1")
        self.assertEqual(t.evaluate(self.agent.states)["services"]["database"]["healthy"], 0)
        self.state("a", "FAULT", 1)
        self.state("c", "FAULT", 1)

    def test_p2_node_exit_and_rolling_restart_preserve_quorum(self):
        t, w = self.topology()
        for node in ("a", "b", "c"):
            w.node_exit(node)
            self.agent.tick(90001); w.pulse()
            self.assertEqual(t.evaluate(self.agent.states)["services"]["database"]["state"], "AVAILABLE")
            w.stopped.remove(node); w.pulse()
            self.state(node, "NORMAL", 0, True)

    def test_p2_fan_to_thermal_cascade_partial_recovery(self):
        a = self.agent
        a.alarm(event="FAN_FAILURE", component="fan1")
        a.tick(5000); a.alarm()
        a.recover(); a.tick(2001)
        self.state(state="FAULT", faults=1)
        a.recover(fault="FAN_FAILURE", component="fan1"); a.tick(2001)
        self.state(state="NORMAL", faults=0)

    def test_p2_event_storm_and_explicit_capacity(self):
        a = self.agent
        for node in range(32):
            a.event(f"node-{node}")
        for i in range(10000):
            node = f"node-{i % 32}"
            if i % 2:
                a.alarm(node, "FAN_FAILURE", f"fan-{i % 8}")
            else:
                a.event(node)
        self.assertEqual(len(a.states), 32)
        self.assertFalse(any("BDI_EVENT_CAPACITY" in l for l in a.lines))
        a.event("overflow")
        self.assertTrue(any("DEVICE_CAPACITY" in l for l in a.lines))
        self.assertNotIn("overflow", a.states)
        for node in range(1, 32, 2):
            self.state(f"node-{node}", "FAULT")

    def test_p2_128_device_host_scale(self):
        other = NativeAgent(capacity=128)
        self.addCleanup(other.close)
        for i in range(128):
            other.alarm(f"server-{i}")
        self.assertEqual(len(other.states), 128)
        self.assertTrue(all(s["state"] == "FAULT" for s in other.states.values()))
