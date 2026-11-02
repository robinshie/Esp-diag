#!/usr/bin/env python3
"""Exercise the real Docker MongoDB and API in an isolated topic namespace."""
import copy
from dataclasses import replace
from datetime import datetime, timezone
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

            copy_report = {'schema_version': 1, 'report_id': 'copy-original', 'sent_at_ms': 1200,
                'status': None, 'observations': [], 'lines': [{'at_ms': 1200, 'text': '[BDI][EVENT] event=THERMAL device=sw1'}]}
            repo.register_report('copy-test', copy_report)
            original = api.get('/api/devices/copy-test/sd-copy').json()['copy']
            import hashlib
            assert json.loads(original['payload']) == copy_report
            assert hashlib.sha256(original['payload'].encode()).hexdigest() == original['sha256']
            repo.register_report('copy-test', copy_report)
            assert api.get('/api/devices/copy-test/sd-copy?after=' + str(original['sequence'])).json()['copy'] is None
            repo.register_report('copy-test', {**copy_report, 'report_id': 'copy-late', 'sent_at_ms': 100})
            late = api.get('/api/devices/copy-test/sd-copy?after=' + str(original['sequence'])).json()['copy']
            assert late['report_id'] == 'copy-late' and late['sequence'] > original['sequence']
            explanation = api.get('/api/reports/copy-original/explain?agent=copy-test&line=0')
            assert explanation.status_code == 200 and explanation.json()['source'] == 'database'
            assert explanation.json()['model'] == 'tiny-log-v3-protocol'
            assert api.get('/api/reports/copy-original/explain?agent=other-esp&line=0').status_code == 404
            assert api.get('/api/reports/copy-original/explain?agent=copy-test&line=1').status_code == 404
            # Simulate an old pre-upgrade archive. Backfill gives it a later DB
            # sequence even though its event timestamp predates the current cursor.
            repo.reports.update_one({'topic_root': repo.root, 'agent_id': 'copy-test', 'report.report_id': 'copy-original'}, {'$unset': {'mirror_sequence': ''}})
            upgraded = api.get('/api/devices/copy-test/sd-copy?after=' + str(late['sequence'])).json()['copy']
            assert upgraded['report_id'] == 'copy-original' and json.loads(upgraded['payload']) == copy_report
            repo.reports.update_many({'topic_root': repo.root, 'agent_id': 'copy-test'}, {'$set': {'received_at': datetime(2020, 1, 1, tzinfo=timezone.utc)}})
            boundary = api.get('/api/devices/copy-test/sd-copy/boundary').json()['received_after_ms']
            assert api.get(f'/api/devices/copy-test/sd-copy?received_after_ms={boundary}').json()['copy'] is None
            interval_report = {**copy_report, 'report_id': 'copy-future', 'lines': [{'at_ms': 1300 + n, 'text': '[BDI][ACK] result=FAILED'} for n in range(130)]}
            repo.register_report('copy-test', interval_report)
            future = api.get(f'/api/devices/copy-test/sd-copy?received_after_ms={boundary}').json()['copy']
            assert future['report_id'] == 'copy-future' and json.loads(future['payload']) == interval_report
            explanation = api.get('/api/reports/copy-future/explain?agent=copy-test&line=129')
            assert explanation.status_code == 200 and explanation.json()['evidence_text'] == interval_report['lines'][129]['text']
            assert api.get('/api/reports/copy-future/explain?agent=copy-test&line=2048').status_code == 422
            passed('future-only SD boundary excludes old history and one interval retains over 64 lines for database AI')
            scoped = {**copy_report, 'boot': 'scope-boot', 'status': {'connection_id': 'scope-usb'}}
            for rid, at, boot in [('context-before', 3000, 'scope-boot'), ('context-target', 4000, 'scope-boot'),
                                  ('context-future', 5000, 'scope-boot'), ('context-otherboot', 3500, 'other-boot')]:
                repo.register_report('copy-test', {**scoped, 'report_id': rid, 'sent_at_ms': at, 'boot': boot})
            context = api.get('/api/reports/context-target/context?agent=copy-test').json()
            assert context['source'] == 'database' and {row['report_id'] for row in context['reports']} == {'context-before', 'context-target'}
            assert api.get('/api/reports/context-target/context?agent=another-agent').status_code == 404
            passed('incident context is scoped to device/boot/USB and excludes future records')
            repo.devices.delete_many({'topic_root': repo.root, 'agent_id': 'copy-test'})
            repo.reports.delete_many({'topic_root': repo.root, 'agent_id': 'copy-test'})
            repo.registrations.delete_one({'_id': repo.key('copy-test')})
            passed('DB-authoritative SD copy preserves all fields/hash, deduplicates, includes late and old archives; AI reads saved evidence only')

            report = {"schema_version": 1, "report_id": "new-report", "sent_at_ms": 2000,
                      "status": None, "observations": [{"device": "target-1", "locked": "true"}],
                      "lines": [{"at_ms": 2000, "text": "[BDI][STATE] locked=true"}]}
            repo.register_report("edge-test", report)
            repo.register_report("edge-test", report)
            assert len(repo.history()) == 1
            assert [row["id"] for row in repo.list_devices()] == ["edge-test"]
            assert api.get("/api/devices").json()["devices"][0]["report_count"] == 1
            passed("automatic ESP32 registration and duplicate delivery deduplication")

            metadata = {"name": "持久化验收设备", "location": "验收机架", "enabled": False, "device_number": "ESP-001"}
            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            repo.register_report("edge-test", {**report, "report_id": "old-report", "sent_at_ms": 1000})
            row = api.get("/api/devices").json()["devices"][0]
            assert row["name"] == metadata["name"] and row["enabled"] is False
            assert row["report_count"] == 2
            assert row["device_number"] == "ESP-001"
            assert row["latest_report"]["report_id"] == "new-report"
            repo.record_availability("edge-test", "OFFLINE", 3000)
            repo.record_availability("edge-test", "ONLINE", 1000)
            assert repo.list_devices()[0]["availability"] == "OFFLINE"
            passed("metadata survives reporting; old snapshots and availability cannot overwrite new state")

            renamed = {**metadata, "device_number": "ESP-002"}
            assert api.put("/api/devices/edge-test", json=renamed).status_code == 200
            assert api.put("/api/devices/another-edge", json={**renamed, "name": "duplicate"}).status_code == 409
            assert len(repo.list_devices()) == 1
            assert api.put("/api/devices/edge-test", json={key: value for key, value in metadata.items() if key != "device_number"}).status_code == 200
            assert repo.list_devices()[0]["device_number"] == "ESP-002", "old clients must preserve a saved number"
            assert len(repo.history('edge-test')) == 2
            assert api.put("/api/devices/edge-test", json={**metadata, "device_number": ""}).status_code == 200
            assert api.put("/api/devices/another-edge", json=renamed).status_code == 200
            assert api.delete("/api/devices/another-edge").status_code == 200
            assert api.put("/api/devices/edge-test", json=metadata).status_code == 200
            passed("device number can change or clear, is unique, survives old clients, and preserves identity/history")

            reopened = MongoRepository(settings)
            try:
                assert reopened.list_devices()[0]["location"] == "验收机架"
                assert reopened.list_devices()[0]["device_number"] == "ESP-001"
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
        cleanup.copy_sequences.delete_many({'topic_root': cleanup.root})
        cleanup.close()
    destination = ROOT / "test/.runtime/mongodb-acceptance.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps({"passed": checks}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__": main()
