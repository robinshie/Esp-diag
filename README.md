# ESP32-S3 Embedded-BDI 监测网关

主链路：配置驱动 SSH 采集 → USB → ESP32 Embedded-BDI → 授权动作与实际状态验证 →
持久化网关 → MQTT → Web 控制台。前端只展示 ESP32 及其监测结果，不直连服务器。

真实环境入口：`python -m esp_gateway run --config config/local.json`。
复制 [SSH 配置模板](config/ssh.example.json)，配置串口、地址、账号、命令、解析规则、
MQTT 与前端参数即可切换 SSH 环境。生产包不依赖测试目录，真实模式不会启用模拟器。
先阅读 [项目架构](docs/architecture.md) 与 [配置部署指南](docs/deployment.md)。
设备档案与上报历史使用 Docker MongoDB，安装与启动见 [数据库指南](docs/database.md)。
本机数据库管理界面为 `http://127.0.0.1:8081/db/esp_diag`（mongo-express 容器），
登录信息由初始化脚本生成到私有文件 `deploy/mongo-express-login.txt`。
`check` 校验配置，`probe` 核验采集，`export-web` 导出前端运行时配置。
旧固件需更新一次；此后时间参数也从配置下发，无需因环境变化重新编译。

当前版本采用 **protocol 2**。升级时必须同时更新固件、网关及前端；旧版没有
设备身份的 `H3C LOG` / `ACK` 不再接受。服务器/交换机保留在 ESP32 的监测上下文中。

本轮实测结果见 [验收记录](docs/cluster-validation.md)，帧格式见
[USB 协议](docs/usb-protocol-v2.md)。

## 客户网口 IP 配置

新增[网口 / 蓝牙配置说明](docs/network-setup.md)：网线直连后打开设备配置页，
或在控制台点击“网口 / 蓝牙配置”通过蓝牙写入 IP、掩码、网关和 DNS。配置断电保留。
当前构建已按 Waveshare ESP32-S3-ETH 的板载 W5500 引脚启用网口，同时支持蓝牙和配置存储。
双击 BOOT 可单独打开 TF 记录只读网页（青灯每轮闪两下），连接 ESP-Logs 热点查看，无需重配网络；十分钟后自动关闭。配置入口默认关闭，按住 BOOT 三秒才开启；保存成功后绿灯亮三秒，随后关闭热点、蓝牙连接及 HTTP 配置服务。未保存时十分钟自动关闭。

网口 MQTT 已新增[独立实验构建](docs/ethernet-mqtt-experiment.md)，默认关闭。
本轮仅编译，未烧录、未连接 Broker；实验主题不进入生产后端归档。

## 已实现的覆盖

| 优先级 | 实现 | 验收入口 |
| --- | --- | --- |
| P0 | 每设备独立 BDI、按部件和故障记账、部分恢复、降级状态 | `test_cluster_scenarios.py` |
| P0 | 心跳不清故障；设备失联与网关在线分开 | 原生场景及前端测试 |
| P0 | 启动唯一请求 ID、ACK 匹配、幂等账本、有限重试、恢复验证 | 动作场景及 MQTT 集成 |
| P1 | 持续故障、级联、恢复观察期、抖动、重复与乱序消息 | 原生场景 |
| P1 | USB 接收确认与重发、串口重连、重启快照恢复 | PTY 与实机 USB 验收 |
| P1 | SQLite MQTT 队列、PUBACK 后出队、Broker 重启补发 | `mqtt_acceptance.py` |
| P2 | 机架供电、共享上联、共享存储、节点退出、滚动重启 | 集群场景与 `cluster_demo.py` |
| P2 | 拓扑依赖、业务最小健康成员数、疑似共同故障域 | `cluster_topology.py` |
| P2 | 32 设备 / 10,000 事件突发、显式容量溢出、128 设备主机模型 | 原生场景及实机容量验收 |

这覆盖了集群模拟的主要正确性与故障路径。HDM/Redfish/SNMP/Syslog
采集适配、超出已有 SSH 能力的动作及业务探针、生产 HA、24–72 小时硬件耐久测试
仍属于真实环境接入验收，不能由模拟通过代替。共享故障域只报告 `SUSPECTED`，
不会据此批量修复设备；业务仲裁是拓扑与设备状态的模型，不等于数据库自身的选举协议。

## 快速运行：无需开发板

