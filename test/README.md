# 正式固件与三层架构检查

正式路径为 ESP 真实 H3C SSH / BDI / TF / 小模型，`esp_backend` 是可选 MQTT 归档服务，`frontend/device` 与 `frontend/backend` 通过 `frontend/shared` 共用证据解释。`esp_gateway` 和 USB 帧测试属于历史实验；需要这些测试时显式选择 `esp32-s3-usb-lab`，不要将默认正式固件当作 USB 网关端点。默认构建只编译 `src/main.cpp`，USB 入口位于 `src/legacy/main.cpp`。

## 正式自动检查入口

```sh
python3 tools/sync_log_model.py --check
python3 tools/embed_network_page.py
node --test frontend/test/*.test.js
npm --prefix frontend run build
.venv/bin/python -m unittest test.test_h3c_bdi_policy -v
.venv/bin/python -m unittest test.test_loop_discovery test.test_strategy_composition -v
.venv/bin/python -m unittest discover -s test -p test_heartbeat_bdi.py
.venv/bin/python -m unittest discover -s test -p test_local_report.py
```

H3C 原生测试使用真实解析器、策略、恢复状态机与 Embedded-BDI 库，检查身份、同轮新鲜证据、管理口/聚合/STP 保护，以及先意图后动作、两轮隔离验证。测试故障样本不连接或写入真实交换机，不能被描述为物理环路修复成功。SSH 主机钥测试覆盖首次自动信任、保存失败和换钥阻止登录；页面测试覆盖三击日志、重新配对失败不重发、原生 uptime 和 `contained` 不等于恢复。

后端原生契约与 AI provider 测试在 `test/test_backend_native.py` 和 `test/test_backend_provider.py`；provider 默认 disabled，测试不能依赖真实外部模型。`node test/archive_viewer_browser.mjs` 用本地只读 SD/模型接口与已存报告核对精确固件 HTML、手机宽度、分页、空卡与模型结果展示；它属于页面集成，不是实机 HTTP 验收。

实机只读验收：三击 BOOT、连接 `ESP-Logs-…` 后运行 `.venv/bin/python test/standalone_http_acceptance.py`。它通过设备 Wi-Fi HTTP 检查真实 SSH/心跳、本机文件和 ESP 模型，不打开 USB、不修改设备配置。真实交换机 `shutdown` 和物理环路故障需要单独记录端口、事件、命令、两轮读回与现场状态；不可由模拟测试或成功 ACK 代替。

当连接 ESP 热点会断开 GPT 时，先在终端运行 `python3 tools/collect_device_logs.py`，再三击 BOOT 并连接热点。程序无需互联网或 Python 依赖，默认等待 10 分钟；连接后采样 30 秒状态，读取最近最多 40 包 TF 日志，再刷新列表补采下载期间新增的最多 10 包，保存到 `test/.runtime/esp-offline-diagnostics.json`。`--tail-reports 0–40` 控制补采上限；达到上限会提示并设置 tail_collection.limit_reached。补采只覆盖刷新时已落盘的快照，不承诺所有待写及后续事件已保存。终端提示采集完成后切回联网 Wi-Fi，GPT 可直接读取本地文件继续检查。失败会保留部分结果和错误，不判定验收成功。此入口只读，不改 Wi-Fi、SSH 或端口配置，也不调用 USB。采集程序测试：`python3 -m unittest test.test_offline_diagnostics -v`。

`native-bdi-20261102-r2` 修复了持续产生日志时 TF 列表依赖全局待写计数归零而一直 busy 的问题。列表返回原子改名后已落盘的不可变文件快照；刷新请求合并为至少一秒间隔，写任务先处理最多 32 条队列记录，避免每次 HTTP 轮询都单条刷盘。`test.test_local_journal` 在模拟慢 FAT 与持续产生日志、轮询、异步读取下运行真实 `LocalJournal` 任务，使用 ASan/UBSan；不只是检查页面替身。BDI 状态新增五项表各自的 valid/complete/cycle/fresh，解析拒绝会记录 `[BDI][COLLECT] ... state=parse_rejected`；这些字段不放宽动作所需证据。

`native-bdi-20261102-r3` 根据离线实机样本识别 `LPDT is not configured.` 和 `No link aggregation group exists.`。前者是有效的“检测器未配置”证据，不能等同于无环路；后者是有效空聚合表。六项同轮原始输出保存在 `test/fixtures/h3c-r6328p03/`，H3C 回归会通过完整 Runtime 回放，核对 observation_valid、monitoring、loop_detection_disabled 以及没有写动作。另将 PSRAM 日志队列扩为 384 个槽，索引使用 uint16_t，真实任务回归覆盖超过 256 条突发，防止索引回绕及截断。HTTP 验收必须获得五表同轮有效证据，不能仅凭 BDI cycle 大于零通过。

r5 策略/设备替换回归位于 `test.test_strategy_composition`，覆盖固定容量组合、替换设备查询顺序、独立动作门控和无旧轮证据复用。SSH worker 回归另以不同命令和 # 提示符的测试设备验证同一传输实现。开发契约见 `docs/firmware-strategies.md`。

