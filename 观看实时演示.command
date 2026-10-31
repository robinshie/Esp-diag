#!/bin/bash
cd "$(dirname "$0")" || exit 1
if [[ -x .venv/bin/python ]]; then
  DEMO_PYTHON=.venv/bin/python
else
  DEMO_PYTHON="$HOME/.platformio/penv/bin/python"
fi
"$DEMO_PYTHON" deploy/init_mongodb_env.py || exit 1
"$DEMO_PYTHON" test/live_demo.py start || exit 1
echo
echo "ESP32 实时控制台：http://127.0.0.1:4173/console.html"
echo "设备仪表盘：http://127.0.0.1:4173"
echo "MQTT：127.0.0.1:18883；WebSocket：127.0.0.1:9001/mqtt"
echo "MongoDB：127.0.0.1:27017/esp_diag；后端：http://127.0.0.1:8080/api/health"
echo "数据库管理：http://127.0.0.1:8081；登录信息见 deploy/mongodb.env 中 MONGO_EXPRESS_*"
echo "下面持续显示真实 ESP32 与唯一模拟服务器 sim-01 的双向日志。"
echo "Ctrl+C 只退出日志；停止服务请运行：$DEMO_PYTHON test/live_demo.py stop"
echo
tail -n 30 -F test/.runtime/live-demo/gateway.log
