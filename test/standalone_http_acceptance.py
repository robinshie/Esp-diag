#!/usr/bin/env python3
"""Verify the actual ESP over Wi-Fi/HTTP. No serial input or configuration writes."""
import argparse
import base64
import json
from pathlib import Path
import time
from urllib.error import HTTPError
from urllib.parse import parse_qs, urlencode
from urllib.request import build_opener, ProxyHandler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.4.1")
    parser.add_argument("--wait", type=float, default=150)
    args = parser.parse_args()
    base = args.url.rstrip("/")
    opener = build_opener(ProxyHandler({}))

    def read(path):
        with opener.open(base + path, timeout=5) as reply:
            return reply.read().decode("utf-8")

    def get(path):
        return json.loads(read(path))

    deadline = time.monotonic() + args.wait
    last = None
    while time.monotonic() < deadline:
        try:
            device = get("/device/status")
            assert device["mode"] == "standalone" and device["log_source"] == "esp_native", device
            assert device["ai_on_device"] is True
            storage = parse_qs(read("/storage"))
            current = (device["ssh_state"], storage.get("state"), device["heartbeat"])
            if current != last:
                print(json.dumps({"ssh": current[0], "storage": current[1], "heartbeat": current[2]}, ensure_ascii=False), flush=True)
                last = current
            if device["ssh_state"] == "connected" and "heartbeat_state=healthy" in device["heartbeat"] and storage.get("state") == ["ready"] and device.get("bdi", {}).get("cycle", 0) > 0 and device.get("bdi", {}).get("observation_valid") is True:
                listing = get("/storage/files")
                if listing.get("state") == "ready" and listing.get("files"):
                    break
        except (OSError, ValueError) as error:
            print(type(error).__name__ + ": waiting for ESP Wi-Fi page", flush=True)
        time.sleep(1)
    else:
        raise SystemExit("ESP did not become ready; connect to ESP-Logs and triple-click BOOT to reopen the window.")

    assert device.get("firmware_version") == "native-bdi-20261102-r3", device
    assert isinstance(device.get("bdi"), dict) and device["bdi"]["cycle"] > 0, device
    assert device['bdi']['observation_valid'] is True, device
    assert isinstance(device.get("mqtt"), dict) and isinstance(device.get("analysis"), dict), device
    assert listing["source"] == "esp_native"
    filename = listing["files"][0]["name"]
    chunks = []
    offset = 0
    while True:
        reply = get("/storage/read?" + urlencode({"file": filename, "offset": offset}))
        job = reply.get("job")
        for _ in range(80):
            if reply.get("state") not in {"pending", "busy"}:
                break
            time.sleep(.1)
            reply = get("/storage/read?" + urlencode({"job": job} if job else {"file": filename, "offset": offset}))
            job = reply.get("job", job)
        assert reply["state"] == "ready", reply
        chunk = base64.b64decode(reply["base64"], validate=True)
        assert reply["start"] == offset and reply["next"] == offset + len(chunk)
        chunks.append(chunk)
        offset = reply["next"]
        assert offset <= 100000
        if reply["eof"]:
            break
        assert chunk
    report = json.loads(b"".join(chunks).decode("utf-8"))
    assert report["source"] == "esp_native" and report["transport"] == "ETH" and report["clock"] == "uptime"
    assert report["boot"] and report["lines"] and len(report["lines"]) <= 64
    assert report["boot"] == device["boot"], "Expected current boot evidence"
    text = next((line["text"] for line in report["lines"] if line["text"].startswith("[BDI][STATUS]")), report["lines"][0]["text"])
    inference = get("/model/explain?" + urlencode({"log": text}))
    assert inference["experimental"] is True and isinstance(inference["accepted"], bool)
    assert inference["model"] and 0 <= inference["score"] <= 1
    html = read("/archive")
    assert "一键分析最近日志" in html and "正式版记录由 ESP 本机采集" in html
    try:
        read("/network")
    except HTTPError as error:
        assert error.code == 403
    else:
        raise AssertionError("Expected read-only BOOT logs mode, not configuration mode")
    evidence = {"passed": True, "loop_detection_enabled": device['bdi'].get('loop_detection_enabled'),
                "runtime_transport": "Wi-Fi HTTP only", "device": device, "storage": storage,
                "file": filename, "bytes": offset, "lines": len(report["lines"]), "source": report["source"],
                "boot": report["boot"], "clock": report["clock"], "inference": inference}
    target = Path(__file__).parent / ".runtime" / "standalone-http-acceptance.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, ensure_ascii=False, indent=2))
    print(json.dumps(evidence, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