使用 Python 3.11+ 和 C++11 编译器（默认 `clang++`）：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r test/requirements.txt
.venv/bin/python test/cluster_suite.py --sanitize
```

测试编译并运行 **与固件相同的 C++ 状态模型及 Embedded-BDI**，使用虚拟时钟
验证 90 秒失联、2 秒恢复观察和动作超时；不需要等实际时间，也不会连接服务器。
结果写入 `test/.runtime/cluster-report.json`。不支持 sanitizer 的编译环境可省略参数。

运行可重复的场景并导出每阶段设备、业务与关联结果：

```bash
.venv/bin/python test/cluster_demo.py --scenario rack-power
.venv/bin/python test/cluster_demo.py --scenario uplink
.venv/bin/python test/cluster_demo.py --scenario storage
.venv/bin/python test/cluster_demo.py --scenario rolling
.venv/bin/python test/cluster_demo.py --scenario cascade
```

默认拓扑为三台服务器、两个上联交换机、一个共享存储，数据库要求至少两台
健康成员。配置见 `test/scenarios/topology.json`；可使用 `--topology` 指定文件。
默认结果为 `test/.runtime/cluster-demo.json`。

## 真实 MQTT 集成验收

```bash
.venv/bin/python -m pip install -r test/requirements-integration.txt
.venv/bin/python test/mqtt_acceptance.py
```

该测试自行启动并重启只监听 `127.0.0.1:18884` 的临时 MQTT Broker，使用真实
pyserial PTY 字节流和原生 C++/BDI。覆盖补发、网关重开状态库、固件重启重建、
动作确认及部分恢复，结束后清理进程。可通过 `--mqtt-port` 修改测试端口。
报告写入 `test/.runtime/mqtt-integration-report.json`。这是网络集成测试，ESP32
由同源码原生进程代替；物理 USB 验收见下节。

Docker 依赖（MongoDB、数据库管理、MQTT）统一使用根目录 [compose.yaml](compose.yaml)，
一键启动与配置见 [Docker 依赖指南](docs/docker.md)。

## ESP32 与持续模拟

安装 PlatformIO，将 ESP32-S3 接入原生 USB：

```bash
./flash.sh
.venv/bin/python test/usb_cluster_acceptance.py
```

USB 验收要求刚复位的固件，测试独立设备、部分恢复、错误 ACK、重复输入、
32 台设备上限、超长帧重新同步，报告写入 `test/.runtime/usb-cluster-report.json`。
验收会占满本轮启动的设备槽位；之后复位或重新烧录，再运行网关。

启动 Mosquitto 与持续模拟：

```bash
./deploy/dependencies.sh up -d --wait
.venv/bin/python test/h3c_server_sim.py --mqtt-port 18883 --devices 3 --seed 42
```

默认每 20–60 秒对所有设备采样一次，每台每轮 5% 故障概率。故障会持续存在；
随机修复只针对一个部件。故障仍存在时，健康心跳不会把设备变绿。

```bash
# 快速故障测试，限定 60 秒
.venv/bin/python test/h3c_server_sim.py --mqtt-port 18883 --skip-smoke --duration 60 \
  --devices 8 --min-interval 1 --max-interval 3 --fault-rate 0.5 --seed 42

# 注入动作失败、丢失 ACK 或命令成功但未恢复
.venv/bin/python test/h3c_server_sim.py --mqtt-port 18883 --action-mode failed
.venv/bin/python test/h3c_server_sim.py --mqtt-port 18883 --action-mode timeout
.venv/bin/python test/h3c_server_sim.py --mqtt-port 18883 --action-mode partial

