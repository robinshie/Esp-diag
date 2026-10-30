#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_DIR="$PROJECT_DIR/test/.runtime"
SSH_LOG="$RUNTIME_DIR/s5120v2_ssh_server.log"

if [[ -x "$PROJECT_DIR/.venv/bin/python" ]]; then
  PYTHON="$PROJECT_DIR/.venv/bin/python"
elif [[ -x "$HOME/.platformio/penv/bin/python" ]]; then
  PYTHON="$HOME/.platformio/penv/bin/python"
else
  PYTHON="python3"
fi

mkdir -p "$RUNTIME_DIR"
cd "$PROJECT_DIR"

if lsof -nP -iTCP:2222 -sTCP:LISTEN >/dev/null 2>&1; then
  echo "错误：本机 2222 端口已被占用，请先关闭旧的假 SSH 服务器。" >&2
  exit 1
fi

cleanup() {
  if [[ -n "${SSH_SERVER_PID:-}" ]] && kill -0 "$SSH_SERVER_PID" 2>/dev/null; then
    kill "$SSH_SERVER_PID" 2>/dev/null || true
    wait "$SSH_SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

docker compose -f mqtt/compose.yaml up -d

"$PYTHON" \
  productlines/h3c/switches/s5120v2-li/simulator/fake_ssh_server.py \
  >"$SSH_LOG" 2>&1 &
SSH_SERVER_PID=$!

for _ in {1..20}; do
  if lsof -nP -iTCP:2222 -sTCP:LISTEN >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$SSH_SERVER_PID" 2>/dev/null; then
    echo "错误：假 SSH 服务器启动失败。" >&2
    sed -n '1,120p' "$SSH_LOG" >&2
    exit 1
  fi
  sleep 0.1
done

if ! lsof -nP -iTCP:2222 -sTCP:LISTEN >/dev/null 2>&1; then
  echo "错误：等待假 SSH 服务器监听超时。" >&2
  exit 1
fi

echo "开始完整闭环：假交换机故障 -> USB -> ESP32 BDI -> SSH 恢复 -> MQTT"
"$PYTHON" test/s5120v2_bdi_e2e.py "$@"
