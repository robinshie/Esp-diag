"""Versioned, read-only MQTT boundary for independently running ESP32 devices.

Device time is uptime, never a Unix timestamp. Backend receipt time is stored
separately by the repository. No message accepted here requests an action.
"""
import ipaddress
import json
import math
import re

MAX_NATIVE_REPORT_BYTES = 64 * 1024
MAX_STATUS_BYTES = 16 * 1024
MAX_SAFE_INTEGER = 9007199254740991


def uptime(value):
    return type(value) is int and 0 <= value <= MAX_SAFE_INTEGER


def boot_id(value):
    return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value) is not None


def native_report(value):
    return isinstance(value, dict) and value.get("source") == "esp_native"


def validate_analysis(value, lines=None):
    if not isinstance(value, list) or len(value) > 64:
        raise ValueError("invalid device analysis")
    for item in value:
        if not isinstance(item, dict):
            raise ValueError("invalid device analysis item")
        if "line" in item and (type(item["line"]) is not int or item["line"] < 0
                               or (lines is not None and item["line"] >= lines)):
            raise ValueError("invalid analysis evidence line")
        if "score" in item and (type(item["score"]) not in {int, float}
                                or not math.isfinite(item["score"]) or not 0 <= item["score"] <= 1):
            raise ValueError("invalid device analysis score")
        if "accepted" in item and type(item["accepted"]) is not bool:
            raise ValueError("invalid device analysis acceptance")
        for key in ("model", "label"):
            if key in item and (not isinstance(item[key], str) or len(item[key]) > 128):
                raise ValueError("invalid device analysis label")
    return value


def validate_native_report(value):
    if value.get("clock") != "uptime" or not boot_id(value.get("boot")):
        raise ValueError("native reports require uptime clock and boot identity")
    if not uptime(value.get("sent_at_ms")):
        raise ValueError("invalid native report uptime")
    lines = value.get("lines")
    if not isinstance(lines, list) or len(lines) > 64:
        raise ValueError("invalid native report lines")
    for line in lines:
        if not isinstance(line, dict) or not uptime(line.get("at_ms")) or line["at_ms"] > value["sent_at_ms"]:
            raise ValueError("invalid native evidence uptime")
        if not isinstance(line.get("text"), str) or len(line["text"].encode()) > 4096:
            raise ValueError("invalid native evidence text")
    if "analysis" in value:
        validate_analysis(value["analysis"], len(lines))
    if len(json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()) > MAX_NATIVE_REPORT_BYTES:
        raise ValueError("oversized native report")
    return value


def validate_status(value):
    if not isinstance(value, dict) or type(value.get("schema_version")) is not int or value["schema_version"] != 1:
        raise ValueError("invalid device status schema")
    if value.get("source") != "esp_native" or not boot_id(value.get("boot")) or not uptime(value.get("uptime_ms")):
        raise ValueError("invalid device status identity or uptime")
    if "ip" in value:
        ipaddress.IPv4Address(value["ip"])
    if "ssh_state" in value and (not isinstance(value["ssh_state"], str) or len(value["ssh_state"]) > 64):
        raise ValueError("invalid SSH status")
    if "analysis" in value:
        validate_analysis([value["analysis"]] if isinstance(value["analysis"], dict) else value["analysis"])
    if len(json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()) > MAX_STATUS_BYTES:
        raise ValueError("oversized device status")
    return value


def availability(payload):
    # The native client uses a compact retained LWT. Older gateways use fields.
    if payload in {"ONLINE", "OFFLINE"}:
        return payload
    fields = dict(token.split("=", 1) for token in payload.split() if "=" in token)
    if fields.get("state") not in {"ONLINE", "OFFLINE"}:
        raise ValueError("invalid availability")
    return fields["state"]
