# ESP 独立运行与可选后台架构

正式构建为 `esp32-s3-devkitc-1`。设备固件、软件后台和前端有独立边界：现场采集、BDI、动作验证、TF 日志与小模型都在 ESP 完成；后台只接收上报并提供历史与附加解释。USB 不参与正式运行链路。

```mermaid
flowchart LR
  SW[真实 H3C 交换机] <-->|W5500 / 已认证 SSH| SSH[单一 SSH 工作任务]
  SSH --> OBS[同轮真实采集 / 解析与有效性]
  OBS --> BDI[ESP Embedded-BDI]
  BDI --> LEDGER[持久化动作意图]
  LEDGER -->|保护门控后 shutdown| SSH
  SW <-->|独立 ICMP 心跳| HB[心跳 BDI]
  SSH --> LOG[本机 TF 日志 / esp-native]
  BDI --> LOG
  HB --> LOG
  LOG <-->|分类| AI[ESP 小模型]
  LOG --> WEB[设备配置 / 日志页面]
  WEB <-->|物理 BOOT 开窗| PHONE[手机或电脑]
  LOG -. 可选 MQTT .-> BROKER[Broker]
  BROKER --> BACK[esp_backend / 收件箱与 MongoDB]
  BACK --> FRONT[后台控制台]
  BACK -. 默认 disabled .-> PROVIDER[Ollama 等解释接口]
```

开发接口与策略/设备替换方式见 [固件策略开发结构](firmware-strategies.md)。

## 三层职责

| 层 / 路径 | 职责 |
| --- | --- |
| `src/main.cpp` | 仅组装正式固件组件并调度；状态机由主循环单线程推进，不包含 USB 运行时分支 |
| `src/config/` | 板级与模式设置、网络/SSH/MQTT 参数、统一设置校验 |
| `src/device/` | 独立 BOOT 采样、长按配置/三击日志、十分钟窗口、HTTP 与按需 BLE |
| `src/net/` | SSH 独占工作任务、命令白名单、ICMP、可选网口 MQTT 与发布游标 |
| `src/security/` | 主机公钥首次配对、按目标持久信任与换钥阻止登录 |
| `src/domain/` | 标准化证据、现场策略、语义动作与结果验证 |
| `src/bdi/` | 策略接口及心跳、探索、隔离三个实际 BDI 策略 |
| `src/application/` | 与品牌无关的采集调度、策略调用、动作闭环与持久账本 |
| `src/adapters/h3c/` | H3C 型号身份、命令、返回解析、终端视图和动作翻译 |
| `src/h3c/` | 历史头文件路径与类型别名兼容转发 |
| `src/storage/` | 原生报告、PSRAM 队列、独占 TF 任务和 NVS 文本持久化适配 |
| `src/common/` | 统一事件出口、脱敏与日志文本等公共辅助能力 |
| `src/ai/` | 本机推理、合格模型、设备分类服务；没有动作调用权限 |
| `src/generated/` | 从 `frontend/device/` 与 `shared/` 生成的页面头文件，不手动维护 |
| `src/legacy/` | 旧 USB 入口、归一化集群模型和 TF 数据库镜像，仅显式实验构建使用 |
| `esp_backend/` | 原生 MQTT 契约、持久收件箱、设备与历史 API、MongoDB、可选 AI provider |
| `frontend/device/` | 固件嵌入的设置与本机日志分析页面，无后台依赖 |
| `frontend/backend/` | 软件控制台，Model / View / Controller / Service 分工，只消费上报和 API |
| `frontend/shared/` | 两类前端共用的证据解释和呈现，不发送 SSH/MQTT 控制动作 |
| `esp_gateway/` | 历史 USB 主机网关测试兼容；不是正式固件的运行依赖 |

默认 PlatformIO `build_src_filter = +<main.cpp>` 只选正式入口；`esp32-s3-usb-lab` 改选 `+<legacy/main.cpp>`。旧 `esp32-s3-eth-mqtt-lab` 与 `esp32-s3-log-ai-lab` 保留为默认能力的兼容别名，正式功能不依赖它们。

部署用推理源码和权重为 `src/ai/inference.h`、`src/ai/model.h`；训练、语料及对照验证留在 `experiments/tiny_log_explainer/`。`tools/sync_log_model.py` 导出合格产物，`--check` 只检查一致性。正式宏 `ESP_LOG_AI_ENABLED` 默认 1。页面构建则由 `tools/embed_network_page.py` 生成 `src/generated/network_page.h`、`archive_page.h`；生成产物与原始模板同时校验，避免维护重复实现。

## 真实采集、决策与验证

自动轮询按固定顺序读取 `display version`、`display interface brief`、`display interface brief down`、`display loopback-detection`、`display stp brief`、`display link-aggregation verbose`、`display logbuffer reverse`、`display cpu-usage`、`display memory`、`display mac-address mac-move`。每项按实际完成时间标记 cycle 与目标版本，输出截断、未知格式、混轮、过期或切换目标均不能成为写操作的依据。手动查询复用同一串行 SSH 工作任务。

