"""Load simple KEY=value environment files without evaluating shell commands."""
import os
import re
import shlex
from pathlib import Path


def load_environment(path, missing_ok=False):
    path = Path(path)
    if missing_ok and not path.exists():
        return
    for line in path.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key.strip()):
            raise ValueError("invalid environment file assignment")
        words = shlex.split(value, comments=True)
        if len(words) > 1:
            raise ValueError("environment values containing spaces must be quoted")
        os.environ.setdefault(key.strip(), words[0] if words else "")
