#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in esp_gateway.simulation."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from esp_gateway.simulation import *

if __name__ == "__main__":
    raise SystemExit(main())