已确认支持的身份为现场 S5130S-52S-EI / R6328P03；其他身份保留只读。H3C BDI 和心跳 BDI 使用分离的信念与计划。真实环路处置要求 `loop_confirmed`、`action_allowed`、`management_ready` 同时满足，执行前再次核对证据。

业务口授权不能覆盖管理口 `GigabitEthernet1/0/1`、trunk/hybrid、聚合成员、STP ROOT/MAST、保护已激活或已限制转发的端口。策略只对明确且尚未隔离的环路形成四步命令：`system-view` → 指定接口 → `shutdown` → `return`。动作意图必须先持久化；持久化失败、会话边界不明或执行失败时不重放写命令。断电后保留认领记录，避免同一端口因重启再次盲目动作。

命令 ACK 与恢复是两类证据。至少两个不同的处置后完整采集周期同时确认 ADM 和管理关闭原因，才进入 `contained`。这只证明隔离，**不能声明物理环路已修复**。正式流程保留隔离等待现场修复，不自行 `undo shutdown`。策略模块的受控恢复能力要求原动作所有权、明确修复确认、当前保护门控与持续读回验证，不能用于恢复外部关闭或交换机保护的端口。

## BOOT、SSH 公钥与本机 AI

1. 上电读取 NVS。当前默认 ESP 为 `192.168.10.1/24`，目标 `192.168.10.2:22`；已保存参数优先。
2. 长按 BOOT 3 秒进入 `ESP-Config-…` 设置窗口，一次保存网口、SSH、MQTT；保存成功后三秒关闭，未保存十分钟关闭。
3. 首次 SSH 连接在发送密码前自动取得并保存主机公钥，之后重连校验。指纹只读显示，无需输入；换钥阻止登录。在可信现场长按开启配置页，明确点击重新配对才清当前目标旧公钥。接口本身不能远程打开物理配置窗口。
4. 连续短按 BOOT 三次进入 `ESP-Logs-…` 日志窗口。连接并访问 `http://192.168.4.1/archive`；热点会避开网口网段。只读窗口不提供配置写入。
5. 日志模型在 ESP 推理，页面结合原文与规则显示中文说明；未知、低置信度或阶段冲突时不采用模型结论。模型与后台 AI 都不能触发硬件动作。

SSH 首次登录后，心跳每五秒一次，连续三次失败告警、两次成功解除心跳告警，过期为 unknown。心跳成功不等于业务正常、SSH 会话有效或环路已修复。

## TF 原文、时间与可选 MQTT

新目录 `/esp-native` 只保存实际设备事件，包标注 `source=esp_native`、`transport=ETH`、`boot`、`clock=uptime`。原文时间保持 64 位单调运行时间，页面显示“本次开机 +…秒”；跨 boot 不混合关联。旧 `/esp-db-mirror` 和 `/esp-journal` 保留，不混入正式原生日志。

固定 PSRAM 队列 384 条，每条最多约 1 KiB；每 30 秒或读取时请求提交，单包最多 64 条、约 48 KiB。TF 独占任务临时写入后重命名，约 12 MiB、最多 256 包循环保留并预留约 1 MiB。断电可能丢失未提交记录；卡异常不阻塞采样，缺口明确报告。动作意图账本独立于日志展示；意图持久化不可用时，自动动作不能继续执行。

MQTT 默认关闭，启用后使用 `esp-switch/<设备ID>/status`、`report`、`availability`。已提交 TF 报告以 QoS 1 发送，只有匹配 PUBACK 才前进游标；断线后可补发仍在 TF 保留窗口内的报告，超出循环窗口的记录不能承诺找回。状态/Last Will 与报告分别处理，不重复复制 USB 数据。

后台以 [`config/native.json`](../config/native.json) 连接 Broker，无串口配置依赖。`esp_backend` 的持久收件箱解耦 MQTT 与 MongoDB；原生 report 不改写 uptime，数据库外层 `archive_at_ms` 表示接收归档时间。前端 retained 消息只恢复内容，不单凭重收消息刷新在线。缺后台或 Broker 不影响本机采集、分析和 BDI。

后台 AI provider 默认 `ESP_AI_PROVIDER=disabled`。可选 `ollama` 或 `openai-compatible`，另配模型和地址，只读取有界归档证据，解释结果带 provider 来源。它不是 ESP 模型的替代，也没有 SSH 执行器或 MQTT 控制通道。

## 改版前独立固件验收记录（2026-11-02）

默认独立固件已编译、烧录并通过写入哈希校验。41 项页面与证据关联测试通过；本机报告的容量、脱敏、UTF-8 和单调时间通过 ASan/UBSan 测试；实际 BDI 心跳计划测试通过，小模型 55 项 Python/C++ 对照与内存检查通过。用户已手动确认 BOOT/Wi-Fi 页面和连接恢复。电脑自动连接热点未能找到该 SSID，故本轮未完成自动 Wi-Fi HTTP 验收；不将手动确认扩大为已完成所有接口或存储耐久验收。

