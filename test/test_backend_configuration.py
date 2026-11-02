import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from esp_backend.settings import Settings


class NativeConfigurationTest(unittest.TestCase):
    def test_native_backend_imports_without_legacy_gateway(self):
        subprocess.run([str(Path('.venv/bin/python')), '-c',
            'import sys; import esp_backend.api; assert not any(name == "esp_gateway" or name.startswith("esp_gateway.") for name in sys.modules)'],
            check=True, capture_output=True)

    def test_native_settings_need_no_ssh_usb_or_simulation_configuration(self):
        raw = json.loads(Path('config/native.json').read_text())
        self.assertEqual(raw['mode'], 'native')
        self.assertEqual(raw['agent']['topic_root'], 'esp-switch')
        for key in ('serial', 'devices', 'simulation'):
            self.assertNotIn(key, raw)
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {
            'ESP_API_TOKEN': 'test-token-' * 4, 'ESP_ARCHIVE_MQTT_PASSWORD': 'test-password',
            'ESP_ARCHIVE_MQTT_HOST': '192.0.2.10', 'ESP_ARCHIVE_MQTT_PORT': '1884',
            'MONGODB_URI': 'mongodb://127.0.0.1/test'}, clear=True):
            env = Path(directory) / 'empty.env'
            env.write_text('')
            settings = Settings.load('config/native.json', env)
            self.assertEqual(settings.config['mqtt']['host'], '192.0.2.10')
            self.assertEqual(settings.config['mqtt']['port'], 1884)
            self.assertEqual(settings.config['mqtt']['username'], 'archive-readonly')
            self.assertEqual(settings.ai.provider, 'disabled')

    def test_legacy_imports_alias_one_shared_implementation(self):
        from esp_gateway.config import ConfigError, credential
        from esp_protocol.config import ConfigError as SharedError, credential as shared_credential
        from esp_gateway.reporting_config import ReportingConfig
        from esp_protocol.reporting_config import ReportingConfig as SharedReporting
        from esp_gateway.environment import load_environment
        from esp_protocol.environment import load_environment as shared_environment
        self.assertIs(ConfigError, SharedError)
        self.assertIs(credential, shared_credential)
        self.assertIs(ReportingConfig, SharedReporting)
        self.assertIs(load_environment, shared_environment)


if __name__ == '__main__':
    unittest.main()
