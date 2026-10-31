#!/usr/bin/env python3
"""Read-only checks of the local Docker MongoDB management interface."""
import base64
from html import unescape
import json
import os
from pathlib import Path
import sys
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from esp_gateway.environment import load_environment


def main():
    load_environment(ROOT / "deploy/mongodb.env")
    base = "http://127.0.0.1:" + os.environ.get("MONGO_EXPRESS_PORT", "8081")
    credentials = os.environ["MONGO_EXPRESS_USERNAME"] + ":" + os.environ["MONGO_EXPRESS_PASSWORD"]
    authorization = "Basic " + base64.b64encode(credentials.encode()).decode()

    try:
        urlopen(base, timeout=5)
        raise AssertionError("management interface accepted an unauthenticated request")
    except HTTPError as error:
        assert error.code == 401
    print("PASS database manager requires independent web login", flush=True)

    database = quote(os.environ.get("MONGO_DATABASE", "esp_diag"), safe="")
    def page(path):
        request = Request(base + path, headers={"Authorization": authorization})
        with urlopen(request, timeout=10) as response:
            assert response.status == 200
            return unescape(response.read().decode())

    home = page("/")
    assert "/db/" + database in home and "/db/admin" not in home
    collections = page("/db/" + database)
    assert "devices" in collections and "reports" in collections
    print("PASS application database exposes devices and reports collections", flush=True)
    devices = page("/db/" + database + "/devices")
    reports = page("/db/" + database + "/reports")
    assert "esp32-s3-eth" in devices and "report_id" in reports
    print("PASS real ESP32 metadata and archived reports are visible", flush=True)
    health = json.load(urlopen("http://127.0.0.1:4173/api/health", timeout=5))
    assert health["database"] == "connected" and health["mqtt_connected"] and not health["archive_error"]
    print("PASS existing ESP32/MQTT/database experiment remains healthy", flush=True)
    result = {"management_auth_required": True, "collections_visible": ["devices", "reports"], "experiment_health": health}
    (ROOT / "test/.runtime/mongo-express-acceptance.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__": main()
