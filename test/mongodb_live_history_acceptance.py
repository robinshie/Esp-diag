"""Live-session counts, permanent history and keyset queries against real MongoDB.

Uses an isolated topic namespace and removes only its own test documents.
"""
import copy
from dataclasses import replace
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from fastapi.testclient import TestClient
from esp_backend.api import create_app
from esp_backend.repository import MongoRepository
from esp_backend.settings import Settings
from test.mongodb_acceptance import NoMQTT


def main():
    settings = Settings.load(ROOT / "config/demo.json", ROOT / "deploy/mongodb.env")
    config = copy.deepcopy(settings.config)
    config["agent"]["topic_root"] = "live-history-" + uuid.uuid4().hex
    settings = replace(settings, config=config, history_days=0)
    checks = []
    def passed(name):
        checks.append(name)
        print("PASS", name, flush=True)
    now = int(time.time() * 1000)
    old = now - 45 * 86400000
    def report(identity, at, usb="usb-old", boot="boot-old"):
        return {"schema_version": 1, "report_id": identity, "sent_at_ms": at, "boot": boot,
                "status": {"reasoning_cycle": "ACTIVE", "connection_id": usb},
                "observations": [], "lines": [{"at_ms": at, "text": "[BDI][STATUS]"}]}
    repo = MongoRepository(settings)
    try:
        # Simulate existing records written with the former 30-day policy.
        repo.history_days = 30
        repo.register_report("edge-a", report("old-report", old))
        assert repo.reports.count_documents({"topic_root": repo.root, "expires_at": {"$exists": True}}) == 1
        repo.close()
        repo = MongoRepository(settings)
        with TestClient(create_app(settings, repo, NoMQTT())) as api:
            api.headers["Authorization"] = "Bearer " + settings.token
            assert repo.reports.count_documents({"topic_root": repo.root, "expires_at": {"$exists": True}}) == 0
            assert api.get(f"/api/reports?end_ms={old}").json()["total"] == 1
            assert api.get("/api/reports").json()["history_days"] == 0
            passed("permanent retention removes existing TTL deadlines and preserves queryable reports older than 30 days")

            current = report("current-1", now - 10, "usb-current", "boot-current")
            repo.register_report("edge-a", current)
            repo.register_report("edge-a", current)
            repo.register_report("edge-a", report("current-2", now - 9, "usb-current", "boot-current"))
            assert api.get("/api/devices?scope=live").json()["devices"][0]["report_count"] == 2
            assert api.get("/api/devices?scope=history").json()["devices"][0]["report_count"] == 3
            assert api.get("/api/reports?scope=live").json()["total"] == 2
            # Opening a new repository represents a backend/browser restart.
            reopened = MongoRepository(settings)
            try:
                reopened.initialize()
                assert reopened.list_devices("live")[0]["report_count"] == 2
            finally:
                reopened.close()
            passed("current connection counts exclude old sessions, survive reopening, and deduplicate redelivery")

            repo.register_report("edge-a", report("reboot", now - 8, "usb-current", "boot-new"))
            assert api.get("/api/reports?scope=live").json()["total"] == 1
            # Late traffic from an old connection cannot switch the live session.
            repo.register_report("edge-a", report("late-old", old + 1))
            assert api.get("/api/reports?scope=live").json()["reports"][0]["report"]["report_id"] == "reboot"
            api.delete("/api/devices/edge-a")
            assert api.get("/api/reports?scope=live").json()["total"] == 0
            assert api.get("/api/reports?scope=history&agent=edge-a").json()["total"] == 5
            repo.register_report("edge-a", report("reconnect", now - 7, "usb-next", "boot-new"))
            assert api.get("/api/devices?scope=live").json()["devices"][0]["report_count"] == 1
            passed("hardware reboot/reconnect starts a new live count while removed devices remain queryable in history")

            # Equal timestamps exercise the secondary cursor key.
            for index in range(7):
                repo.register_report("edge-b", report(f"equal-{index}", now, "usb-b", "boot-b"))
            url = f"/api/reports?scope=history&agent=edge-b&start_ms={now}&end_ms={now}&limit=2"
            seen, cursor = [], None
            while True:
                payload = api.get(url + ("&cursor=" + cursor if cursor else "")).json()
                assert payload["total"] == 7
                seen.extend(row["report"]["report_id"] for row in payload["reports"])
                cursor = payload["next_cursor"]
                if not cursor:
                    break
            assert len(seen) == len(set(seen)) == 7
            assert api.get(f"/api/reports?agent=edge-a&end_ms={old+1}").json()["total"] == 2
            for suffix in ("scope=bad", "cursor=bad", "start_ms=2&end_ms=1", "start_ms=-1", "limit=201"):
                assert api.get("/api/reports?" + suffix).status_code == 422
            passed("device/time filters and stable cursor pagination cover all equal-timestamp records; malformed queries are rejected")

            assert repo.reports.count_documents({"topic_root": repo.root, "expires_at": {"$exists": True}}) == 0
            # Deletion in Mongo Express must change both scoped counts immediately.
            repo.reports.delete_many({"topic_root": repo.root, "agent_id": "edge-a", "report.status.connection_id": "usb-next"})
            row = next(row for row in api.get("/api/devices?scope=live").json()["devices"] if row["id"] == "edge-a")
            assert row["report_count"] == 0 and row["latest_report"] is None
            assert api.get("/api/reports?agent=edge-a").json()["total"] == 5
            passed("external deletion updates live counts without erasing older history; new records have no expiration")
    finally:
        cleanup = MongoRepository(settings)
        for collection in (cleanup.devices, cleanup.reports, cleanup.registrations):
            collection.delete_many({"topic_root": cleanup.root})
        cleanup.close()
    destination = ROOT / "test/.runtime/mongodb-live-history-acceptance.json"
    destination.write_text(json.dumps({"passed": checks}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    main()
