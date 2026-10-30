#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
PIO_CORE_DIR="$PROJECT_DIR/.pio-core"

if [[ -n "${PIO_BIN:-}" ]]; then
  PIO="$PIO_BIN"
elif command -v pio >/dev/null 2>&1; then
  PIO="$(command -v pio)"
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
  PIO="$HOME/.platformio/penv/bin/pio"
else
  echo "错误：找不到 PlatformIO。请安装 pio 或设置 PIO_BIN。" >&2
  exit 1
fi

if [[ -n "${PYTHON_BIN:-}" ]]; then
  PYTHON="$PYTHON_BIN"
elif [[ -x "$PROJECT_DIR/.venv/bin/python" ]]; then
  PYTHON="$PROJECT_DIR/.venv/bin/python"
elif [[ -x "$HOME/.platformio/penv/bin/python" ]]; then
  PYTHON="$HOME/.platformio/penv/bin/python"
else
  PYTHON="python3"
fi

if [[ -e /dev/cu.usbmodem101 ]]; then
  PORT="/dev/cu.usbmodem101"
else
  PORT="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print -quit 2>/dev/null || true)"
fi

if [[ -z "$PORT" ]]; then
  echo "错误：未检测到 ESP32 USB 串口，请检查 USB 连接。" >&2
  exit 1
fi

echo "项目：$PROJECT_DIR"
echo "串口：$PORT"
echo "开始编译并烧录..."

cd "$PROJECT_DIR"
PLATFORMIO_CORE_DIR="$PIO_CORE_DIR" \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no \
"$PIO" run --target upload --upload-port "$PORT"

echo
echo "烧录完成。"
echo "运行 MQTT 测试：$PYTHON $PROJECT_DIR/test/h3c_server_sim.py"
