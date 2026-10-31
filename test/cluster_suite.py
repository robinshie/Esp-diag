#!/usr/bin/env python3
"""One-command hardware-free acceptance suite; writes a machine-readable report."""
import argparse
import json
import time
import unittest
from pathlib import Path
from native_support import build_native


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sanitize", action="store_true")
    args = p.parse_args()
    build_native(args.sanitize)
    suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent), "test_*.py")
    started = time.monotonic()
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {"tests": result.testsRun, "failures": len(result.failures), "errors": len(result.errors),
              "skipped": len(result.skipped), "duration_seconds": round(time.monotonic() - started, 3),
              "hardware": False, "native_firmware_model": True, "embedded_bdi": True,
              "sanitizers": args.sanitize,
              "details": [{"test": str(t), "trace": trace} for t, trace in result.failures + result.errors]}
    destination = Path(__file__).parent / ".runtime/cluster-report.json"
    destination.write_text(json.dumps(report, indent=2))
    print(f"Report: {destination}")
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