# 按拓扑生成设备，并计算业务健康及故障域关联
.venv/bin/python test/h3c_server_sim.py --topology test/scenarios/topology.json
```

`--action-mode success` 为默认值。账本按故障 incident 持久化执行结果，重试与
ESP 重启不会产生新的同故障操作；切换模式测试请使用不同的 `--state-db`。
`--topology` 提供后，设备取自拓扑，代替 `--devices`。相关性故障注入使用
`cluster_demo.py` 的确定性场景，持续网关模式产生独立随机故障。

后台管理沿用 `./test/gateway.sh start|status|logs|stop`；可用 `PYTHON_BIN` 指定
Python。网关使用文件锁防止同一状态库多实例；不同网关必须使用不同串口、
`--state-db`、`--gateway-id` 和 MQTT 主题部署边界。

USB 联调提供 `simulation.workload=all_faults`，配合
`action_mode=realistic` 密集轮换 12 个场景：账户锁定、过热、风扇故障、电源故障、
管理关闭端口、物理断链、环路、STP 保护、MAC 漂移、CPU 高负载、内存高占用、
登录失败。场景间隔约 12–18 秒，并错开发送恢复事件；每次采样保留心跳。
账户解锁和管理关闭端口通过模拟执行器演示延迟确认、重试及策略冷却；其他
恢复标记为模拟维护，环路场景不代表已实现真实交换机自动隔离。
虚拟日志经过真实 USB ESP，再由网关转发其输出至 MQTT 和页面。
运行 `.venv/bin/python test/live_demo.py restart-gateway` 可应用这一配置并保持
现有前端、后端和 MQTT 服务运行；将 `workload` 改为 `standard` 可恢复原采样方式。

当前 `config/demo.json` 使用 `workload=overnight` 长时间观察模式：每 8–12 秒
发送健康心跳，启动及每次 ESP 确认恢复后随机正常运行 300–900 秒，然后才注入
一个新故障；尚未确认恢复时不叠加新故障。CPU/内存、登录及链路瞬态的抽样权重
高于风扇、电源故障，这些权重仅用于实验，不是 H3C 现场故障统计。普通模拟维护
需要 20–60 秒，温度、风扇、电源需要 60–180 秒；账户解锁及管理关闭端口仍沿用
独立命令确认和恢复观察，约 85% 正常延迟、12% 丢首个回执、3% 失败后等待策略
到期或模拟维护。切换模式时已有告警正常走恢复确认，不清空历史或直接报绿。
`simulation.normal_min/normal_max` 控制每次恢复后的正常时长；`fault_rate` 不控制
这两个场景工作模式。运行不设自动结束时间，后端历史继续保存，TF 保持已有循环
记录策略。macOS 启动器会在过夜模式创建本项目的 `caffeinate -i` 进程，阻止空闲
休眠；屏幕可以熄灭，电脑需接电并保持开盖。`live_demo.py stop` 同时释放防休眠，
不改变系统永久设置。已经运行时可使用 `live_demo.py keep-awake` 补开。

## 可靠性及容量约定

- 固件默认最多 32 设备、每设备 32 个活跃故障、全局 128 个活跃故障。
  容量不足会输出明确告警；丢失故障信息的设备进入 `UNKNOWN/incomplete`，
  不会报告正常。此保护标志需修复源快照后重启重建，避免猜测被拒绝的故障。
- `NORMAL`：在线且没有活跃故障；`DEGRADED`：只有 WARN 故障；`FAULT`：有
  CRITICAL 故障；`UNKNOWN`：90 秒未收到设备数据，或状态不完整。
- 恢复必须包含 `device + component + fault`。收到恢复后观察 2 秒，期间再次
  报警取消恢复；离线及过期恢复信息不会清除故障。
- 动作状态为 `WAIT_ACK → VERIFY → DONE` 或 `FAILED`。ACK 等待 3 秒，同一
  请求最多尝试 3 次；成功 ACK 后等实际恢复，10 秒未验证则失败，不无限重试。
  失败熔断保持到该故障恢复；再次发生的新 incident 才可产生新请求。
- 固件启动生成随机 boot ID，请求 ID 带 boot 前缀。ACK 必须匹配设备、部件、
  动作及当前请求；旧、错误、重复、迟到 ACK 不清故障。
- 只有 `cause=ADMIN_DOWN` 的接口故障允许申请 `RECOVER_INTERFACE`；断线
  只告警。`demo` 使用模拟执行器；`ssh` 只运行配置中显式开启的白名单动作。
- USB 输入上限 511 字节（不含换行），4096 字节接收缓冲。网关逐条等待
  `RECEIPT`，丢确认时重发同一序号；被固件拒绝时停止并保留队列。设备输出
  支持分片重组，最大 8192 字节；超长帧整行丢弃并告警。
- SQLite 同时保存 MQTT 队列、USB 队列、源快照与动作账本。MQTT 以 QoS 1
  顺序补发，收到 PUBACK 后出队，可能重复投递。USB 重连按当前故障快照
  重建，不对断线期间已经结束的历史故障重新执行动作。
- 每个队列最多 50,000 条；溢出报错并停止接收，不静默丢消息。数据位于
  `test/.runtime/gateway.sqlite3`，删除它会丢失历史与幂等账本。
- 128 设备测试仅代表主机上的模型扩展验证，不代表 ESP32 的生产承载能力。

## MQTT 与控制台

Mosquitto 本机参数：`127.0.0.1:18883`，WebSocket `127.0.0.1:9001/mqtt`，
测试用户 `esp32`，密码 `esp32-bdi-test`。订阅 `h3c/#`。

| 主题 | 内容 |
| --- | --- |
| `h3c/esp32-s3-eth/report` | 单包 JSON：ESP32 运行状态、全部监测快照、本批 USB 原文；retained |
| `h3c/esp32-s3-eth/availability` | 网关/USB 在线状态，包含 MQTT Last Will |

