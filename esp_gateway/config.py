"""Validated runtime configuration. Credentials stay in environment variables."""
from __future__ import annotations

import json
import os
import re
import string
from pathlib import Path
from urllib.parse import urlparse


class ConfigError(ValueError):
    pass


def _object(value, allowed, required=(), label="config"):
    if not isinstance(value, dict):
        raise ConfigError(f"{label}: expected an object")
    unknown = set(value) - set(allowed)
    missing = set(required) - set(value)
    if unknown or missing:
        raise ConfigError(f"{label}: unknown={sorted(unknown)}, missing={sorted(missing)}")


def _number(value, low, high, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
        raise ConfigError(f"{label}: expected {low}..{high}")


def _integer(value, low, high, label):
    _number(value, low, high, label)
    if not isinstance(value, int):
        raise ConfigError(f"{label}: expected an integer")


def _identifier(value, label, component=False):
    pattern = r"[A-Za-z0-9_./-]{1,64}" if component else r"[A-Za-z0-9_.-]{1,64}"
    if not isinstance(value, str) or not re.fullmatch(pattern, value):
        raise ConfigError(f"{label}: invalid identifier")


def _command(value):
    if not isinstance(value, str) or not value.strip() or any(char in value for char in "\r\n\x00"):
        raise ConfigError("commands must be nonempty single-line strings")


def credential(section, key="password_env"):
    name = section.get(key)
    if not name:
        return None
    if not isinstance(name, str) or not os.environ.get(name):
        raise ConfigError(f"missing environment variable: {name}")
    return os.environ[name]


def load_config(path, *, require_credentials=True):
    path = Path(path).resolve()
    try:
        config = json.loads(path.read_text())
    except (OSError, ValueError) as exc:
        raise ConfigError(f"cannot read JSON config: {path}") from exc
    _object(config, {"schema_version", "mode", "agent", "serial", "storage", "mqtt", "web", "devices", "simulation", "protocol", "reporting"},
            {"schema_version", "mode", "agent", "serial", "storage", "mqtt", "web"})
    if type(config["schema_version"]) is not int or config["schema_version"] != 1 or config["mode"] not in {"demo", "ssh"}:
        raise ConfigError("expected schema_version=1 and mode=demo|ssh")
    protocol = config.setdefault("protocol", {})
    _object(protocol, {"stale_ms", "settle_ms", "ack_ms", "verify_ms"}, label="protocol")
    defaults = {"stale_ms": 90000, "settle_ms": 2000, "ack_ms": 3000, "verify_ms": 30000 if config["mode"] == "ssh" else 10000}
    for key, low, high in (("stale_ms", 1000, 3600000), ("settle_ms", 100, 60000), ("ack_ms", 100, 60000), ("verify_ms", 1000, 600000)):
        protocol.setdefault(key, defaults[key])
        _number(protocol[key], low, high, f"protocol.{key}")
        if not isinstance(protocol[key], int):
            raise ConfigError(f"protocol.{key} must be integer")
    if protocol["verify_ms"] <= protocol["settle_ms"]:
        raise ConfigError("verification must exceed the stabilization window")
    reporting = config.setdefault("reporting", {})
    _object(reporting, {"batch_ms", "interval_ms"}, label="reporting")
    reporting.setdefault("batch_ms", 250)
    reporting.setdefault("interval_ms", 5000)
    _integer(reporting["batch_ms"], 50, 1000, "reporting.batch_ms")
    _integer(reporting["interval_ms"], 1000, 5000, "reporting.interval_ms")

    def local_file(section, key, *, must_exist=False):
        if section.get(key):
            value = Path(os.path.expanduser(section[key]))
            section[key] = str((path.parent / value).resolve())
            if must_exist and not Path(section[key]).is_file():
                raise ConfigError(f"{key}: file does not exist: {section[key]}")

    _object(config["agent"], {"id", "topic_root", "name"}, {"id", "topic_root"}, "agent")
    _identifier(config["agent"]["id"], "agent.id")
    if config["agent"]["id"] in {"server", "switch", "service"}:
        raise ConfigError("agent.id conflicts with a reserved source namespace")
    _identifier(config["agent"]["topic_root"], "agent.topic_root")
    _object(config["serial"], {"port", "baud"}, {"port", "baud"}, "serial")
    if not isinstance(config["serial"]["port"], str) or not config["serial"]["port"]:
        raise ConfigError("serial.port is required")
    _integer(config["serial"]["baud"], 9600, 2000000, "serial.baud")
    _object(config["storage"], {"state_db"}, {"state_db"}, "storage")
    if not isinstance(config["storage"]["state_db"], str) or not config["storage"]["state_db"]:
        raise ConfigError("storage.state_db is required")
    local_file(config["storage"], "state_db")
    mqtt = config["mqtt"]
    _object(mqtt, {"host", "port", "username", "password_env", "client_id", "tls"},
            {"host", "port", "username", "password_env", "client_id"}, "mqtt")
    _integer(mqtt["port"], 1, 65535, "mqtt.port")
    if not isinstance(mqtt["host"], str) or not mqtt["host"].strip():
        raise ConfigError("mqtt.host is required")
    _identifier(mqtt["client_id"], "mqtt.client_id")
    for section in (mqtt, config["web"]):
        for key in ("username", "password_env"):
            if not isinstance(section.get(key), str) or not section[key]:
                raise ConfigError(f"{key} must be a nonempty string")
    _object(mqtt.get("tls", {}), {"enabled", "ca_file", "cert_file", "key_file"}, label="mqtt.tls")
    tls = mqtt.setdefault("tls", {})
    if not isinstance(tls.get("enabled", False), bool):
        raise ConfigError("mqtt.tls.enabled must be boolean")
    if bool(tls.get("cert_file")) != bool(tls.get("key_file")):
        raise ConfigError("MQTT client certificate and key must be configured together")
    for key in ("ca_file", "cert_file", "key_file"):
        local_file(tls, key, must_exist=True)
    web = config["web"]
    _object(web, {"mqtt_url", "username", "password_env", "agent_ids", "api_url", "api_token_env"},
            {"mqtt_url", "username", "password_env", "agent_ids"}, "web")
    if bool(web.get("api_url")) != bool(web.get("api_token_env")):
        raise ConfigError("web.api_url and api_token_env must be configured together")
    if web.get("api_url") and (not isinstance(web["api_url"], str) or not re.fullmatch(r"/[A-Za-z0-9/_-]+", web["api_url"]) or web["api_url"].startswith("//")):
        raise ConfigError("web.api_url must be a same-origin path such as /api")
    parsed = urlparse(web["mqtt_url"])
    if parsed.scheme not in {"ws", "wss"} or not parsed.hostname or parsed.username or parsed.password:
        raise ConfigError("web.mqtt_url must be a ws/wss URL without embedded credentials")
    if not isinstance(web["agent_ids"], list) or config["agent"]["id"] not in web["agent_ids"]:
        raise ConfigError("web.agent_ids must include this gateway's agent.id")
    for agent_id in web["agent_ids"]:
        _identifier(agent_id, "web.agent_ids")
        if agent_id in {"server", "switch", "service"}:
            raise ConfigError("web.agent_ids conflicts with a reserved source namespace")
    if require_credentials:
        credential(mqtt)
        credential(web)
        if web.get("api_url"):
            if len(credential(web, "api_token_env")) < 32:
                raise ConfigError("web API token must contain at least 32 characters")
    devices = config.setdefault("devices", [])
    if not isinstance(devices, list) or len(devices) > 32:
        raise ConfigError("devices must be a list of at most 32 entries")
    if config["mode"] == "ssh" and (not devices or config.get("simulation")):
        raise ConfigError("ssh mode requires devices and cannot enable simulation")
    if config["mode"] == "demo" and devices:
        raise ConfigError("demo mode cannot contain real SSH devices")
    ids = set()
    for device in devices:
        _object(device, {"id", "kind", "rack", "poll_interval", "ssh", "checks", "actions"},
                {"id", "kind", "rack", "poll_interval", "ssh", "checks"}, "device")
        _identifier(device["id"], "device.id")
        _identifier(device["rack"], "device.rack")
        if device["id"] in ids or device["id"] == config["agent"]["id"]:
            raise ConfigError("device IDs must be unique and differ from agent.id")
        ids.add(device["id"])
        if device["kind"] not in {"server", "switch"}:
            raise ConfigError("device.kind must be server|switch")
        _number(device["poll_interval"], 1, 60, "device.poll_interval")
        if device["poll_interval"] * 2000 >= protocol["stale_ms"]:
            raise ConfigError("source freshness must allow at least two poll intervals")
        ssh = device["ssh"]
        _object(ssh, {"host", "port", "username", "password_env", "private_key_file", "known_hosts", "transport",
                      "prompt_pattern", "setup_commands", "connect_timeout", "command_timeout", "error_pattern"},
                {"host", "port", "username", "known_hosts", "transport"}, "ssh")
        if ssh["transport"] not in {"shell", "exec"}:
            raise ConfigError("ssh.transport must be shell|exec")
        _integer(ssh["port"], 1, 65535, "ssh.port")
        for key in ("connect_timeout", "command_timeout"):
            _number(ssh.get(key, 5), 0.1, 30, f"ssh.{key}")
        for key in ("host", "username"):
            if not isinstance(ssh[key], str) or not ssh[key]:
                raise ConfigError(f"ssh.{key} is required")
        if not isinstance(ssh.get("setup_commands", []), list):
            raise ConfigError("ssh.setup_commands must be a list")
        for command in ssh.get("setup_commands", []):
            _command(command)
        local_file(ssh, "known_hosts", must_exist=True)
        local_file(ssh, "private_key_file", must_exist=True)
        if not ssh.get("private_key_file") and not ssh.get("password_env"):
            raise ConfigError("ssh requires a private key or password_env")
        if require_credentials:
            credential(ssh)
        for pattern in (ssh.get("prompt_pattern", r"(?m)^[<\[][^\r\n]+[>\]]\s*$"), ssh.get("error_pattern", r"(?i)%\s*(Unrecognized|Incomplete|Ambiguous)|Error:|Permission denied")):
            try:
                re.compile(pattern)
            except re.error as exc:
                raise ConfigError("invalid SSH prompt/error pattern") from exc
        checks = device["checks"]
        if not isinstance(checks, list) or not checks or len(checks) > 32:
            raise ConfigError("each device requires 1..32 checks")
        keys = set()
        for check in checks:
            _object(check, {"command", "component", "event", "severity", "alarm_pattern", "healthy_pattern", "cause", "action_when_pattern", "action_cause"},
                    {"command", "component", "event", "severity", "alarm_pattern", "healthy_pattern"}, "check")
            _identifier(check["component"], "check.component", component=True)
            _identifier(check["event"], "check.event")
            _command(check["command"])
            if check["severity"] not in {"WARN", "CRITICAL"}:
                raise ConfigError("check.severity must be WARN|CRITICAL")
            pair = (check["component"], check["event"])
            if pair in keys:
                raise ConfigError("duplicate component/event check")
            keys.add(pair)
            for key in ("alarm_pattern", "healthy_pattern", "action_when_pattern"):
                try:
                    if key in check:
                        re.compile(check[key])
                except (re.error, TypeError) as exc:
                    raise ConfigError(f"invalid check.{key}") from exc
            for key in ("cause", "action_cause"):
                if key in check:
                    _identifier(check[key], f"check.{key}")
            # Reserve room for a replay envelope and the largest incident ID.
            envelope = (f"H3C LOG device={device['id']} kind={device['kind']} rack={device['rack']} "
                        f"epoch={'9' * 16} seq={'9' * 16} age_ms={'9' * 16} incident={'9' * 16}-{'9' * 16} replay=true ")
            cause = max((check.get("cause", ""), check.get("action_cause", "")), key=len)
            bodies = [f"level=CRITICAL event={check['event']} status=LOCKED component={check['component']} cause={cause}",
                      f"level=INFO event=RECOVERY status=OK component={check['component']} fault={check['event']} cause=SSH_OBSERVATION"]
            if any(len((envelope + body).encode()) > 511 for body in bodies):
                raise ConfigError("check identifiers exceed the ESP32 USB frame capacity")
        actions = device.setdefault("actions", {})
        _object(actions, {"UNLOCK", "RECOVER_INTERFACE"}, label="actions")
        for action, policy in actions.items():
            _object(policy, {"enabled", "allowed_components", "commands"}, {"enabled", "allowed_components", "commands"}, f"actions.{action}")
            if not isinstance(policy["enabled"], bool) or not isinstance(policy["allowed_components"], list):
                raise ConfigError("actions require boolean enabled and a component allowlist")
            expected = "AUTH_LOCKED" if action == "UNLOCK" else "LINK_DOWN"
            for component in policy["allowed_components"]:
                _identifier(component, "allowed component", component=True)
                if (component, expected) not in keys:
                    raise ConfigError("action allowlist must reference a configured fault check")
            if policy["enabled"] and not policy["allowed_components"]:
                raise ConfigError("enabled action requires a nonempty component allowlist")
            if not isinstance(policy["commands"], list) or not policy["commands"]:
                raise ConfigError("actions.commands must be a nonempty list")
            for command in policy["commands"]:
                _command(command)
                try:
                    for literal, field, spec, conversion in string.Formatter().parse(command):
                        if field is not None and (field != "component" or spec or conversion):
                            raise ConfigError("command templates only support {component}")
                    command.format(component="allowed-component")
                except (ValueError, KeyError, IndexError, AttributeError) as exc:
                    raise ConfigError("command templates only support {component}") from exc
            budget = ssh.get("connect_timeout", 5) * 3 + ssh.get("command_timeout", 5) * (
                len(ssh.get("setup_commands", [])) + len(policy["commands"]) + 2 + (ssh["transport"] == "shell"))
            if policy["enabled"] and (budget + protocol["settle_ms"] / 1000 >= protocol["verify_ms"] / 1000):
                raise ConfigError("action SSH timeout budget exceeds protocol.verify_ms; increase the configured verification window")
        budget = ssh.get("connect_timeout", 5) * 3 + ssh.get("command_timeout", 5) * (
            len(ssh.get("setup_commands", [])) + len({check["command"] for check in checks}) + (ssh["transport"] == "shell"))
        if budget * 1000 >= protocol["stale_ms"] or budget > 110:
            raise ConfigError("collector SSH timeout budget exceeds source freshness or shutdown budget")
    if sum(len(device["checks"]) for device in devices) > 128:
        raise ConfigError("checks exceed the ESP32 global 128-fault capacity; split across agents")
    simulation = config.get("simulation", {})
    _object(simulation, {"devices", "seed", "min_interval", "max_interval", "fault_rate", "action_mode"}, label="simulation")
    if config["mode"] == "demo":
        _integer(simulation.get("devices", 1), 1, 32, "simulation.devices")
        _number(simulation.get("min_interval", 3), 0.1, 60, "simulation.min_interval")
        _number(simulation.get("max_interval", 5), simulation.get("min_interval", 3), 60, "simulation.max_interval")
        _number(simulation.get("fault_rate", 0.15), 0, 1, "simulation.fault_rate")
        if simulation.get("action_mode", "realistic") not in {"success", "failed", "timeout", "partial", "realistic"}:
            raise ConfigError("invalid simulation.action_mode")
    return config


def browser_config(config):
    """Only the browser's read-only account is exported, never gateway/SSH secrets."""
    web = config["web"]
    public = {"schema_version": 1, "mode": config["mode"], "mqtt": {"url": web["mqtt_url"], "username": web["username"],
            "password": credential(web)}, "topic_root": config["agent"]["topic_root"],
            "agent_ids": web["agent_ids"], "agent_name": config["agent"].get("name", config["agent"]["id"]),
            "serial_label": config["serial"]["port"]}
    if web.get("api_url"):
        public["api"] = {"url": web["api_url"], "token": credential(web, "api_token_env")}
    return public
