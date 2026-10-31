#!/usr/bin/env python3
"""Restart the local demo MongoDB; verify real ESP32 reports queue and catch up."""
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from esp_backend.repository import MongoRepository
from esp_backend.settings import Settings


def main():
    settings = Settings.load(ROOT / "config/demo.json", ROOT / "deploy/mongodb.env")
    command = ["docker", "compose", "--env-file", "deploy/mongodb.env"]
    repo = MongoRepository(settings)
    original = [(row["id"], row["name"], row["location"], row["enabled"]) for row in repo.list_devices()]
    before = repo.reports.count_documents({"topic_root": repo.root})
    gateway_log = ROOT / "test/.runtime/live-demo/gateway.log"
    log_size = gateway_log.stat().st_size
    queued = 0
    try:
        subprocess.run(command + ["stop", "mongodb"], cwd=ROOT, check=True)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            with sqlite3.connect("file:" + settings.state_path + "?mode=ro", uri=True) as inbox:
                queued = inbox.execute("SELECT COUNT(*) FROM outbox").fetchone()[0]
            if queued >= 2: break
            time.sleep(0.3)
        assert queued > 0, "no real ESP32 report was queued during the outage"
        assert gateway_log.stat().st_size > log_size, "USB gateway did not continue logging"
        try:
            urllib.request.urlopen("http://127.0.0.1:8080/api/health", timeout=8)
            raise AssertionError("database outage was not reported")
        except urllib.error.HTTPError as exc:
            assert exc.code == 503
        print(f"PASS MongoDB offline: {queued} queued reports; USB gateway continues; API returns 503", flush=True)
    finally:
        subprocess.run(command + ["up", "-d", "--wait", "mongodb"], cwd=ROOT, check=True)

    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        health = json.load(urllib.request.urlopen("http://127.0.0.1:8080/api/health", timeout=8))
        if health["pending_reports"] == 0 and not health["archive_error"]: break
        time.sleep(0.3)
    assert health["pending_reports"] == 0 and not health["archive_error"]
    assert health["mqtt_connected"]
    restored = [(row["id"], row["name"], row["location"], row["enabled"]) for row in repo.list_devices()]
    assert original == restored, "device metadata did not survive container restart"
    after = repo.reports.count_documents({"topic_root": repo.root})
    assert after > before
    repo.close()
    result = {"queued_during_outage": queued, "reports_before": before, "reports_after": after,
              "pending_after_recovery": health["pending_reports"], "device_metadata_preserved": True,
              "usb_continued": True}
    (ROOT / "test/.runtime/mongodb-outage.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS MongoDB restarted: metadata preserved, inbox drained, history increased", json.dumps(result), flush=True)


if __name__ == "__main__": main()
