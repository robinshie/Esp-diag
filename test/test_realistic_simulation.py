import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace

from h3c_server_sim import UsbMqttGateway
from realistic_simulation import RealisticSimulation
from reliable_gateway import Store, fields


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

    def test_all_faults_rotates_every_scenario_and_keeps_physical_faults_out_of_actions(self):
        self.gateway.args.workload = "all_faults"
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)
        for _ in range(150):
            self.now += 5
            self.sim.sample()
        from reliable_gateway import fields
        alarms = [fields(line) for line in self.usb if fields(line).get("status") in {"ALARM", "LOCKED"}]
        expected = {(row[0], row[1]) for row in self.sim.SCENARIOS}
        self.assertEqual({(row['event'], row['component']) for row in alarms}, expected)
        self.assertEqual(next(row for row in alarms if row['component'] == 'GigabitEthernet1/0/8')['cause'], 'ADMIN_DOWN')
        self.assertEqual(next(row for row in alarms if row['component'] == 'GigabitEthernet1/0/9')['cause'], 'PHYSICAL_DOWN')
        recoveries = [fields(line) for line in self.usb if fields(line).get('event') == 'RECOVERY']
        self.assertFalse(any(row['fault'] == 'AUTH_LOCKED' or row['component'] == 'GigabitEthernet1/0/8' for row in recoveries))
        self.assertTrue(any(row['fault'] == 'LOOP_DETECTED' and row['cause'] == 'SIMULATED_MAINTENANCE' for row in recoveries))

    def test_all_faults_retains_heartbeats_and_staggers_faults_per_sample(self):
        self.gateway.args.workload = "all_faults"
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)
        for _ in range(60):
            self.usb.clear()
            self.now += 5
            self.sim.sample()
            self.assertIn('event=HEARTBEAT', self.usb[-1])
            self.assertLessEqual(len(self.usb), 2)

    def test_administrative_link_recovery_uses_delayed_command_observation(self):
        command = self.command.replace('UNLOCK','RECOVER_INTERFACE').replace('component=admin','component=GigabitEthernet1/0/8').replace('AUTH_LOCKED','LINK_DOWN')
        self.sim.submit(command)
        self.advance(1.3)
        self.assertIn('action=RECOVER_INTERFACE', self.usb[-1])
        self.assertFalse(any('RECOVERY' in line for line in self.usb))
        self.advance(7)
        self.assertIn('fault=LINK_DOWN', self.usb[-1])
        self.assertIn('cause=POST_COMMAND_CHECK', self.usb[-1])

    def overnight(self, minimum=300, maximum=900):
        self.gateway.args.workload = "overnight"
        self.gateway.args.normal_min = minimum
        self.gateway.args.normal_max = maximum
        self.sim = RealisticSimulation(self.gateway, clock=lambda: self.now)

    def test_overnight_starts_with_a_full_healthy_interval(self):
        self.overnight(300, 300)
        self.sim.sample()
        for _ in range(29):
            self.now += 10
            self.sim.sample()
        self.assertFalse(any(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb))
        self.now += 10
        self.sim.sample()
        self.assertEqual(sum(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb), 1)

    def test_overnight_waits_for_esp_confirmation_then_full_healthy_interval(self):
        self.overnight(300, 300)
        self.gateway.publish_log('WARN', 'MAC_MOVE', 'ALARM', component='port1')
        for _ in range(60):
            self.now += 10
            self.sim.sample()
        # Publishing a maintenance recovery alone does not clear the source.
        self.assertEqual(sum(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb), 1)
        self.gateway.world['sim-01'].clear()  # emulate settled [BDI][RECOVERED]
        self.sim.sample()
        self.now += 299
        self.sim.sample()
        self.assertEqual(sum(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb), 1)
        self.now += 1
        self.sim.sample()
        self.assertEqual(sum(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb), 2)

    def test_overnight_fast_recovery_still_preserves_healthy_interval(self):
        self.overnight(300, 300)
        self.sim.sample()
        self.now += 300
        self.sim.sample()
        self.gateway.world['sim-01'].clear()  # recovery before next sampling tick
        self.now += 10
        self.sim.sample()
        self.now += 299
        self.sim.sample()
        self.assertEqual(sum(fields(line).get('status') in {'ALARM', 'LOCKED'} for line in self.usb), 1)

    def test_failed_interface_action_does_not_claim_lockout_timer_repaired_port(self):
        self.profile_after(3)
        command = self.command.replace('UNLOCK', 'RECOVER_INTERFACE').replace('component=admin', 'component=port1').replace('AUTH_LOCKED', 'LINK_DOWN')
        self.sim.submit(command)
        # Explicitly exercise the existing cooldown branch for an interface.
        pending = list(self.sim.pending.values())[-1]
        pending['record'].update(profile='cooldown', accepted=False, recovery_due=self.now + 30)
        self.advance(31)
        self.assertIn('fault=LINK_DOWN', self.usb[-1])
        self.assertIn('cause=SIMULATED_MAINTENANCE', self.usb[-1])

    def test_eight_hour_virtual_clock_run_is_mostly_healthy_without_stacked_faults(self):
        self.overnight()
        healthy, alarms, cursor, last_recovered = 0, [], 0, None
        for _ in range(8 * 360):
            self.advance(10)
            self.sim.sample()
            batch, cursor = self.usb[cursor:], len(self.usb)
            for line in batch:
                f = fields(line)
                if f.get('status') in {'ALARM', 'LOCKED'}:
                    if last_recovered is not None:
                        self.assertGreaterEqual(self.now - last_recovered, 300)
                    alarms.append(f['event'])
                    if f['event'] == 'AUTH_LOCKED' or f.get('cause') == 'ADMIN_DOWN':
                        action = 'UNLOCK' if f['event'] == 'AUTH_LOCKED' else 'RECOVER_INTERFACE'
                        self.sim.submit(f"[BDI][CMD] {action} device=sim-01 component={f['component']} fault={f['event']} incident={f['incident']} request_id=test-{len(alarms)} attempt=1")
                if f.get('event') == 'RECOVERY':
                    self.gateway.world['sim-01'].pop(f['component'] + ':' + f['fault'], None)
                    incident = self.gateway.store.active_incident('sim-01', f['component'], f['fault'])
                    self.gateway.store.reconciled(f"[BDI][RECOVERED] device=sim-01 component={f['component']} fault={f['fault']} incident={incident}")
                    last_recovered = self.now
            self.assertLessEqual(len(self.gateway.world.get('sim-01', {})), 1)
            healthy += not bool(self.gateway.world.get('sim-01'))
        self.assertGreater(healthy / (8 * 360), 0.90)
        self.assertGreater(len(alarms), 20)
        self.assertLess(len(alarms), 80)
        self.assertGreater(len(set(alarms)), 5)
        self.overnight_stats = dict(simulated_hours=8, healthy_percent=round(100 * healthy / (8 * 360), 2), incidents=len(alarms), fault_types=sorted(set(alarms)), max_simultaneous_faults=1)


if __name__ == "__main__":
    unittest.main()
