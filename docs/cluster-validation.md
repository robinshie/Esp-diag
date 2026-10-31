# 集群模拟改造验收记录

日期：2026-10-31。测试环境：macOS、Python 3.11、Clang C++11、ESP32-S3 USB、
本机临时 aMQTT Broker。Docker Desktop 安装缺少可执行程序，因此 MQTT 联调使用
仅监听回环地址的替代测试 Broker；本次没有重新验证 Docker/Mosquitto 部署。

## 单包上报验收（后续更新）

- Python 回归 79 项、前端测试 19 项、生产构建和 SSH 集成 5 组全部通过。
- 物理 ESP32 实时 MQTT 订阅 12 秒，只收到 3 条 `h3c/esp32-s3-eth/report`，
  相邻间隔 5.011 秒、5.007 秒；每包分别包含 5、3、3 行实际 USB 输出。
  期间没有实时 serial/status/observation 重复主题。
- 浏览器确认一包一条可展开摘要，保留锁定、处理、实际恢复确认与解锁成功。
- 待合并数据重启恢复、队列满保留、突发分包、QoS 去重、retained 新鲜度与
  离线通知优先规则由回归覆盖。USB 协议与固件不因 MQTT 打包而改变。

## SSH 配置架构验收（前次）

| 验收 | 结果 | 证据 |
| --- | --- | --- |
| Python 回归、配置边界、部署身份、SSH 超时与动作重读/到期保护 | 71 项通过 | `python -m unittest discover -s test -p 'test_*.py'` |
| 前端状态、运行时配置、自定义主题、生产地址禁止 demo | 13 项通过 | `node --test frontend/test/*.test.js` |
| 配置驱动 SSH → PTY 同源码 BDI → SSH 实际状态查询 → MQTT | 5 组通过 | `test/.runtime/ssh-config-report.json` |
| 前端生产构建 | 通过 | `npm --prefix frontend run build` |
| 固件构建 | 通过，Flash 655302 B（19.6%）、静态 RAM 27040 B（8.3%） | PlatformIO |
| 物理 ESP32 新固件、USB 配置握手与实时展示 | 通过 | `live-demo/gateway.log` 的 `H3C CONFIGURED`；浏览器只显示一个在线 ESP32 |

本次 SSH 集成使用本机假交换机和真实 SSH/MQTT，ESP32 在此集成中由同源码
原生固件 PTY 代替；物理 ESP32 另行运行 USB 演示链路。动作验证包括只执行一次
`undo shutdown`、恢复后实际查询为 UP、物理断线不修复、采集失败后数据过期。
超时与排队期间状态变化另由回归测试覆盖。未连接尚未指定的真实 SSH 型号。

## 前轮集群模拟验收

| 验收 | 结果 | 证据 |
| --- | --- | --- |
| C++ 固件状态模型 + Embedded-BDI + Python 网关回归 | 49 项通过，0 失败/跳过 | `test/.runtime/cluster-report.json` |
| AddressSanitizer / UndefinedBehaviorSanitizer | 原生验收通过 | 同上，`sanitizers=true` |
| 前端状态与新鲜度测试 | 5 项通过 | `node --test frontend/test/*.test.js` |
| 前端生产构建 | 通过 | `npm --prefix frontend run build` |
| ESP32 固件构建及烧录 | 通过，Flash 约 19.5%，静态 RAM 约 8.3% | PlatformIO 输出 |
| 物理 USB 验收 | 7 组检查通过；32 台模拟设备时剩余堆约 293 KB | `test/.runtime/usb-cluster-report.json` |
| 实际 MQTT Broker 重启集成 | 6 组检查通过，使用 PTY 和同源码原生固件 | `test/.runtime/mqtt-integration-report.json` |
| SSH → 物理 USB → BDI → SSH 恢复 → MQTT | 完整闭环 PASS | `MQTT_BACKEND=external ./test/full_bdi_test.sh --mqtt-port 18883` |
| 新网关 + 物理 ESP32 + MQTT 持续运行与重开状态库 | 通过，最终 MQTT / USB 积压均为 0 | 临时 `hardware-gateway-validation.sqlite3` |
| 32 设备 / 10,000 事件突发 | 通过，未出现 BDI 队列静默溢出 | 原生场景 |
| 128 设备 | 主机模型通过；ESP 固件仍明确限制为 32 | 原生场景 |

物理 USB 检查包括：设备隔离、心跳不清故障、部分恢复、重复输入拒绝、ACK
关联与恢复验证、设备容量保护、超长帧重新同步。SSH 流程只连接本机假交换机。
临时产物按仓库惯例放在 `.runtime/`，不纳入版本控制；复现命令见主 README。

## 实测中发现并修复的问题

- ESP USB 默认 256 字节接收缓冲可能丢掉超长输入尾部：改为 4096 字节，
  超长帧丢弃到换行，并增加逐条 RECEIPT、重传及持久化 USB 队列。
- macOS `tcdrain/flush` 可无限阻塞：移除网关 flush，使用超时写与接收确认。
- 读循环可能长时间占用串口锁：缩短有界读、按可用字节读取，并在锁外让出执行。
- 网关重启后，固件状态未变化时可能不再输出快照：重放事件显式请求完整快照。
- 旧快照可能过早确认尚在队列中的恢复：只有匹配 incident 的稳定恢复通知才能
  删除持久化故障，普通快照无权删除。
- macOS Bash 3.2 的空数组在 `set -u` 下失败：完整闭环入口使用明确默认参数。

## 验收边界

已完成本轮 P0/P1/P2 模拟功能与可重复验收。真实厂商日志适配与样本回放、
真实管理 API 的授权动作、真实业务探针与选举行为、生产 MQTT TLS/ACL/HA、
物理断电/拔线以及 24–72 小时硬件耐久测试未执行。128 节点主机测试和短时
实机容量测试不能视为 ESP32 生产吞吐量或长期可靠性承诺。