---

# 历史主机网关架构（开发与迁移参考）

```mermaid
flowchart LR
  C[部署 JSON + 环境变量] --> G[Python 网关]
  D[真实 SSH 设备] -->|配置的采集命令| S[SSH 采集与规则解析]
  S -->|归一化事件/实际恢复观测| G
  G -->|USB protocol 2| E[ESP32 Embedded-BDI]
  E -->|状态/信念/动作请求| G
  G -->|授权动作与查询验证| D
  G -->|ESP32 上报 / QoS 1| M[MQTT Broker]
  M -->|WebSocket / 连接状态| W[前端：仅展示 ESP32]
  M -->|只读订阅 / 本地持久收件箱| B[Python 归档与设备 API]
  B --> DB[(Docker MongoDB)]
  W <-->|同源 API / 设备档案与历史| B
```

ESP32 负责故障记忆、BDI 决策、有限重试、验证截止时间与恢复稳定窗口。
部署网关持续等待 USB：启动时未插硬件不会因等待 30 秒而退出，随后插入会自动握手。
运行中拔出后串口读取器继续重试，重新插入后恢复连接和快照同步。
SSH 登录、厂商命令、输出解析、凭据、TLS、持久化都在主机网关。
前端不访问 SSH 设备、不调用模拟器，只订阅配置的 ESP32 主题。
数据库服务首次发现同一主题根的新 ESP32 时自动注册，保存档案、最新快照与历史。
已移除的设备保留连接记录，同一次连接继续上报不重建档案；新 USB 连接或硬件启动
的实时状态会自动重新发现，也可手动添加。旧快照和后端重启不会恢复已移除设备。
数据库 API 启用时，前端每 5 秒通过 API 同步当前 USB 连接的档案、状态、计数和最近上报，
MQTT 不直接更新展示数据。实时计数按 USB 连接与硬件 boot 划分，来自 MongoDB，
刷新或后端重启不清零；独立历史页按设备和日期查询所有连接，历史默认长期保留。
未配置 API 的独立模式才直接展示 MQTT。MongoDB 断线不阻塞 BDI，
后端通过独立 SQLite 收件箱补写，细节见 [数据库指南](database.md)。
日志中的 `device=<target>` 是监测对象，不能解释为 ESP32 自身的健康状态。

## 模块边界

设备管理 MVC 的目录职责、设备编号语义与扩展方法见 [设备管理 MVC](management-mvc.md)。

| 模块 | 职责 |
|---|---|
| `src/` | 固件与可在主机编译的同一份状态模型/Embedded-BDI |
| `esp_gateway/config.py` | 配置校验、路径、凭据环境变量、前端配置导出 |
| `esp_gateway/transport.py` | USB 握手/确认/重连、MQTT、队列、ESP32 上报路由 |
| `esp_gateway/store.py` | SQLite 快照、双向队列、动作账本与部署身份绑定 |
| `esp_backend/models/`、`controllers/`、`repository.py` | 管理参数模型、设备/历史 HTTP 控制器、MongoDB 持久化 |
| `esp_gateway/reports.py` | 持久化打包上报、快照与 USB 原文合并、正常限频与故障及时上报 |
| `esp_gateway/ssh.py` | host key 验证、shell/exec 会话、有界输出与规则解析 |
| `esp_gateway/runtime.py` | 每设备 SSH 采集、动作授权、执行与真实状态验证 |
| `esp_gateway/simulation.py`、`realistic_simulation.py` | 仅 demo 模式使用的演示源与延迟动作 |
| `config/` | 环境配置示例；真实配置使用忽略提交的 `local.json` |
| `frontend/backend/models/`、`views/`、`controllers/`、`services/` | 设备状态、页面渲染、管理流程、HTTP 服务；`app.js` 仅启动 |
| `deploy/` | systemd、Web 反代、MQTT ACL、凭据文件模板 |
| `test/` | 验收；旧脚本保留兼容入口 |
| `productlines/` | 型号资料、原始样本和假设备测试工具 |

生产包不从 `test/` 导入代码。真实与模拟模式共享传输、持久化和协议，通过
配置选择采集适配器；真实模式禁止启用模拟器。

## 配置契约

| 配置 | 控制内容 |
|---|---|
| `mode` | `demo` 或 `ssh` |
| `agent` | ESP32 逻辑 ID、主题根、名称 |
| `serial` | USB 路径、波特率 |
| `storage` | 持久状态库，演示/生产及不同 Agent 不混用 |
| `protocol` | 新鲜度、ACK 等待、验证、稳定窗口，握手时下发固件 |
| `reporting` | 正常打包周期 `interval_ms`（1000–300000，省略默认 5000，当前演示 60000）与变化合并窗口 `batch_ms`（默认 250） |
| `mqtt` | 网关 Broker、账号、TLS、证书、客户端 ID |
| `web` | 浏览器 WebSocket、独立只读账号、ESP32 ID 列表 |
| `devices[].ssh` | 地址、认证、信任文件、超时、会话和提示符 |
| `devices[].checks` | 命令、告警/健康正则、部件、故障、级别与原因 |
| `devices[].actions` | 动作开关、目标白名单、命令模板；默认关闭 |