配置入口只发布 ESP32 命名空间，主题根与 Agent ID 均可配置。旧测试脚本还可发布
`h3c/server/...`、`h3c/switch/...`、`h3c/service/...` 作为验收输出，前端不订阅。
当前演示的正常状态每 60 秒合为一包，故障/动作/恢复在 250 毫秒窗口内合并后及时发送。
设备列表与设备详情分别显示每台 ESP 当前连接的上报数，顶部不累加该计数。
在设备详情的“本设备上报间隔（秒）”中可设置 1–300 秒并保存，本机网关自动应用，
不需重启；也可以修改对应网关配置的 `reporting.interval_ms`（毫秒）。
页面每包新增一行，展开可查看 USB 原文；不会将同一输出再按 serial/状态/确认分成多行。
`reporting.interval_ms` 与 `reporting.batch_ms` 可配置，USB 确认和动作执行不等待打包。

```bash
cd frontend
npm install
npm run dev
```

打开 <http://127.0.0.1:4173>。控制台只展示 ESP32，监测对象作为其上报上下文；
不凭心跳原文或动作 ACK 清除告警。retained 消息保留源采样时间，
浏览器重连不会刷新旧数据的寿命。网关在线不代表服务器在线。
配置数据库后，首页默认展示当前 USB 连接的状态与入库计数，刷新不归零。
[历史查询](http://127.0.0.1:4173/history.html) 可按设备和日期查看过去连接的上报；
历史默认长期保存，30 天以前也可查，详见 [数据库指南](docs/database.md)。

```bash
node --test frontend/test/*.test.js
npm --prefix frontend run build
```

## 假交换机完整闭环

```bash
./flash.sh
./test/full_bdi_test.sh

# 无 Docker 时，先安装 integration 依赖，再使用仅回环监听的测试 Broker
MQTT_BACKEND=local ./test/full_bdi_test.sh

# 使用自行启动的 Broker
MQTT_BACKEND=external ./test/full_bdi_test.sh --mqtt-port 18883
```

流程注入固定端口的**管理性关闭**，通过 ESP32 决策、SSH `undo shutdown`、
接口状态核验和恢复观察期确认，再核验 MQTT。假交换机支持 24 个独立端口，
`simulate interface-down` 模拟断线，`simulate admin-down` 模拟管理关闭，
`simulate cable-up` 模拟插回网线；`undo shutdown` 不会修复断线。
只控制本机 `127.0.0.1:2222` 的假交换机。

## 代码导航与接入边界

设备管理已按 MVC 拆分：前端 `models/` 保存设备状态，`views/` 负责呈现，
`controllers/` 协调操作，`services/` 访问 API；后端分为参数 Model、路由 Controller 与
Repository。新增管理字段和功能的步骤见 [设备管理 MVC](docs/management-mvc.md)。
设备编号可在列表编号或铅笔按钮中设置，例如 `ESP-001`；编号独立于连接 ID，
同一项目内非空编号唯一，修改后保留原连接计数与历史。

- `src/cluster_model.h`：协议解析、设备/故障/请求状态机、时间与容量边界。
- `src/cluster_runtime.h`：每设备独立 Embedded-BDI 与动作上下文。
- `src/main.cpp`：有界 USB 帧处理及固件计时。
- `esp_gateway/transport.py`：共享 USB、MQTT、握手与可靠转发。
- `esp_gateway/store.py`：SQLite 队列、快照、动作账本和部署身份。
- `esp_gateway/runtime.py`、`ssh.py`：生产 SSH 采集、配置规则和动作执行。
- `esp_gateway/simulation.py`：隔离的演示源；`test/h3c_server_sim.py` 是兼容入口。
- `esp_gateway/topology.py`：依赖、故障域和业务模型。
- `productlines/`：按厂商、设备类型、系列隔离的产品资料、日志与模拟器。

本版本输入是本项目定义的归一化协议，不能直接把厂商原始日志接入固件。
SSH 接入已提供配置驱动采集/解析和授权动作；上线需用真实样本核验命令与规则。
其他采集协议需要相应适配器。MQTT 的证书、ACL 和 Broker HA 按现场部署。
Embedded-BDI 上游采用 GPL-3.0，分发时
遵守对应许可证。

交换机 SSH / Telnet 连接参数的设备配置说明见 [交换机管理连接配置](docs/switch-connection.md)（当前为参数保存，ESP 直连客户端尚未接入）。

TF 卡故障摘要、原始日志和处置过程的本地保存见 [TF 卡故障记录](docs/tf-fault-journal.md)。

TF 日志页的小规模训练模型与 ESP 本地推理实验见 [离线日志解释模型](experiments/tiny_log_explainer/README.md)，仅用于显示解释。
