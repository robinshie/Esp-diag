# 集群模拟与验收

所有测试入口及协议约定见 [项目 README](../README.md)。Python 使用 3.11+。

```bash
python3 -m pip install -r test/requirements.txt
python3 test/cluster_suite.py --sanitize
node --test frontend/test/*.test.js
```

`cluster_suite.py` 编译固件同源码 C++ 与 Embedded-BDI，通过虚拟时钟覆盖
P0/P1/P2，并执行网关持久化、PTY 重连、丢接收确认、假交换机部件隔离等测试。
默认不需要 ESP32、SSH 服务或 MQTT Broker。报告在 `.runtime/cluster-report.json`。

```bash
python3 test/cluster_demo.py --scenario rack-power
python3 test/cluster_demo.py --scenario uplink
python3 test/cluster_demo.py --scenario storage
python3 test/cluster_demo.py --scenario rolling
python3 test/cluster_demo.py --scenario cascade
```

场景产出 `.runtime/cluster-demo.json`，包含每步设备状态、服务最小成员数检查
及标为 `SUSPECTED` 的共同故障域。不会对关联组发起自动批量操作。

```bash
python3 -m pip install -r test/requirements-integration.txt
python3 test/mqtt_acceptance.py
```

MQTT 集成自行启动/重启 `127.0.0.1:18884` 的测试 Broker，用 PTY 和同源码
原生固件验证完整网关、补发及重启恢复。报告在 `.runtime/mqtt-integration-report.json`。

```bash
./flash.sh
python3 test/usb_cluster_acceptance.py
```

USB 实机测试检查设备隔离、部分恢复、ACK、容量与超长帧同步。它会占满
32 个设备槽位，下一轮测试前须复位固件。报告在 `.runtime/usb-cluster-report.json`。

```bash
./flash.sh
./test/full_bdi_test.sh
# 无 Docker 时（需 integration 依赖）
MQTT_BACKEND=local ./test/full_bdi_test.sh
```

该流程仅控制 `127.0.0.1:2222` 的假交换机，使用管理性关闭事件验证 SSH 恢复。
网线断开由单独场景验证，不会被 `undo shutdown` 虚假修复。

持续随机测试支持 `--devices`、`--seed`、`--fault-rate`、`--action-mode`、
`--topology` 及 `--state-db`。源故障、MQTT/USB 队列和动作账本持久化在状态库。
不同试验用不同库，避免把上一次故障或幂等结果带入新试验。无硬件的快速测试
与真实设备运行仍有区别；未执行的耐久与真实日志验收不应当作已通过。

## MongoDB

按 [数据库指南](../docs/database.md) 启动 Docker MongoDB 后，执行
`.venv/bin/python test/mongodb_acceptance.py`，验证真实数据库与 API 的鉴权、
自动注册、幂等、迟到报告、持久档案、TTL 和历史保留；测试记录使用独立命名空间。
再执行 `.venv/bin/python test/mongodb_live_history_acceptance.py`，验证当前连接计数、
重连与重启、移除设备后的历史、45 天前记录查询、取消旧 TTL、日期过滤和游标分页。
实时演示运行时，`test/mongodb_outage_acceptance.py` 会短暂停止再恢复 MongoDB，
验证真实 USB 上报继续运行、收件箱积压及恢复补写。结果在 `.runtime/mongodb-*.json`。

## Docker 依赖验收

`.venv/bin/python test/docker_dependencies_acceptance.py` 使用随机独立 Compose 项目与端口，
验证数据库、管理登录、MQTT TCP/WebSocket、认证与重启持久化；结束只清理该测试项目。