采集器遇到 `expired` 时，会从同一不可变文件的当前 offset 最多重建三次读取任务，保留已校验分片，并继续检查大小、偏移和 UTF-8 完整性。固件当前只有一个异步读取槽，其他网页或采集进程可能替换已完成但尚未被轮询的任务；建议采集时关闭其他读取页面。持续冲突仍报部分结果，不伪装为完整采集。14 项采集器回归包含任务过期、有限重试与重试后位置异常拒绝。

本文件列测试用途和入口；本轮最终数量、构建与烧录结论由实际运行记录另行补充。下面保留原有集群、USB 和数据库实验说明。

---

# 历史集群模拟与验收

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

## 网口心跳 BDI

`.venv/bin/python -m unittest discover -s test -p test_heartbeat_bdi.py` 使用实际 Embedded-BDI 计划及 ASan/UBSan，验证首次 SSH 登录门控、三次失败告警、两次成功恢复、过期证据、目标切换和业务故障隔离。真实 ping、网线断开和 ICMP 策略仍需实机验证。

## 已登录 SSH 的只读命令

`c++ -std=c++11 test/ssh_commands.cpp -o /tmp/esp-ssh-commands && /tmp/esp-ssh-commands` 验证固定命令、提示符分片、终端控制码、输出截断及 URL 编码。

`switch_ssh.cpp` 按 [交换机连接指南](../docs/switch-connection.md) 编译，使用真实 libssh API 头及模拟网络，验证认证、指纹、重试、同会话命令、重复请求、忙状态、任意命令拒绝及超时后关闭会话。正式版实机查询使用设备配置页；USB 查询协议仅保留在显式开发构建中。

## 正式版无 USB 运行验收

默认构建独立工作并启用 ESP 离线 AI。连按三下 BOOT、连接 ESP-Logs 热点后，运行 `.venv/bin/python test/standalone_http_acceptance.py`，通过真实 Wi-Fi HTTP 检查 SSH、心跳、TF 原生记录、完整 UTF-8 JSON、只读窗口和板载推理。该验收不打开串口、不发送 USB 命令、不修改设备配置。

`.venv/bin/python -m unittest discover -s test -p test_local_report.py` 验证本机报告格式、容量、凭据脱敏、无效 UTF-8 修复、中文截断边界和单调时间，启用 ASan/UBSan。


USB Console 接口模拟测试：`.venv/bin/python -m unittest discover -s test -p test_console_session.py`，包含 16 个带 ASan/UBSan 的会话与 USB 代际场景。它不依赖真实驱动，不代表 PL2303 已在板上运行；接口与硬件限制见 [Console 通道说明](../docs/console-transport.md)。

同一 `test_console_session.py` 还验证 UART/TCP 共用会话的断线撤销和不重放，以及真实 `UartConsolePort`/`TcpConsolePort` 代码在硬件/Socket 桩上的边界。三种固件构建环境和 GPIO17/18、MAX3232 接线见通信模式文档；模拟测试不代表实机验收。

双通道路由测试包含在 `test_console_session.py`，源文件为 `failover_transport.cpp`；模拟主/备会话及执行中隔离命令，验证切换门槛、结果归属、不重放、Console 命令结束并空闲等待 5 秒后尝试 SSH 登录，登录通过后回切并关闭 Console，以及配置变更时对未结束命令的阻断。

双通道 Console 按需启动：路由测试还检查 UART 启停回调次数，确认 SSH 正常不启用备用口，掉线达到阈值且旧请求结束后才尝试登录，SSH 在切换前恢复会取消 Console 尝试。

自动回切还覆盖 Console 隔离命令执行中不抢占、结束后保留原结果且不重放、SSH 主机信任未通过时不回切、等待窗口中有新 Console 命令时重新计时、回切后再次故障可重新启用 Console，以及回切后的新命令确实由 SSH 执行。

顺序恢复还检查：SSH 暂停确认后才接收 Console 业务命令，SSH 重连尝试期间不接收新 Console 命令，连接失败/60 秒截止后保留 Console，以及错误凭据和公钥异常停止自动重复认证。

SSH worker 回归另覆盖 `pause()`→连接/终端资源释放→`stopped()`→`resume(profile)`→重新认证→再次暂停，确保装配层使用明确生命周期接口后仍等待实际停止确认。

## SSH 故障处理实验

`python3 -m unittest test.test_switch_fault_recovery -v` 在本机模拟持续广播风暴、端口隔离与确认修复后的恢复，并回放实机只读计数器样本。覆盖证据连续性、管理口/上联保护、存储失败、SSH 结果不明、重启与故障复发，启用 ASan/UBSan；不连接交换机，不执行实机写命令。结果保存到 `test/.runtime/switch-fault-recovery-experiment.json`。范围、默认禁用的风暴阈值与实机观察见 [说明](../docs/switch-fault-recovery.md)。

只读烧录环境 `esp32-s3-readonly-acceptance` 额外关闭站点 BDI 写授权。`python3 tools/verify_switch_release.py --observe 60` 可在无互联网的 ESP 热点上自动验证真实固件、SSH、心跳、六表采集和日志下载；支持临时连接并恢复原 Wi-Fi。结果保存为 `test/.runtime/r15-hardware-acceptance.json`，不完整下载不会判为整体成功。验收判据测试为 `python3 -m unittest test.test_switch_release_validation -v`，覆盖断连、重启、缺失只读保护、混轮/过期表、写命令、核心命令截断与受限日志缓冲区。
