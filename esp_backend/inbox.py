"""Durable MQTT receipt queue, independent of USB/action gateway state."""
import sqlite3
import threading
from pathlib import Path


class Inbox:
    def __init__(self, path, limit=50000):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.lock, self.limit = threading.RLock(), limit
        # Keep the existing outbox format so an upgrade can drain queued data.
        self.db.execute("CREATE TABLE IF NOT EXISTS outbox(id INTEGER PRIMARY KEY, topic TEXT, payload TEXT, retain INTEGER)")
        self.db.commit()

    def enqueue(self, topic, payload):
        with self.lock, self.db:
            if self.db.execute("SELECT COUNT(*) FROM outbox").fetchone()[0] >= self.limit:
                raise BufferError("MQTT inbox capacity reached")
            self.db.execute("INSERT INTO outbox(topic,payload,retain) VALUES(?,?,0)", (topic, payload))

    def first(self):
        with self.lock:
            return self.db.execute("SELECT id,topic,payload,retain FROM outbox ORDER BY id LIMIT 1").fetchone()

    def delivered(self, row_id):
        with self.lock, self.db:
            self.db.execute("DELETE FROM outbox WHERE id=?", (row_id,))

    def close(self):
        with self.lock:
            self.db.close()
