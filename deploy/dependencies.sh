#!/bin/sh
# One Compose entry point; resolve paths from the repository, not the caller.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$task_root"
if [ -x .venv/bin/python ]; then
    .venv/bin/python deploy/init_mongodb_env.py
else
    python3 deploy/init_mongodb_env.py
fi
exec docker compose --env-file deploy/mongodb.env "$@"
