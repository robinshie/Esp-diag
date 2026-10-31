from dataclasses import dataclass
import os
import json
import re
from pathlib import Path

from esp_gateway.config import credential
from esp_gateway.environment import load_environment


@dataclass
class Settings:
    config: dict
    token: str
    mongo_host: str
    mongo_port: int
    database: str
    username: str
    password: str
    mongo_uri: str | None
    history_days: int
    state_path: str
    host: str
    port: int

    @classmethod
    def load(cls, config_path, env_file):
        load_environment(env_file)
        # The archive reads only MQTT/namespace settings. It never needs SSH
        # credentials, known_hosts, serial access or recovery permissions.
        raw = json.loads(Path(config_path).read_text())
        cfg = {"agent": raw["agent"], "mqtt": dict(raw["mqtt"])}
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", cfg["agent"]["topic_root"]):
            raise ValueError("invalid archive topic root")
        if os.environ.get("ESP_ARCHIVE_MQTT_PASSWORD"):
            cfg["mqtt"]["password_env"] = "ESP_ARCHIVE_MQTT_PASSWORD"
            cfg["mqtt"]["username"] = os.environ["ESP_ARCHIVE_MQTT_USERNAME"]
        credential(cfg["mqtt"])
        if not isinstance(cfg["mqtt"]["port"], int) or not 1 <= cfg["mqtt"]["port"] <= 65535:
            raise ValueError("invalid MQTT port")
        tls = cfg["mqtt"].get("tls", {})
        if bool(tls.get("cert_file")) != bool(tls.get("key_file")):
            raise ValueError("MQTT certificate and key must be configured together")
        for key in ("ca_file", "cert_file", "key_file"):
            if tls.get(key):
                tls[key] = str((Path(config_path).resolve().parent / Path(tls[key]).expanduser()).resolve())
                if not Path(tls[key]).is_file():
                    raise ValueError(f"MQTT {key} does not exist")
        token = os.environ.get("ESP_API_TOKEN", "")
        if len(token) < 32:
            raise ValueError("ESP_API_TOKEN must contain at least 32 characters")
        uri = os.environ.get("MONGODB_URI")
        username, password = os.environ.get("MONGO_USERNAME", ""), os.environ.get("MONGO_PASSWORD", "")
        if not uri and (not username or not password):
            raise ValueError("MongoDB application credentials are required")
        history = int(os.environ.get("ESP_HISTORY_DAYS", "0"))
        if not 0 <= history <= 3650:
            raise ValueError("ESP_HISTORY_DAYS must be 0..3650 (0 keeps history indefinitely)")
        return cls(cfg, token, os.environ.get("MONGO_HOST", "127.0.0.1"), int(os.environ.get("MONGO_PORT", "27017")),
                   os.environ.get("MONGO_DATABASE", "esp_diag"), username, password, uri, history,
                   str(Path(os.environ.get("ESP_BACKEND_STATE", "var/backend/inbox.sqlite3")).resolve()),
                   os.environ.get("ESP_API_HOST", "127.0.0.1"), int(os.environ.get("ESP_API_PORT", "8080")))