路径相对配置文件解析，凭据用 `*_env` 引用环境变量。缺配置、凭据、host key、
非法正则或不满足时间预算时，在访问设备前失败，不自动回退为模拟模式。

## 单包 MQTT 上报

配置入口使用 `<root>/<agent>/report`，不再分别发送 serial、status、observation、
event/action 副本。每包含 `schema_version=1`、唯一 `report_id`、`sent_at_ms`、
`boot`、最近一次实际 `status`、全部 `observations[]`、本批 `lines[{at_ms,text}]`。
USB 原文仅记录一次；网关自身诊断明确带 `[GATEWAY]` 前缀。

正常数据按可配置间隔合并发送，当前演示为每 60 秒；状态改变/动作/恢复在 250 毫秒窗口后及时发送；单包
最多 64 行，连续突发会提前分包。待打包内容落 SQLite，打包与加入 MQTT 队列原子
提交，Broker 断线继续保留。USB RECEIPT、ACK 和执行器不等待 MQTT 打包。

每包携带完整最新快照，原始采样时间不更新为打包时间。前端只订阅 report 和
availability，每包只显示一条；QoS 重投按 report ID 去重，retained 只重建状态。
availability / Last Will 保持独立，以便断线立即生效，不进入日志刷屏。

## 恢复真实性

1. SSH 检查必须全部读取并识别成功，才发送新心跳；未知/歧义输出不当成正常。
2. `ACK OK` 表示执行器接收了任务，不表示设备已恢复。
3. 执行器只允许明确开启且在白名单中的动作；执行前重读实际状态，
   已恢复则不修改，变为物理断线则取消管理性修复。
4. 动作后重新查询，实际状态恢复才发送恢复观测；ESP32 稳定验证后才输出成功。
5. 动作在执行前落盘；重试/重启只验证已认领操作，不重复修改设备。
   执行中崩溃导致结果不确定时保留故障供运维核验，不宣称严格 exactly-once。
6. 每设备采集与动作提交串行化，避免旧采样覆盖刚完成的恢复。

固件安全边界：最多 32 台监测对象，USB 输入 511 字节，输出最多 8192 字节。
配置不能把一块 ESP32 扩成无限容量。
配置校验限制总检查数不超过 128，并预留重放帧空间，超限需拆分到其他 Agent。

## 支持范围

当前“改配置即可接入”指 SSH 命令/文本输出设备，以及已有 `UNLOCK` /
`RECOVER_INTERFACE` 动作。IP、账号、端口、命令、正则、部件、MQTT 地址和
时间参数变化不需要修改源码或重新编译。设备软件版本不同先用 `probe` 核验输出。

