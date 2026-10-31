"""Exercise the dependency Compose in a disposable project, never the live volumes."""
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading
import urllib.error
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import paho.mqtt.client as mqtt
from pymongo import MongoClient
from esp_gateway.environment import load_environment


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def connect(port, username, password, transport="tcp", accepted=True):
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="deps-test-" + uuid.uuid4().hex,
                         transport=transport)
    if username:
        client.username_pw_set(username, password)
    if transport == "websockets":
        client.ws_set_options(path="/mqtt")
    ready, reasons = threading.Event(), []
    def on_connect(client, userdata, flags, reason, properties):
        reasons.append(reason)
        ready.set()
    client.on_connect = on_connect
    client.connect("127.0.0.1", port, keepalive=10)
    client.loop_start()
    if not ready.wait(5) or (reasons[0] == 0) != accepted:
        client.disconnect()
        client.loop_stop()
        raise AssertionError("MQTT authentication/connection did not match expectation")
    return client


def receive(client, topic, publish=None):
    ready, received = threading.Event(), []
    subscribed = threading.Event()
    client.on_subscribe = lambda *_: subscribed.set()
    def on_message(client, userdata, message):
        received.append(message.payload.decode())
        ready.set()
    client.on_message = on_message
    client.subscribe(topic, qos=1)
    assert subscribed.wait(5), "MQTT SUBACK timed out"
    if publish is not None:
        client.publish(topic, publish, qos=1, retain=True).wait_for_publish(5)
    assert ready.wait(5), "MQTT report round trip timed out"
    return received[0]


def main():
    load_environment(ROOT / "deploy/mongodb.env")
    env = os.environ.copy()
    ports = {key: str(free_port()) for key in ("MONGO_PORT", "MONGO_EXPRESS_PORT", "MQTT_PORT", "MQTT_WS_PORT")}
    assert len(set(ports.values())) == len(ports)
    env.update(ports)
    project = "esp-deps-check-" + uuid.uuid4().hex[:10]
    command = ["docker", "compose", "-p", project, "--env-file", str(ROOT / "deploy/mongodb.env"),
               "-f", str(ROOT / "compose.yaml")]
    def compose(*args):
        subprocess.run([*command, *args], env=env, cwd=ROOT, check=True)
    checks = []
    def passed(name):
        checks.append(name)
        print("PASS", name, flush=True)
    clients = []
    database = None
    try:
        compose("config", "--quiet")
        compose("up", "-d", "--wait", "--wait-timeout", "90")
        passed("all three Docker dependencies become healthy on isolated ports")
        database = MongoClient("127.0.0.1", int(ports["MONGO_PORT"]), username=env["MONGO_USERNAME"],
                               password=env["MONGO_PASSWORD"], authSource=env["MONGO_DATABASE"],
                               serverSelectionTimeoutMS=5000)
        collection = database[env["MONGO_DATABASE"]].dependency_acceptance
        collection.insert_one({"_id": project, "value": "persistent"})
        url = f"http://127.0.0.1:{ports['MONGO_EXPRESS_PORT']}/db/{env['MONGO_DATABASE']}"
        try:
            urllib.request.urlopen(url, timeout=5)
            raise AssertionError("database manager accepted anonymous access")
        except urllib.error.HTTPError as error:
            assert error.code == 401
        authorization = base64.b64encode((env["MONGO_EXPRESS_USERNAME"] + ":" + env["MONGO_EXPRESS_PASSWORD"]).encode()).decode()
        with urllib.request.urlopen(urllib.request.Request(url, headers={"Authorization": "Basic " + authorization}), timeout=10) as reply:
            assert reply.status == 200
        passed("MongoDB application user can persist data; manager requires its independent login")
        topic = env.get("MQTT_TOPIC_ROOT", "h3c") + "/dependency-test/" + project
        for transport, key in (("tcp", "MQTT_PORT"), ("websockets", "MQTT_WS_PORT")):
            client = connect(int(ports[key]), env.get("MQTT_USERNAME", "esp32"), env["ESP_DEMO_MQTT_PASSWORD"], transport)
            clients.append(client)
            assert receive(client, topic + "/" + transport, publish=transport) == transport
        passed("authenticated MQTT QoS 1 round trip works over both TCP and /mqtt WebSocket")
        for username, password in ((None, None), (env.get("MQTT_USERNAME", "esp32"), "wrong-" + uuid.uuid4().hex)):
            client = connect(int(ports["MQTT_PORT"]), username, password, accepted=False)
            clients.append(client)
        passed("MQTT rejects anonymous access and incorrect passwords")
        for client in clients:
            client.disconnect()
            client.loop_stop()
        clients.clear()
        compose("restart", "mosquitto", "mongodb")
        compose("up", "-d", "--wait", "--wait-timeout", "90")
        assert collection.find_one({"_id": project})["value"] == "persistent"
        client = connect(int(ports["MQTT_PORT"]), env.get("MQTT_USERNAME", "esp32"), env["ESP_DEMO_MQTT_PASSWORD"])
        clients.append(client)
        assert receive(client, topic + "/tcp") == "tcp"
        passed("MongoDB documents and retained MQTT reports survive container restarts")
    except BaseException:
        compose("logs", "--tail", "45", "mosquitto")
        raise
    finally:
        for client in clients:
            client.disconnect()
            client.loop_stop()
        if database:
            database.close()
        # This project and its volumes were created solely for this disposable test.
        compose("down", "-v", "--remove-orphans")
    destination = ROOT / "test/.runtime/docker-dependencies-acceptance.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps({"passed": checks}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
