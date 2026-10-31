#!/usr/bin/env python3
"""Keep the real ESP32 gateway, frontend and local MQTT demo running visibly in logs."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "test/.runtime/live-demo"
STATE = RUNTIME / "processes.json"
CACHED_DEPS = ROOT / "test/.runtime/broker-deps"
if CACHED_DEPS.exists():
    sys.path.insert(0, str(CACHED_DEPS))
sys.path.insert(0, str(ROOT))


def demo_config():
    from esp_gateway.config import load_config
    from esp_gateway.environment import load_environment
    load_environment(ROOT / "deploy/mongodb.env", missing_ok=True)
    os.environ.setdefault("ESP_DEMO_MQTT_PASSWORD", "esp32-bdi-test")
    return load_config(ROOT / "config/demo.json")


def trace(stage, payload):
    print(f"{datetime.now().astimezone().isoformat(timespec='seconds')} [{stage}] {payload}", flush=True)


def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except PermissionError:
        return True
    except OSError:
        return False


def listening(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    except OSError:
        return False


def status():
    if not STATE.exists():
        return {}
    return {role: {**record, "running": alive(record["pid"])}
            for role, record in json.loads(STATE.read_text()).items()}


def stop():
    for role, record in status().items():
        if record["running"]:
            os.killpg(record["pid"], signal.SIGTERM)
            trace("STOP", f"{role} pid={record['pid']}")


def stop_gateway():
    records = json.loads(STATE.read_text())
    old = records["gateway"]["pid"]
    if alive(old):
        os.killpg(old, signal.SIGTERM)
        for _ in range(100):
            if not alive(old):
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("old gateway did not exit; no second serial reader started")
    return records


def restart_gateway():
    records = stop_gateway()
    log_path = RUNTIME / "gateway.log"
    with log_path.open("a") as log:
        process = subprocess.Popen([sys.executable, "-u", __file__, "gateway"], cwd=ROOT,
                                   stdout=log, stderr=log, start_new_session=True)
    records["gateway"] = {"pid": process.pid, "log": str(log_path)}
    STATE.write_text(json.dumps(records, indent=2))
    trace("RESTART", f"gateway pid={process.pid}; frontend and MQTT kept running")


def start_backend():
    records = json.loads(STATE.read_text()) if STATE.exists() else {}
    if records.get("backend") and alive(records["backend"]["pid"]):
        return
    config = demo_config()
    api_port = int(os.environ.get("ESP_API_PORT", "8080"))
    if listening(api_port):
        raise RuntimeError("API port already in use; existing service left untouched")
    python = ROOT / ".venv/bin/python"
    if not python.exists():
        raise RuntimeError("install backend dependencies in .venv before starting MongoDB/API demo")
    RUNTIME.mkdir(parents=True, exist_ok=True)
    log_path = RUNTIME / "backend.log"
    with log_path.open("a") as log:
        process = subprocess.Popen([str(python), "-u", "-m", "esp_backend", "--config", str(ROOT / "config/demo.json"),
                                    "--env-file", str(ROOT / "deploy/mongodb.env")], cwd=ROOT,
                                   stdout=log, stderr=log, start_new_session=True)
    records["backend"] = {"pid": process.pid, "log": str(log_path)}
    STATE.write_text(json.dumps(records, indent=2))
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError("backend failed; see backend.log")
        if listening(api_port):
            trace("READY", f"MongoDB API=http://127.0.0.1:{api_port}/api/health")
            return
        time.sleep(0.1)
    raise TimeoutError("backend API did not become ready")


def restart_backend():
    records = json.loads(STATE.read_text()) if STATE.exists() else {}
    if records.get("backend") and alive(records["backend"]["pid"]):
        old = records["backend"]["pid"]
        os.killpg(old, signal.SIGTERM)
        for _ in range(300):
            if not alive(old):
                break
            time.sleep(0.1)
        else:
            raise TimeoutError("old backend did not exit")
    start_backend()


def start():
    from esp_gateway.config import browser_config
    from urllib.parse import urlparse
    subprocess.run([sys.executable, str(ROOT / "deploy/init_mongodb_env.py")], check=True)
    config = demo_config()
    mqtt_port = config["mqtt"]["port"]
    ws_port = urlparse(config["web"]["mqtt_url"]).port or 80
    expected = {"MQTT_PORT": str(mqtt_port), "MQTT_WS_PORT": str(ws_port),
                "MQTT_USERNAME": config["mqtt"]["username"], "MQTT_TOPIC_ROOT": config["agent"]["topic_root"]}
    for key, value in expected.items():
        if os.environ.get(key) != value:
            raise RuntimeError(f"{key} in deploy/mongodb.env must match config/demo.json")
    # Upgrade earlier demos without leaving two brokers competing for the same
    # ports. Only retire the broker explicitly owned by this launcher's state.
    records = json.loads(STATE.read_text()) if STATE.exists() else {}
    legacy = records.pop("broker", None)
    if legacy:
        if alive(legacy["pid"]):
            os.killpg(legacy["pid"], signal.SIGTERM)
            for _ in range(200):
                if not alive(legacy["pid"]):
                    break
                time.sleep(0.1)
            else:
                raise TimeoutError("old demo broker did not exit; no Docker broker started")
        STATE.write_text(json.dumps(records, indent=2))
        trace("MIGRATE", "retired the launcher's old Python MQTT broker")
    # Older checkouts also had a separate mqtt/ Compose project. Stop only
    # containers labelled as that exact repository directory; preserve its data.
    old_containers = subprocess.check_output([
        "docker", "ps", "--filter", f"label=com.docker.compose.project.working_dir={ROOT / 'mqtt'}",
        "--filter", "label=com.docker.compose.service=mosquitto", "--format", "{{.ID}}"], text=True).split()
    if old_containers:
        subprocess.run(["docker", "stop", *old_containers], check=True)
        trace("MIGRATE", "stopped the repository's legacy MQTT containers; old volumes preserved")
    services = ["mosquitto"]
    if config["web"].get("api_url"):
        services += ["mongodb", "mongo-express"]
    subprocess.run(["docker", "compose", "--env-file", str(ROOT / "deploy/mongodb.env"),
                    "up", "-d", "--wait", *services], cwd=ROOT, check=True)
    for _ in range(50):
        if listening(mqtt_port) and listening(ws_port):
            break
        time.sleep(0.1)
    else:
        raise RuntimeError("Docker MQTT is healthy internally but host ports are unavailable; check port conflicts and recreate mosquitto")
    existing = status()
    if any(record["running"] for record in existing.values()):
        print(json.dumps(existing, indent=2))
        if not all(record["running"] for record in existing.values()):
            raise RuntimeError("partial demo still running; run live_demo.py stop before restarting")
        if config["web"].get("api_url"):
            start_backend()
        return
    for port in (4173,):
        if listening(port):
            raise RuntimeError(f"port {port} is already in use; existing service left untouched")
    import serial  # dependency check before spawning anything
    npm = shutil.which("npm")
    if not npm:
        raise RuntimeError("npm is not on PATH")
    RUNTIME.mkdir(parents=True, exist_ok=True)
    public_config = ROOT / "frontend/public/runtime-config.json"
    public_config.parent.mkdir(parents=True, exist_ok=True)
    public_config.write_text(json.dumps(browser_config(config), indent=2) + "\n")
    records = {}

    def spawn(role, command, cwd=ROOT):
        log_path = RUNTIME / f"{role}.log"
        with log_path.open("a") as log:
            process = subprocess.Popen(command, cwd=cwd, stdout=log, stderr=log,
                                       start_new_session=True)
        records[role] = {"pid": process.pid, "log": str(log_path)}
        STATE.write_text(json.dumps(records, indent=2))
        return process

    def wait(process, port):
        for _ in range(100):
            if process.poll() is not None:
                raise RuntimeError(f"service failed; see logs in {RUNTIME}")
            if listening(port):
                return
            time.sleep(0.1)
        raise TimeoutError(f"port {port} did not become ready")

    try:
        spawn("mqtt", [sys.executable, "-u", __file__, "watch"])
        spawn("gateway", [sys.executable, "-u", __file__, "gateway"])
        frontend = spawn("frontend", [npm, "run", "dev", "--", "--strictPort"], ROOT / "frontend")
        wait(frontend, 4173)
        if config["web"].get("api_url"):
            start_backend()
        trace("READY", f"console=http://127.0.0.1:4173/console.html MQTT=127.0.0.1:{mqtt_port} WS={config['web']['mqtt_url']}")
        print(json.dumps(status(), indent=2))
    except Exception:
        stop()
        raise


def gateway():
    from esp_gateway.runtime import build_gateway
    from esp_gateway.config import browser_config
    config = demo_config()
    public_config = ROOT / "frontend/public/runtime-config.json"
    public_config.parent.mkdir(parents=True, exist_ok=True)
    public_config.write_text(json.dumps(browser_config(config), indent=2) + "\n")
    agent = build_gateway(config)
    original_send, original_handle = agent.send_usb, agent._handle_line

    def send(line):
        trace("USB -> ESP32", line)
        return original_send(line)

    def receive(line):
        trace("ESP32 -> GATEWAY", line)
        return original_handle(line)

    agent.send_usb, agent._handle_line = send, receive
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: agent.stop_event.set())
    trace("START", "realistic simulation -> real USB ESP32 Embedded-BDI -> MQTT; delayed verification, retries and policy cooldown")
    agent.run()


def watch():
    import paho.mqtt.client as mqtt
    config = demo_config()
    from esp_gateway.config import credential
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="esp-live-demo-monitor")
    client.username_pw_set(config["mqtt"]["username"], credential(config["mqtt"]))

    def connected(client, userdata, flags, reason, properties):
        trace("MQTT", f"connected result={reason}; subscribing h3c/#")
        client.subscribe(config["agent"]["topic_root"] + "/#", qos=1)

    client.on_connect = connected
    client.on_message = lambda client, userdata, msg: trace(msg.topic, msg.payload.decode(errors="replace"))
    client.connect(config["mqtt"]["host"], config["mqtt"]["port"], keepalive=30)
    client.loop_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("role", choices=["start", "stop", "status", "stop-gateway", "restart-gateway", "start-backend", "restart-backend", "broker", "gateway", "watch"])
    role = parser.parse_args().role
    if role == "broker":
        import asyncio
        import logging
        from local_mqtt_broker import run
        from urllib.parse import urlparse
        config = demo_config()
        logging.basicConfig(level=logging.INFO)
        asyncio.run(run(config["mqtt"]["port"], urlparse(config["web"]["mqtt_url"]).port or 80))
    elif role == "status":
        print(json.dumps(status(), indent=2))
    elif role == "restart-gateway":
        restart_gateway()
    elif role == "stop-gateway":
        stop_gateway()
    elif role == "start-backend":
        start_backend()
    elif role == "restart-backend":
        restart_backend()
    else:
        globals()[role]()
