"""Durable MQTT intake: receipt -> local inbox -> idempotent MongoDB write."""
import fcntl
import json
import logging
import threading
import time

import paho.mqtt.client as mqtt

from esp_gateway.config import credential
from esp_gateway.store import Store, fields
from .repository import agent_id, validate_report

log = logging.getLogger(__name__)


class ReportIngestor:
    def __init__(self, settings, repository):
        self.settings, self.repository = settings, repository
        self.store = Store(settings.state_path)
        self.lock = open(settings.state_path + ".lock", "a")
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            self.store.close()
            self.lock.close()
            raise
        self.stop_event, self.connected = threading.Event(), threading.Event()
        self.archive_error = False
        cfg = settings.config["mqtt"]
        self.root = settings.config["agent"]["topic_root"]
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=cfg["client_id"] + "-archive",
                                  clean_session=False, manual_ack=True)
        self.client.username_pw_set(cfg["username"], credential(cfg))
        tls = cfg.get("tls", {})
        if tls.get("enabled"):
            self.client.tls_set(ca_certs=tls.get("ca_file"), certfile=tls.get("cert_file"), keyfile=tls.get("key_file"))
        self.client.connect_timeout = 3
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = lambda *_: self.connected.clear()
        self.client.on_message = self.on_message
        self.threads = []

    def on_connect(self, client, userdata, flags, reason, properties):
        if self.stop_event.is_set():
            client.disconnect()
            return
        if reason == 0:
            self.connected.set()
            client.subscribe([(self.root + "/+/report", 1), (self.root + "/+/availability", 1)])

    def on_message(self, client, userdata, message):
        try:
            parts = message.topic.split("/")
            if len(parts) != 3 or parts[0] != self.root:
                raise ValueError("unexpected topic")
            agent_id(parts[1])
            if len(message.payload) > 2 * 1024 * 1024:
                raise ValueError("oversized report")
            payload = message.payload.decode()
            if parts[2] == "report":
                validate_report(json.loads(payload))
            elif parts[2] == "availability":
                if fields(payload).get("state") not in {"ONLINE", "OFFLINE"}:
                    raise ValueError("invalid availability")
            else:
                raise ValueError("unexpected channel")
        except (ValueError, UnicodeError, TypeError):
            log.warning("discarded malformed ESP32 report")
            client.ack(message.mid, message.qos)
            return
        try:
            self.store.enqueue(message.topic, json.dumps({"payload": payload, "retained": bool(message.retain),
                                                          "at_ms": int(time.time() * 1000)}))
            client.ack(message.mid, message.qos)  # Only after the inbox commit.
        except Exception as exc:
            log.error("MQTT inbox unavailable: %s; disconnecting without ACK", type(exc).__name__)
            client.disconnect()

    def mqtt_loop(self):
        cfg = self.settings.config["mqtt"]
        while not self.stop_event.is_set():
            try:
                self.client.connect(cfg["host"], cfg["port"], keepalive=30)
                self.client.loop_forever(retry_first_connection=True)
            except Exception as exc:
                log.warning("archive MQTT connection failed: %s", type(exc).__name__)
            self.stop_event.wait(2)

    def archive_loop(self):
        while not self.stop_event.is_set():
            row = self.store.first()
            if not row:
                self.stop_event.wait(0.1)
                continue
            row_id, topic, raw, _ = row
            try:
                item = json.loads(raw)
                identity, channel = topic.split("/")[1:]
                if channel == "report":
                    name = self.settings.config["agent"].get("name") if identity == self.settings.config["agent"]["id"] else None
                    self.repository.register_report(identity, json.loads(item["payload"]), item["retained"], name)
                else:
                    self.repository.record_availability(identity, fields(item["payload"])["state"], item["at_ms"])
                self.store.delivered(row_id)
                self.archive_error = False
            except Exception as exc:
                self.archive_error = True
                log.warning("MongoDB archive write failed: %s; inbox retained", type(exc).__name__)
                self.stop_event.wait(2)

    def start(self):
        self.threads = [threading.Thread(target=target, daemon=True) for target in (self.mqtt_loop, self.archive_loop)]
        for thread in self.threads:
            thread.start()

    def pending(self):
        with self.store.lock:
            return self.store.db.execute("SELECT COUNT(*) FROM outbox").fetchone()[0]

    def stop(self):
        self.stop_event.set()
        self.client.disconnect()
        for thread in self.threads:
            thread.join(20)
        if any(thread.is_alive() for thread in self.threads):
            raise RuntimeError("archive worker did not stop")
        self.store.close()
        self.lock.close()
