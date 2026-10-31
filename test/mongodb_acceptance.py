#!/usr/bin/env python3
"""Exercise the real Docker MongoDB and API in an isolated topic namespace."""
import copy
from dataclasses import replace
import json
from pathlib import Path
import sys
import threading
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from fastapi.testclient import TestClient
from esp_backend.api import create_app
from esp_backend.repository import MongoRepository
from esp_backend.settings import Settings


class NoMQTT:
    connected = threading.Event()
    archive_error = False
    def start(self): pass
    def stop(self): pass
    def pending(self): return 0


def main():
    settings = Settings.load(ROOT / "config/demo.json", ROOT / "deploy/mongodb.env")
    config = copy.deepcopy(settings.config)
    config["agent"]["topic_root"] = "acceptance-" + uuid.uuid4().hex
    settings = replace(settings, config=config, history_days=30)
    repo = MongoRepository(settings)
    checks = []
    def passed(name):
        checks.append(name)
        print("PASS", name, flush=True)
    try:
        with TestClient(create_app(settings, repo, NoMQTT())) as api:
            assert api.get("/api/devices").status_code == 401
            api.headers["Authorization"] = "Bearer " + settings.token
            assert api.put("/api/devices/server", json={"name": "bad"}).status_code == 422
            assert api.put("/api/devices/edge-test", json={"name": "bad", "ssh_password": "forbidden"}).status_code == 422
            assert api.get("/api/reports?limit=201").status_code == 422
            passed("authentication and API validation")

            report = {"schema_version": 1, "report_id": "new-report", "sent_at_ms": 2000,
                      "status": None, "observations": [{"device": "target-1", "locked": "true"}],
                      "lines": [{"at_ms": 2000, "text": "[BDI][STATE] locked=true"}]}
            repo.register_report("edge-test", report)
            repo.register_report("edge-test", report)
            assert len(repo.history()) == 1
            assert [row["id"] for row in repo.list_devices()] == ["edge-test"]
            assert api.get("/api/devices").json()["devices"][0]["report_count"] == 1
            passed("automatic ESP32 registration and duplicate delivery deduplication")

            metadata = {"name": "持久化验收设备", "location": "验收机架", "enabled": False}
            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            repo.register_report("edge-test", {**report, "report_id": "old-report", "sent_at_ms": 1000})
            row = api.get("/api/devices").json()["devices"][0]
            assert row["name"] == metadata["name"] and row["enabled"] is False
            assert row["report_count"] == 2
            assert row["latest_report"]["report_id"] == "new-report"
            repo.record_availability("edge-test", "OFFLINE", 3000)
            repo.record_availability("edge-test", "ONLINE", 1000)
            assert repo.list_devices()[0]["availability"] == "OFFLINE"
            passed("metadata survives reporting; old snapshots and availability cannot overwrite new state")

            reopened = MongoRepository(settings)
            try:
                assert reopened.list_devices()[0]["location"] == "验收机架"
                assert len(reopened.history("edge-test")) == 2
                assert reopened.list_devices()[0]["report_count"] == 2
            finally: reopened.close()
            passed("new database connection restores metadata and history")

            index = repo.reports.index_information()["history_retention"]
            assert index["expireAfterSeconds"] == 0
            document = repo.reports.find_one({"topic_root": repo.root})
            assert 29 < (document["expires_at"] - document["received_at"]).total_seconds() / 86400 < 31
            assert len(api.get("/api/reports?agent=edge-test&limit=1").json()["reports"]) == 1
            passed("history query and 30-day TTL index")

            assert api.delete("/api/devices/edge-test").json()["history_retained"] is True
            assert not repo.list_devices() and len(repo.history()) == 2
            repo.register_report("edge-test", report)
            assert not repo.list_devices(), "MQTT redelivery resurrected a removed device"
            passed("device removal preserves history and is not undone by reporting")

            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            # Reproduce the user's deletion through the manager, bypassing API.
            repo.devices.delete_one({"_id": repo.key("edge-test")})
            repo.register_report("edge-test", report)
            assert not repo.list_devices(), "direct database deletion was undone"
            # Upgrade an older database whose devices were removed before the
            # persistent discovery collection existed.
            repo.registrations.delete_one({"_id": repo.key("edge-test")})
            reopened = MongoRepository(settings)
            try:
                reopened.initialize()
                reopened.register_report("edge-test", report)
                assert not reopened.list_devices(), "backend restart resurrected a removed device"
            finally: reopened.close()
            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            assert repo.list_devices()[0]["name"] == metadata["name"]
            passed("direct MongoDB deletion survives reporting and restart; manual addition restores the device")

            # A removed device stays absent for its current USB session, but a
            # genuinely new USB connection restores plug-and-play discovery.
            connected = {**report, "report_id": "connection-a", "sent_at_ms": 3000,
                         "boot": "boot-a", "status": {"reasoning_cycle": "ACTIVE", "connection_id": "usb-a"}}
            repo.register_report("edge-test", connected)
            repo.devices.delete_one({"_id": repo.key("edge-test")})
            repo.register_report("edge-test", {**connected, "report_id": "connection-a-next", "sent_at_ms": 3100})
            assert not repo.list_devices()
            reconnect = {**connected, "report_id": "connection-b", "sent_at_ms": 3200,
                         "status": {"reasoning_cycle": "ACTIVE", "connection_id": "usb-b"}}
            repo.register_report("edge-test", reconnect, retained=True)
            assert not repo.list_devices(), "retained snapshot triggered discovery"
            repo.register_report("edge-test", {**reconnect, "report_id": "connection-b-live", "sent_at_ms": 3300})
            assert repo.list_devices()[0]["id"] == "edge-test"
            repo.devices.delete_one({"_id": repo.key("edge-test")})
            repo.register_report("edge-test", connected)
            assert not repo.list_devices(), "old USB session replay restored a removed device"
            repo.register_report("edge-test", {**reconnect, "report_id": "connection-b-reboot", "sent_at_ms": 3400,
                                                "boot": "boot-b"})
            assert repo.list_devices()[0]["id"] == "edge-test"
            repo.reports.delete_many({"topic_root": repo.root, "report.report_id": {"$regex": "^connection-"}})
            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            passed("new live USB session or hardware boot rediscovers removed devices; current session and stale retained replay do not")

            # Refresh must restore the archive count, not the browser's receipt
            # count or just the 50 records shown by the history panel.
            for index in range(55):
                repo.register_report("edge-test", {**report, "report_id": f"history-{index}", "sent_at_ms": 5000 + index})
            response = api.get("/api/devices").json()
            assert response["history_days"] == settings.history_days
            assert response["devices"][0]["report_count"] == 57
            assert len(api.get("/api/reports?limit=50").json()["reports"]) == 50
            reopened = MongoRepository(settings)
            try: assert reopened.list_devices()[0]["report_count"] == 57
            finally: reopened.close()
            repo.reports.delete_one({"topic_root": repo.root, "report.report_id": "old-report"})
            assert api.get("/api/devices").json()["devices"][0]["report_count"] == 56
            passed("refresh restores full database counts beyond the 50-row history, including archive expiration/removal")

            name_before_deletion = api.get("/api/devices").json()["devices"][0]["name"]
            repo.reports.delete_many({"topic_root": repo.root, "report.report_id": {"$regex": "^history-"}})
            row = api.get("/api/devices").json()["devices"][0]
            assert row["report_count"] == 1
            assert row["latest_report"]["report_id"] == "new-report"
            repo.reports.delete_many({"topic_root": repo.root})
            repo.registrations.delete_many({"topic_root": repo.root})
            row = api.get("/api/devices").json()["devices"][0]
            assert row["report_count"] == 0 and row["latest_report"] is None
            assert api.get("/api/reports").json()["reports"] == []
            assert row["name"] == name_before_deletion
            passed("external archive deletion removes stale snapshots, rolls back to surviving reports, and clears empty history")
            # Cleanup while the shared client is still open; never drop the database.
            repo.devices.delete_many({"topic_root": repo.root})
            repo.reports.delete_many({"topic_root": repo.root})
    finally:
        cleanup = MongoRepository(settings)
        cleanup.devices.delete_many({"topic_root": cleanup.root})
        cleanup.reports.delete_many({"topic_root": cleanup.root})
        cleanup.registrations.delete_many({"topic_root": cleanup.root})
        cleanup.close()
    destination = ROOT / "test/.runtime/mongodb-acceptance.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps({"passed": checks}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__": main()
