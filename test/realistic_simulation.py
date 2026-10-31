"""Compatibility import for existing test entry points."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from esp_gateway.realistic_simulation import *
