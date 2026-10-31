"""Reporting interval for the one ESP agent owned by a gateway config file."""
import json
import os
from pathlib import Path
import tempfile
import threading

_lock = threading.RLock()


def validate_interval(value):
    if type(value) is not int or not 1000 <= value <= 300000:
        raise ValueError("reporting interval must be 1–300 seconds")
    return value


class ReportingConfig:
    def __init__(self, path):
        self.path = Path(path).resolve()

    def _read(self, identity=None):
        data = json.loads(self.path.read_text())
        if identity is not None and data["agent"]["id"] != identity:
            raise KeyError("this gateway does not manage that ESP")
        return data

    def read(self, identity=None):
        with _lock:
            data = self._read(identity)
            return {"agent_id": data["agent"]["id"],
                    "interval_ms": validate_interval(data.get("reporting", {}).get("interval_ms", 5000))}

    def save(self, identity, interval_ms):
        validate_interval(interval_ms)
        with _lock:
            data = self._read(identity)
            data.setdefault("reporting", {})["interval_ms"] = interval_ms
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode="w", dir=self.path.parent, delete=False) as file:
                    temporary = file.name
                    os.fchmod(file.fileno(), self.path.stat().st_mode & 0o777)
                    json.dump(data, file, ensure_ascii=False, indent=2)
                    file.write("\n")
                    file.flush()
                    os.fsync(file.fileno())
                os.replace(temporary, self.path)
            finally:
                if temporary and os.path.exists(temporary):
                    os.unlink(temporary)
            return {"agent_id": identity, "interval_ms": interval_ms, "saved": True}
