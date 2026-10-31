from pathlib import Path
import tempfile
import unittest

from deploy.init_mongodb_env import ensure_environment

ROOT = Path(__file__).resolve().parents[1]


class DatabaseEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        (self.directory / "mongodb.env.example").write_text((ROOT / "deploy/mongodb.env.example").read_text())

    def test_new_install_generates_private_credentials_and_login_instructions(self):
        ensure_environment(self.directory)
        env = self.directory / "mongodb.env"
        login = self.directory / "mongo-express-login.txt"
        self.assertNotIn("REPLACE_WITH_RANDOM", env.read_text())
        self.assertIn("用户名：admin", login.read_text())
        self.assertEqual(env.stat().st_mode & 0o777, 0o600)
        self.assertEqual(login.stat().st_mode & 0o777, 0o600)

    def test_upgrade_preserves_existing_database_api_and_manager_credentials(self):
        original = 'MONGO_PASSWORD=database-secret\nESP_API_TOKEN=unchanged-api\nMONGO_EXPRESS_PASSWORD="existing manager password"\n'
        env = self.directory / "mongodb.env"
        env.write_text(original)
        ensure_environment(self.directory)
        self.assertTrue(env.read_text().startswith(original))
        self.assertEqual(env.read_text().count("MONGO_EXPRESS_PASSWORD="), 1)
        self.assertIn("密码：existing manager password", (self.directory / "mongo-express-login.txt").read_text())
        first = env.read_text()
        ensure_environment(self.directory)
        self.assertEqual(first, env.read_text())

    def test_upgrade_adds_mqtt_defaults_without_changing_existing_broker_settings(self):
        original = 'MQTT_PORT=21883\nMQTT_USERNAME=custom-user\nESP_DEMO_MQTT_PASSWORD="existing MQTT password"\n'
        env = self.directory / "mongodb.env"
        env.write_text(original)
        ensure_environment(self.directory)
        self.assertTrue(env.read_text().startswith(original))
        self.assertEqual(env.read_text().count("MQTT_PORT="), 1)
        self.assertIn("MQTT_WS_PORT=9001", env.read_text())
        self.assertIn("MQTT_TOPIC_ROOT=h3c", env.read_text())
        self.assertEqual(env.stat().st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
