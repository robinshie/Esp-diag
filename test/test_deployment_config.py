import copy
import json
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from esp_gateway.config import ConfigError, browser_config, load_config
from esp_gateway.runtime import SSHGateway
from esp_gateway.ssh import CheckError, SSHSession, evaluate

ROOT = Path(__file__).resolve().parents[1]


class DeploymentConfigTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "known_hosts").touch()
        self.path = self.root / "gateway.json"
        self.config = json.loads((ROOT / "config/ssh.example.json").read_text())
        self.config["storage"]["state_db"] = "state.sqlite3"
        self.environment = {"MQTT_GATEWAY_PASSWORD": "gateway-secret", "MQTT_WEB_PASSWORD": "browser-secret", "SSH_ACCESS01_PASSWORD": "ssh-secret", "ESP_API_TOKEN": "test-api-token-" + "x" * 32}
        self.env = patch.dict(os.environ, self.environment)
        self.env.start()
        self.addCleanup(self.env.stop)

    def load(self):
        self.path.write_text(json.dumps(self.config))
        return load_config(self.path)

    def gateway(self):
        gateway = SSHGateway(self.load())
        gateway.send_usb = lambda line: self.usb.append(line)
        self.usb = []
        self.addCleanup(gateway.instance_lock.close)
        self.addCleanup(gateway.store.close)
        return gateway

    def test_paths_and_browser_secrets_are_separated(self):
        cfg = self.load()
        self.assertEqual(cfg["storage"]["state_db"], str((self.root / "state.sqlite3").resolve()))
        public = json.dumps(browser_config(cfg))
        self.assertIn("browser-secret", public)
        self.assertNotIn("gateway-secret", public)
        self.assertNotIn("ssh-secret", public)

    def test_missing_credentials_and_unknown_keys_fail_before_network_access(self):
        self.config["mqtt"]["passwrod_env"] = "MISSPELLED"
        with self.assertRaises(ConfigError):
            self.load()
        del self.config["mqtt"]["passwrod_env"]
        with patch.dict(os.environ, {}, clear=True), self.assertRaises(ConfigError):
            self.load()

    def test_real_mode_cannot_enable_a_simulator(self):
        self.config["simulation"] = {"action_mode": "success"}
        with self.assertRaises(ConfigError):
            self.load()

    def test_deployed_gateway_accepts_usb_plugged_after_the_old_startup_deadline(self):
        gateway = self.gateway()
        gateway.mqtt = Mock()
        gateway.reports = None
        gateway.extra_workers = lambda: ()
        gateway._publisher = gateway._sender = lambda: gateway.stop_event.wait(3)
        waiting, plugged = threading.Event(), threading.Event()
        def reader():
            waiting.set()
            if plugged.wait(3):
                gateway.pong_seen.set()
        gateway._reader = reader
        gateway.initial_sample = Mock(side_effect=gateway.stop_event.set)
        errors = []
        def run():
            try:
                gateway.run()
            except BaseException as error:
                errors.append(error)
        # Advance only the gateway's clock beyond the old 30-second deadline;
        # threading timeouts still use their real clock.
        clock = Mock(wraps=time)
        clock.monotonic.side_effect = [0] + [31] * 100
        thread = threading.Thread(target=run)
        with patch("esp_gateway.transport.time", clock):
            try:
                thread.start()
                self.assertTrue(waiting.wait(1))
                time.sleep(0.3)
                self.assertTrue(thread.is_alive(), f"gateway exited before USB insertion: {errors}")
                plugged.set()
                thread.join(3)
                self.assertFalse(thread.is_alive())
                self.assertEqual(errors, [])
                gateway.initial_sample.assert_called_once()
            finally:
                gateway.stop_event.set()
                plugged.set()
                thread.join(3)

    def test_protocol_capacity_and_namespace_errors_fail_before_start(self):
        original = copy.deepcopy(self.config)
        for edit in (
            lambda: self.config["mqtt"].update(port=8883.5),
            lambda: self.config["agent"].update(id="server"),
            lambda: self.config["devices"][0]["checks"][0].update(component="port:1"),
            lambda: self.config["devices"][0].update(id="d" * 64, rack="r" * 64,
                checks=[dict(self.config["devices"][0]["checks"][0], component="c" * 64, event="e" * 64, cause="x" * 64)]),
        ):
            self.config = copy.deepcopy(original)
            edit()
            with self.assertRaises(ConfigError):
                self.load()

    def test_unanswered_ssh_exec_request_is_bounded(self):
        from unittest.mock import MagicMock
        closed = threading.Event()
        session = SSHSession({"command_timeout": 0.1})
        session.client = MagicMock()
        session.client.close.side_effect = closed.set
        channel = session.client.get_transport.return_value.open_session.return_value
        channel.exec_command.side_effect = lambda _: closed.wait(2)
        channel.recv_ready.return_value = False
        channel.recv_stderr_ready.return_value = False
        channel.exit_status_ready.return_value = True
        channel.recv_exit_status.return_value = 0
        started = time.monotonic()
        with self.assertRaises(TimeoutError):
            session.command("display interface brief")
        self.assertTrue(closed.is_set())
        self.assertLess(time.monotonic() - started, 1)

    def test_state_database_cannot_change_deployment_mode_or_agent(self):
        gateway = self.gateway()
        gateway.store.bind_deployment("ssh", gateway.agent_topic)
        with self.assertRaises(ValueError):
            gateway.store.bind_deployment("demo", gateway.agent_topic)
        with self.assertRaises(ValueError):
            gateway.store.bind_deployment("ssh", "h3c/another-agent")

    def test_invalid_regex_template_and_timeout_budgets_fail_early(self):
        original = copy.deepcopy(self.config)
        self.config["devices"][0]["checks"][0]["alarm_pattern"] = "["
        with self.assertRaises(ConfigError):
            self.load()
        self.config = copy.deepcopy(original)
        self.config["devices"][0]["actions"]["RECOVER_INTERFACE"]["commands"] = ["interface {component.__class__}"]
        with self.assertRaises(ConfigError):
            self.load()
        self.config = original
        self.config["devices"][0]["actions"]["RECOVER_INTERFACE"]["enabled"] = True
        self.config["protocol"]["verify_ms"] = 10000
        with self.assertRaises(ConfigError):
            self.load()

    def test_unknown_output_never_becomes_a_healthy_observation(self):
        check = self.load()["devices"][0]["checks"][0]
        with self.assertRaises(CheckError):
            evaluate(check, "permission denied")
        self.assertEqual(evaluate(check, "GE1/0/1              ADM 1G"), (True, "ADMIN_DOWN"))
        self.assertEqual(evaluate(check, "GE1/0/1              DOWN 1G"), (True, "PHYSICAL_DOWN"))
        self.assertEqual(evaluate(check, "GE1/0/1              UP 1G"), (False, "PHYSICAL_DOWN"))

    def alarm_command(self, gateway, cause="ADMIN_DOWN"):
        raw = gateway.publish_log("CRITICAL", "LINK_DOWN", "ALARM", device="access-01", kind="switch", rack="rack-a",
                                  component="GigabitEthernet1/0/1", cause=cause)
        from esp_gateway.store import fields
        incident = fields(raw)["incident"]
        return f"[BDI][CMD] RECOVER_INTERFACE request_id=boot-1 device=access-01 component=GigabitEthernet1/0/1 fault=LINK_DOWN incident={incident} attempt=1"

    def test_live_actions_are_disabled_by_default_and_cable_loss_is_not_repaired(self):
        gateway = self.gateway()
        command = self.alarm_command(gateway)
        gateway.execute_action(command)
        self.assertIn("status=FAILED", self.usb[-1])
        self.assertTrue(gateway.actions.empty())
        gateway.devices["access-01"]["actions"]["RECOVER_INTERFACE"]["enabled"] = True
        other = self.alarm_command(gateway, "PHYSICAL_DOWN")
        gateway.execute_action(other)
        self.assertIn("status=FAILED", self.usb[-1])
        self.assertTrue(gateway.actions.empty())

    def test_retry_and_restart_only_claim_a_mutation_once(self):
        self.config["devices"][0]["actions"]["RECOVER_INTERFACE"]["enabled"] = True
        gateway = self.gateway()
        command = self.alarm_command(gateway)
        gateway.execute_action(command)
        gateway.execute_action(command.replace("attempt=1", "attempt=2"))
        self.assertEqual(gateway.actions.qsize(), 1)
        key, f, action, execute = gateway.actions.get_nowait()
        self.assertTrue(execute)
        gateway.queued.clear()  # simulate in-memory queue loss at process restart
        gateway.execute_action(command.replace("boot-1", "boot-2"))
        self.assertFalse(gateway.actions.get_nowait()[-1])
        self.assertIn("status=OK", self.usb[-1])

    def test_production_topics_include_only_ESP32_and_are_configurable(self):
        gateway = self.gateway()
        gateway._handle_line("[BDI][STATE] device=access-01 kind=switch state=NORMAL availability=ONLINE active_faults=0")
        rows = gateway.store.db.execute("SELECT topic FROM outbox").fetchall()
        self.assertTrue(rows)
        self.assertTrue(all(topic.startswith("h3c/esp32-rack-a/") for (topic,) in rows))

    def test_queued_admin_repair_is_cancelled_if_live_state_changes_to_cable_loss(self):
        from unittest.mock import MagicMock, call
        self.config["devices"][0]["actions"]["RECOVER_INTERFACE"]["enabled"] = True
        gateway = self.gateway()
        gateway.execute_action(self.alarm_command(gateway))
        session = MagicMock()
        session.__enter__.return_value = session
        session.command.return_value = "GE1/0/1              DOWN 1G"
        gateway.session_factory = lambda _: session
        self.run_queued_action(gateway)
        self.assertEqual(session.command.call_args_list, [call("display interface brief")])
        self.assertNotIn("event=RECOVERY", "\n".join(row[0] for row in gateway.store.db.execute("SELECT payload FROM usb_outbox")))

    def run_queued_action(self, gateway):
        finished = threading.Event()
        save_action = gateway.store.save_action

        def save(key, record):
            save_action(key, record)
            finished.set()

        with patch.object(gateway.store, "save_action", side_effect=save):
            worker = threading.Thread(target=gateway.executor, daemon=True)
            worker.start()
            try:
                self.assertTrue(finished.wait(2))
            finally:
                gateway.stop_event.set()
                worker.join(2)

    def test_expired_queued_action_never_opens_ssh(self):
        from unittest.mock import MagicMock
        self.config["devices"][0]["actions"]["RECOVER_INTERFACE"]["enabled"] = True
        gateway = self.gateway()
        gateway.execute_action(self.alarm_command(gateway))
        gateway.actions.queue[0][1]["_deadline"] = time.monotonic() - 1
        gateway.session_factory = MagicMock()
        self.run_queued_action(gateway)
        gateway.session_factory.assert_not_called()


if __name__ == "__main__":
    unittest.main()