Comware 示例命令参考：[H3C R612x 命令参考](https://www.h3c.com/en/Support/Resource_Center/EN/Home/Switches/00-Public/Reference_Guides/Command_References/H3C_S5130S-SI_LI_S5120V2-SI_LI_CR-R612x-6W102/01/201909/1227853_294551_0.htm)。
Redfish、SNMP、Syslog、HDM 专用认证、全新动作或业务高可用仲裁需要相应适配器
与真实样本验收，不属于此次 SSH 配置接入范围。


## 2026-11-02 本次改版验收

默认正式固件构建成功并已烧录到本次 ESP32-S3，写入后的哈希校验通过。
Flash 使用 2,760,083 / 3,342,336 字节（82.6%），静态 RAM 68,484 / 327,680 字节（20.9%）。
固件文件 SHA-256：`3409a20ee523fa973b0a3e509d378c05584d478c2e26b25d20f99207d738db2a`。

- `test/cluster_suite.py --sanitize`：151 项，148 项通过；3 项 MongoDB 检查在普通套件中按环境跳过，随后已由隔离的真实 MongoDB 集成全部通过。
- 前端 107 项测试、Vite 构建和精确嵌入页面的手机视口浏览器测试通过。浏览器集成使用受控只读接口，不作为实机证据。
- 本机模型 55 项 C++ / Python 一致性检查、ASan / UBSan 及模型导出一致性通过。
- `test/backend_native_integration.py`：真实 Mosquitto → SQLite 持久收件箱 → MongoDB、重复与重启、数据库中断恢复，以及 AI provider 错误 / 超时 / 恢复检查通过；临时服务已清理。
- 用户已确认新版三击 BOOT 后热点与日志页面可访问。自动 HTTP 验收所在 Mac 仍为 `192.168.31.23`，到 `192.168.4.1` 的路由经过 VPN；本轮 SSH / BDI 实机状态仍等待页面状态回读，不能把软件通过等同于现场隔离动作已验证。

本次未人为制造真实环路、未做无故端口关闭，也未完成断电、换卡或长期耐久验收。
现场管理口绑定仅对已确认的 `192.168.10.2:22 → GE1/0/1` 生效；切换 SSH 地址或端口会撤销写动作授权，NVS 保存撤销标记，采集和解释继续运行。

### 同日 r2 日志采集修复

离线采集确认旧固件 TF 状态为 ready，但列表持续 busy；60 秒内 written 从 1 增至 113，pending 一直未清空，dropped 增至 758。代码中列表要求 pendingRecords 为零，并且每次请求触发刷盘，持续 SSH 日志导致读者饥饿。r2 返回已落盘文件快照、批量消费队列，并将请求刷盘合并为至少一秒间隔。真实 LocalJournal 任务在慢 FAT 替身下验证持续日志期间列表、分页和异步读取；恢复原有 pending==0 条件会令该回归测试失败。

11 项相关自动测试通过，覆盖 LocalJournal、原生日志格式、H3C 策略及离线采集。固件 `native-bdi-20261102-r2` 已编译、烧录并校验写入哈希；Flash 2,762,711 / 3,342,336 字节，静态 RAM 68,484 字节，firmware.bin SHA-256 为 `9051a92ca386047127cd60fd51864dee5cd13f7b919e2cd42038a734e1e6d4e5`。USB 仅用于此次刷写。

上一启动的用户日志证明 SSH/H3C 命令与 ICMP 正常，但 BDI 状态在 55–57 轮持续 unknown。新一次采集的 boot 已变化，首次 SSH 为 connect_failed；不能沿用上一启动的在线结论。r2 为五项决策表增加 valid/complete/cycle/fresh，并将解析拒绝写入日志。修复版的真实 SSH/BDI 和 TF HTTP 读回仍须离线采集验收，未标记完成。

### 同日 r3 实机格式适配

r2 实机离线采集成功导出 40 包，其中 29 包来自当前 boot。整个状态采样期 SSH 为 connected、主机钥 trusted、ICMP healthy，RTT 为 1–4 ms、misses 为零。TF 列表 busy 问题已在实机读回中解决；采集期 dropped 从 29 增至 41，仍存在 SSH 输出突发溢出。

BDI 无效表已定位为 loops 与 aggregation：设备分别返回 `LPDT is not configured.` 与 `No link aggregation group exists.`。r3 精确支持这两种实机返回，前者表示检测器未配置，不能当作无环路；后者为有效空聚合表。六项同轮实机输出已固化到 `test/fixtures/h3c-r6328p03/`；通过真实 Runtime 回放验证五表有效、monitoring、loop_detection_disabled 和无写动作。PSRAM 队列由 96 扩至 384 槽，uint16_t 索引与超过 256 条突发均通过实际任务回归。

16 项相关测试（含 ASan/UBSan、SSH 验钥/认证/命令、原生任务及离线采集）通过。`native-bdi-20261102-r3` 编译、烧录和写入哈希校验通过，Flash 使用 2,763,259 字节，firmware.bin SHA-256 为 `8962c0187daabe4b9ef9e884a40e1aa6ad75833dcf8afa1e9ff7b61c2b902ac1`。r3 的实机运行读回仍待下一次离线采集；未启用交换机 LPDT，未制造物理环路，也未把软件回放当作真实自动隔离验收。

### r3 实机离线读回验收结果

后续 r3 实机采集成功保存 40 包，其中当前 boot `6861c08f3aaf38a5f4031bd91573b58f` 有 32 包。九次状态采样中 SSH 均 connected、主机钥 trusted，心跳 healthy，RTT 为 1–4 ms、misses 为零。完整同轮决策表已在第 5、6、7、9 轮状态中读回，五表 valid/complete/fresh 均为 true，BDI 为 monitoring，原有 unknown 已消除。首个采样正处于换轮、表 cycle 混合，observation_valid 暂为 false，后续完整轮有效；未将混轮视为可动作证据。

TF 三次采样均 ready，written 从 1249 增至 2608，pending 从 133 降至 4，dropped 始终为零；列表与日志读取通过。原始 logbuffer 超过 8192 字节时仍明确记录 truncated，不等于 TF 丢记录，也不使五项独立决策表失效。当前主机上保存的离线采集文件与自动断言结果分别为 `test/.runtime/esp-offline-diagnostics.json`、`test/.runtime/journal-r3-hardware-result.json`。

本次仅完成真实 SSH、心跳、TF 和 BDI 监测验收。交换机环路检测仍未配置（loop_detection_enabled=false）；当前采样及当前 boot 已读报告中无端口动作。未验证实际环路发生、隔离、恢复或长期耐久，不能据此宣称已完成自动解环。

### r4 多条件环路探索

新增独立 EmbeddedBDI 目标 explore_loop，在原九条只读采集之后读取 `display mac-address mac-move`。同一 MAC/VLAN/端口对的双向增量均至少为 3，端口为同 VLAN 的可用业务 access 口，且本轮 5 秒 CPU 至少 80% 或最近 45 秒发生 STP 表变化时，第一轮 watch、连续第二轮 suspected。累计历史只建立基线，断线、缺轮、失效证据、计数重置会重置进度；稳定的 STP 阻塞不算当前环路信号。直接 LPDT 或 loopback down cause 仍须链路原因一致才报告 confirmed。规则详情与限制见 [多条件环路探索](loop-discovery.md)。

新代理的动作仅 observe_only，没有 SSH 写回调，suspected 不授权原隔离代理。页面增加环路探索状态，`bdi.discovery` 同步进入设备状态、可选 MQTT 和离线采集摘要；TF 记录信念与判断过程。未自动启用 LPDT、MAC 通知或修改 STP。初版间接探索不覆盖管理/保护口、trunk、聚合及 STP 根/保护路径。

18 项 Python 原生/接口回归、108 项前端测试、固定 SSH 命令测试通过；新代理及完整 Runtime 在 ASan/UBSan 下验证两轮疑似、历史重放、计数重置、证据冲突、保护端口、SSH 重连和疑似结果无写动作。精确嵌入页面在真实 Chrome 390 像素视口下验证疑似端口对、无横向溢出与页面异常；接口为受控只读替身，不是实机故障证据。

`native-bdi-20261102-r4` 已编译并烧录，写入哈希校验通过。Flash 使用 2,818,739 / 3,342,336 字节，静态 RAM 69,292 / 327,680 字节；firmware.bin SHA-256 为 `d8f9f9de64768aad92c94ac237e9d93cccd5dcee51ab4be1eaff06b08c2fa887`。USB 仅用于烧录。发布记录位于 `test/.runtime/loop-discovery-r4-release.json`。当前离线采集文件仍为 r3；r4 的真实 MAC 空表返回、探索状态和物理环路发现尚待现场离线采集验收，不沿用 r3 结果宣称新版实机通过。

### r4 实机离线读回结果

后续离线采集取得 40 包，其中当前 r4 boot `5f64d2551d3abe0930bee36953b9266e` 有 20 包；其余 20 包属于上一 boot，未用于当前启动判断。九次状态均为 r4、SSH connected、主机钥 trusted、心跳 healthy，RTT 1–2 ms、misses=0。五项核心表在第 2、3、4、7 轮读回完整同轮有效数据；一次换轮采样混合第 7/6 轮，被正确标为 observation_valid=false，随后第 7 轮完整有效。

新增 `display mac-address mac-move` 已在实机返回 ok，原文包含表头及 `---  0 MAC address moving records found  ---`。九次状态的 mac_moves_valid、cpu_valid 均为 true，CPU 5 秒值为 4–36%。探索代理在完成轮次 2、3、4、6、7 为 monitoring / lpdt_disabled_no_current_multi_signal_evidence / observe_only，未报告疑似或确认环路；首轮日志记录建立基线。交换机 LPDT 仍未配置，空 MAC 历史及当前未触发疑似不能证明不存在物理环路。

TF 三次状态均 ready，written 从 620 增至 2035，dropped 始终为零。状态与已采集当前 boot 报告未出现端口写动作。原始 logbuffer 查询仍按容量报告 truncated；独立核心表与 MAC/CPU 查询完整。实机断言记录为 `test/.runtime/loop-discovery-r4-hardware-result.json`。同轮八项原始返回固化于 `test/fixtures/h3c-r6328p03-r4/`，供完整 Runtime 回放；这是正常网络下采集、解析和只读推理验收，物理环路发现、隔离和恢复仍未实测。

### r5 策略与设备适配重构

新增 `IBdiStrategy`、固定容量 `StrategySet` 和异步心跳输入接口 `IHeartbeatStrategy`。三个具体 BDI 策略统一位于 `src/bdi/strategies/`；标准化证据和公共动作门控在 `domain/`，品牌无关的调度与账本在 `application/`，设备命令和解析在 `adapters/h3c/`。`main.cpp` 显式选择适配器与策略。SSH 通过同一个不可变 `IDeviceAdapter` 获取命令、分页设置、提示符、错误和动作预期视图，具体命令仅由适配器构造；ActionPlan 只保留语义。旧 `h3c/` 包含路径转发到新实现，NVS 意图记录头与键不变。

策略提出动作后仍由公共执行器独立检查当前轮次、目标版本、明确环路证据、端口保护、持久化与后续读回。每轮开始清空证据，避免漏填时沿用上一轮；观察阶段的建议不进入执行。状态增加 device_profile 和 strategy_ids，现有 discovery 字段保持。完整开发契约与新增策略/设备的步骤见 [固件策略开发结构](firmware-strategies.md)。

20 项相关 Python 回归和 108 项前端测试通过；最后修改涉及的九项原生测试、两项策略契约及八项采集器测试另行通过。ASan/UBSan 覆盖策略替换、两项查询且身份最后采集的测试设备、不同 CLI/# 提示符的真实 SSH worker、错误策略动作被阻止、组合容量与重连、漏填数据及原 H3C 实机样本回放。正常状态 JSON 已通过真实序列化后 Python 解码检查。生产适配器仍只有已验证的 H3C；测试适配器不代表另一真实品牌已获得支持。历史 USB 实验入口仅做编译兼容检查，未用于正式运行。

`native-bdi-20261102-r5` 正式构建与烧录成功，写入哈希校验通过，USB 仅用于烧录。Flash 2,822,951 / 3,342,336 字节，静态 RAM 69,340 / 327,680 字节；firmware.bin SHA-256 为 `131e369aec873fea28d8179e14e86de7f7a729998714827c8b9ead0a0d443153`。发布记录为 `test/.runtime/strategy-r5-release.json`。构建与烧录后另行通过以下离线运行读回验收；物理环路与长期耐久未实测。

### r5 实机离线读回结果

采集完成且无采集错误，九次状态均为 r5，同一 boot `97966a2692b109fdca8c836db6cdf82b`；40 份 TF 报告均来自该 boot，报告时间覆盖上电后约 299–417 秒。状态明确加载设备适配器 `h3c-s5130s-r6328p03` 与 `loop_discovery`、`loop_isolation` 两个策略。SSH 持续 connected、主机钥 trusted，状态心跳 RTT 1–2 ms、misses=0；报告另有 25 条 healthy 心跳。

五项核心表在第 25、26、27、29、30 轮获得完整、有效、同轮且新鲜的数据。第 26 轮的一次中途采样仅有接口和 down 表，其余表为空且 cycle=0，observation_valid=false；完成采集后恢复有效，符合每轮清空证据的设计。九类独立查询返回 ok，共 78 次；logbuffer 另有九次明确 truncated，未作为完整日志读取通过。没有 SSH 错误或核心表解析拒绝。

探索策略完成轮次为 25、26、27、29，MAC/CPU 证据均有效，状态为 monitoring / observe_only，双向 MAC 迁移增量为零，连续疑似轮次为零。LPDT 仍返回 `LPDT is not configured.`；未发现当前多条件环路证据，不代表已证明不存在环路。状态中的动作均为 none，采集报告只有 bdi-read 查询与 PING_SWITCH，未见端口写动作。设备内分析模型 `tiny-log-v3-protocol` 有状态输出，但本次低分结果 accepted=false；这不构成 AI 准确率验收。

TF 三次采样均 ready，written 从 7165 增至 8443，dropped 始终为零，日志列表和 40 份文件采集完成；pending 为 16、41、128，表示采样时仍在持续写入，不能把本次列表快照解释为全部待写记录已经落盘。状态可用堆内存为 61,272–72,148 字节。16 项离线证据断言通过，记录于 `test/.runtime/strategy-r5-hardware-result.json`，源采集 SHA-256 为 `bbdbe1a4af15efd92a69464fce508411fcbfb469d9c8b3f864a1f8a320188df8`。本次通过范围是正常网络下 r5 采集、策略加载与只读推理；物理环路发现、隔离、恢复及长期运行仍未验收。

### r5 开启 LPDT 后的首次环路测试采集

用户确认在采集期间连接 GE1/0/2 与 GE1/0/3，之后已拔线。新采集 SHA-256 为 `c7eb8a593e915e25b3aa64a9f197c0f7eb8a28d832c3a68f50ce19c369d46b4e`，九次状态与 40 份报告均来自同一 r5 boot。第 80、81、82 轮完整状态确认 LPDT enabled，实际原文为 10 秒检测周期、No loop is detected，解析有效；已下载报告中的 2、3 号口均为 DOWN，没有 ESP 隔离命令或动作确认。最后两次状态变为 SSH disconnected、心跳 lost / misses=3，BDI waiting_ssh，证据清空；TF ready、dropped=0。

该结果不能判定真实环路检测或隔离成功，也不能仅凭 DOWN 推断端口被 ESP 关闭。报告仅覆盖 uptime 1,090,261–1,207,116 ms，采集器下载的是起始列表快照，结束时的状态可能晚于已下载日志。修复主机采集器：历史快照下载后刷新最新列表，按序号去重并有界补采；不改固件，不调用 USB。11 项采集回归通过，新增覆盖下载期间断线的末段日志、分页与数量上限、补采失败时保留证据且标记不完整。现场补采尚待执行，不能用测试替身代替缺失的实机故障记录。首次采集和判定分别保存在 `test/.runtime/loop-test-r5-capture.json` 与 `test/.runtime/loop-test-r5-result.json`。

### r5 补采确认交换机检测到真实环路，ESP 自动隔离未通过

后续采集取得 100 份历史报告和 7 份末段报告，补采未触及 20 份上限，无采集错误，全部属于同一 r5 boot，覆盖 uptime 1,190,048–1,635,321 ms。用户确认这次补采期间测试线一直接入；已要求拔掉测试线，保留 ESP 管理链路。本次采集保存在 `test/.runtime/loop-test-r5-followup-capture.json`，SHA-256 为 `b2ee38cfd708a3b3f5648902c8b2f6922c1b1fbd88361bfbd6321f68864156b0`。

请求 bdi-read-828 的历史 logbuffer 回读含真实 H3C 事件：设备未校时钟的 05:10:47，GE1/0/2、GE1/0/3 先 UP，随后均记录 LPDT_LOOPED 和 LPDT_VLAN_LOOPED / VLAN 10；05:11:35 两端口记录 LPDT_RECOVERED 与物理 DOWN。2013 年设备日期不作为实际日期。这确认交换机 LPDT 检测到了物理环路，不能再把成功查询中的 No loop is detected 或 DOWN 当作从未发生环路的证据。

ESP 的第一段心跳丢失始于 uptime 1,211,438 ms，1,222,768 ms SSH 断开；1,265,446 ms 心跳恢复，1,288,698 ms SSH 自动重连成功。重连后五表与探索状态恢复正常，历史 LPDT 事件未被重放为当前可执行故障。第二段在 1,560,297 ms 再次心跳超时，1,569,524 ms SSH 断开，之后连接超时；最后状态仍 waiting_ssh。物理网口持续 link=up，不能把该故障解释为物理断线，也没有证据确定具体丢包机制。

107 份报告没有 ESP 的隔离意图、ACK 或验证记录，未验证 shutdown 和自动隔离。当前 SSH 处置依赖可用管理通信；失联时缺少新鲜证据，公共门控不执行写操作。该时序与环路期间管理通信受影响相符，但不能仅凭时序断言广播风暴或热点下载是唯一原因。TF dropped 始终为零，主机末段补采已取得断线及重连事件。本次判定保存在 `test/.runtime/loop-test-r5-followup-result.json`；固件未修改，下一步先在拔掉测试线后确认管理通信恢复。

### r6 历史环路风暴风险标记

日志页和后台共享分析组件增加两个标记：“已确认环路 · 风暴风险待核对”和“疑似环路风暴”。只读规则识别精确 H3C LPDT 原文，并通过登录/退出事件与 ESP 的 SSH 重连、断线及 ICMP lost 关联同一时段；缺少可靠关联时不把普通失联、旧环路重放、其他设备或其他 boot 的记录拼成风暴。标记显示设备、VLAN、端口和可展开原文，明确属于已保存日志，不代表当前状态、自动隔离或物理修复；没有流量计数时不确认风暴。规则运行于离线页面，未改变 ESP 模型或 BDI 动作门控。详见 [环路风暴标记](loop-storm-marker.md)。

117 项前端测试通过，包含九项新增风险标记测试；真实 r5 样本精简为 7 包、53 行，回放得到一个 VLAN 10、GE1/0/2 与 GE1/0/3 的疑似风暴标记。精确固件 HTML 在 390 像素 Chrome 视口下通过标记、历史范围、无溢出与零页面异常检查；接口为只读替身，不等于新固件实机运行验收。前端构建亦通过。

`native-bdi-20261102-r6` 编译、烧录及写入哈希校验通过，USB 仅用于刷写。Flash 2,829,559 / 3,342,336 字节，静态 RAM 69,340 字节；firmware.bin SHA-256 为 `83ddbc90cd28699b0f719669306a5bb8fa8483c0fb4a127ca05513ed8169e390`。发布记录为 `test/.runtime/storm-r6-release.json`，手机回放截图为 `test/.runtime/loop-storm-mobile.png`。r6 的实机页面显示与运行状态仍待现场确认；没有新增自动隔离成功的结论。


### 2026-11-02：管理通道接口与 Type-C Console 预留

BDI/页面命令通过 `src/net/command_transport.h` 的 `ICommandTransport` 使用当前 SSH 实现；领域状态改为 `managementConnected`，处置信念为 `management_ready`。USB Console 预留接口、物理设备代际校验和会话引擎见 [console-transport.md](console-transport.md)。默认运行仍是 SSH；当前未接入 PL2303 硬件驱动，没有启用 USB Host 或烧录。本次连接判断明确区分普通网口 SSH、RJ45 Console 的 RS232 和原生 Type-C USB Host。

### 通信通道组合（2026-11-02）

`src/net/switch_communication.h` 选择 SSH、UART Console、TCP Console；配置在 `src/config/communication_settings.h`，独立 PlatformIO 环境见 `platformio.ini`。BDI 仅通过 `ICommandTransport` 提交命令，设备命令/解析仍归 `IDeviceAdapter`，UART/TCP 共用 `ConsoleSession`。物理链路生命周期在 `IManagedConsolePort`，具体实现 `console_ports.h`。USB 继续通过 `IUsbSerialHost` 注入，真实驱动未接入。详见 [通信模式](console-transport.md)。

双通道构建 `esp32-s3-ssh-console` 通过 `FailoverTransport` 保留 SSH 与 UART Console 两条物理路径；SSH 会话不可用持续 5 秒且旧请求结束后才启动 UART，Console 登录成功后切换；业务命令只走当前通道且不重放；切入 Console 后暂停 SSH，Console 命令结束且空闲 5 秒后才重新登录 SSH，主机公钥验证及登录通过后立即回切并关闭 Console。切换触发 Runtime 清空旧证据，Console 现场绑定独立，详见通信模式文档。
