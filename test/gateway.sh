#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_DIR="$PROJECT_DIR/test/.runtime"
PID_FILE="$RUNTIME_DIR/gateway.pid"
LOG_FILE="$RUNTIME_DIR/gateway.log"
SCRIPT="$PROJECT_DIR/test/h3c_server_sim.py"

if [[ -n "${PYTHON_BIN:-}" ]]; then
  PYTHON="$PYTHON_BIN"
elif [[ -x "$PROJECT_DIR/.venv/bin/python" ]]; then
  PYTHON="$PROJECT_DIR/.venv/bin/python"
elif [[ -x "$HOME/.platformio/penv/bin/python" ]]; then
  PYTHON="$HOME/.platformio/penv/bin/python"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON="$(command -v python3)"
else
  echo "错误：找不到 Python 3。请安装 Python 或设置 PYTHON_BIN。" >&2
  exit 1
fi

is_running() {
  [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null
}

case "${1:-status}" in
  start)
    mkdir -p "$RUNTIME_DIR"
    if is_running; then
      echo "测试已运行，PID=$(cat "$PID_FILE")"
      exit 0
    fi
    nohup "$PYTHON" -u "$SCRIPT" --skip-smoke --min-interval 1 --max-interval 3 \
      >"$LOG_FILE" 2>&1 &
    echo "$!" >"$PID_FILE"
    sleep 1
    if ! is_running; then
      echo "启动失败，请查看：$LOG_FILE" >&2
      exit 1
    fi
    echo "测试已启动，PID=$(cat "$PID_FILE")"
    echo "日志：$LOG_FILE"
    ;;
  stop)
    if ! is_running; then
      echo "测试未运行"
      exit 0
    fi
    pid="$(cat "$PID_FILE")"
    kill -INT "$pid"
    for _ in {1..20}; do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    echo "测试已停止"
    ;;
  status)
    if is_running; then
      echo "测试运行中，PID=$(cat "$PID_FILE")"
    else
      echo "测试未运行"
      exit 1
    fi
    ;;
  logs)
    mkdir -p "$RUNTIME_DIR"
    touch "$LOG_FILE"
    tail -f "$LOG_FILE"
    ;;
  *)
    echo "用法：$0 {start|stop|status|logs}" >&2
    exit 2
    ;;
esac
