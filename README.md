# ESP32-S3 Embedded-BDI 华三日志监控 PoC

本项目验证以下链路：

```text
Python 华三日志模拟器
        │ USB CDC
        ▼
ESP32-S3 Embedded-BDI
        │ USB CDC
        ▼
Python USB-to-MQTT 网关
        │ MQTT
        ▼
Mosquitto / MQTTX
```

ESP32 负责解析服务器事件、维护 BDI 信念、选择计划并产生动作。Python
负责模拟日志、回复模拟解锁 ACK，并把输入、BDI 状态和动作转发到 MQTT。
当前方案不需要 Wi-Fi 天线或以太网线。

## 当前验证结论

这条技术路线已经完成端到端 PoC 验证：

1. USB `PING/PONG` 双向回环成功。
2. ESP32 能收到健康、温度、风扇、电源和账户锁定事件。
3. Embedded-BDI 能更新 `server_healthy`、`server_critical` 和
   `server_locked` 信念。
4. 严重事件能触发告警动作。
5. 锁定事件能触发带 `request_id` 的解锁意图。
6. Python 能返回模拟 ACK，ESP32 能将 `server_locked` 恢复为 `false`。
7. Python 能把原始日志、信念、动作、状态和解锁命令发布到 MQTT。
8. Mosquitto 和 MQTTX 能收到 `h3c/#` 下的实时消息。

已验证的代表性输出：

```text
[BDI][EVENT] belief=server_locked value=true
[BDI][CMD] UNLOCK request_id=1 target=SIMULATED_H3C_CONSOLE reason=SERVER_LOCKED
[BDI][EVENT] belief=server_locked value=false
[BDI][ACK] unlock request_id=1 result=SUCCESS
```

这证明“感知 → BDI 决策 → 动作请求 → 结果确认 → MQTT 上报”链路可行。
目前的告警、恢复和解锁动作仍是模拟动作，不代表已经具备操作真实服务器的
生产能力。

## 项目结构

```text
.
├── bdi/agentspeak.asl          # BDI 规则源文件
├── lib/EmbeddedBDI/            # Embedded-BDI 官方运行时
├── src/main.cpp                # USB 协议、日志解析和推理循环
├── src/bdi_configuration.h     # AgentSpeak 对应的 C++ 配置
├── src/bdi_functions.h         # BDI 动作函数
├── test/h3c_server_sim.py      # 随机日志模拟器和 USB-MQTT 网关
├── test/gateway.sh             # 测试进程管理脚本
├── frontend/                   # MQTT 实时设备管理控制台
├── mqtt/compose.yaml           # Mosquitto Docker 配置
├── flash.sh                    # 一键烧录
└── platformio.ini              # ESP32-S3 PlatformIO 配置
```

## 环境要求

- macOS
- ESP32-S3-ETH，通过原生 USB 连接
- Docker Desktop
- PlatformIO CLI：`pio`，也可通过 `PIO_BIN` 指定
- Python 3：`python3`，也可通过 `PYTHON_BIN` 指定
- MQTTX，可选但推荐

## 1. 手动烧录

### 一键烧录

```bash
git clone https://github.com/robinshie/Esp-diag.git
cd Esp-diag
./flash.sh
```

脚本会自动查找 `/dev/cu.usbmodem*`、编译、烧录并校验 Flash。

### 完全清理后烧录

```bash
cd Esp-diag

PLATFORMIO_CORE_DIR=.pio-core \
PLATFORMIO_SETTING_ENABLE_TELEMETRY=no \
pio run --target clean

./flash.sh
```

成功标志：

```text
Hash of data verified.
[SUCCESS]
烧录完成。
```

如果提示串口被占用，先停止 Python 网关或 PlatformIO Monitor：

```bash
./test/gateway.sh stop
```

## 2. 启动 MQTT Broker

```bash
cd Esp-diag/mqtt
docker compose up -d
docker compose ps
```

开发环境连接参数：

```text
Host: 127.0.0.1
Port: 1883
Username: esp32
Password: esp32-bdi-test
TLS: off
```

Broker 只绑定本机地址，ESP32 不直接连接 Broker；Python 是 USB-MQTT
网关。

查看 Broker 日志：

```bash
cd Esp-diag/mqtt
docker compose logs -f
```

## 3. 安装测试依赖

```bash
cd Esp-diag
python3 -m pip install -r test/requirements.txt
```

## 4. 前台功能测试

确保没有 PlatformIO Monitor 或其他程序占用串口，然后运行：

```bash
cd Esp-diag
python3 test/h3c_server_sim.py
```

脚本启动时先执行固定冒烟测试：

1. USB `PING/PONG`。
2. 健康心跳。
3. 温度严重告警。
4. 故障恢复。
5. 账户锁定。
6. BDI 解锁请求和模拟 ACK。

之后持续随机产生：

- 正常心跳
- 温度过高和恢复
- 风扇故障
- 电源故障
- 账户锁定

按 `Ctrl+C` 停止。限定测试时间的示例：

```bash
python3 test/h3c_server_sim.py \
  --duration 300 --min-interval 1 --max-interval 3
```

## 5. 后台测试管理

在用户自己的终端中运行：

```bash
cd Esp-diag

./test/gateway.sh start
./test/gateway.sh status
./test/gateway.sh logs
./test/gateway.sh stop
```

后台日志位于：

```text
test/.runtime/gateway.log
```

同一时间只能有一个程序打开 `/dev/cu.usbmodem101`。

## 6. MQTTX 验证

在 MQTTX 中创建连接：

```text
Name: ESP32 BDI
Protocol: mqtt://
Host: 127.0.0.1
Port: 1883
Username: esp32
Password: esp32-bdi-test
SSL/TLS: off
```

连接后订阅：

```text
h3c/#
```

也可以使用命令行客户端：

```bash
docker exec -it esp-mosquitto \
  mosquitto_sub -h localhost \
  -u esp32 -P esp32-bdi-test \
  -t 'h3c/#' -v
```

## 7. MQTT 主题

| 主题 | 方向 | 内容 |
| --- | --- | --- |
| `h3c/server/sim-01/log` | Python → MQTT | 仅故障原始日志，正常心跳不逐条上报 |
| `h3c/esp32-s3-eth/availability` | 网关 → MQTT | 保留的在线/离线状态，仅变化时上报 |
| `h3c/esp32-s3-eth/health` | 网关 → MQTT | 保留的 NORMAL/FAULT 状态，仅变化时上报 |
| `h3c/esp32-s3-eth/status` | ESP32/网关 → MQTT | 推理循环与 USB 状态 |
| `h3c/esp32-s3-eth/event` | ESP32/网关 → MQTT | BDI 信念变化 |
| `h3c/esp32-s3-eth/action` | ESP32/网关 → MQTT | BDI 动作与 ACK 结果 |
| `h3c/esp32-s3-eth/cmd/unlock` | ESP32/网关 → MQTT | 模拟解锁请求 |

### Web 设备控制台

Mosquitto 同时在本机 `9001` 端口提供 MQTT WebSocket。启动前端：

```bash
cd frontend
npm install
npm run dev
```

打开 <http://127.0.0.1:4173>。控制台会订阅 `h3c/#`，显示设备在线状态、
健康度、告警、账户锁定、BDI 信念和实时消息。设备名称、位置和监控启停配置
仅保存在当前浏览器中。`sim-01` 只是模拟日志来源，会归并到物理设备
`esp32-s3-eth`，不会计为第二台设备。详细说明见 `frontend/README.md`。

## 8. 当前 BDI 行为

当前信念：

```text
server_healthy
server_critical
server_locked
```

当前 AgentSpeak 规则：

```agentspeak
!start.

+!start <- announce_ready.
+server_healthy <- report_healthy.
+server_critical <- report_critical.
+server_locked <- request_unlock.
```

当前动作实现：

- `announce_ready`：打印智能体就绪状态。
- `report_healthy`：打印健康确认。
- `report_critical`：打印严重告警决策。
- `request_unlock`：输出带唯一 `request_id` 的模拟解锁命令。

`RECOVERY` 会把 `server_critical` 设为 `false`、把
`server_healthy` 设为 `true`，但不会修复真实服务器。

MQTT 默认采用“状态与异常”模式：正常心跳仍通过 USB 送入 BDI，但不会逐条
发布到 MQTT。在线和健康状态使用 retained 消息，只在启动、离线、故障和恢复
时更新；原始故障日志、BDI 告警、解锁请求与结果仍实时上报。

## 9. 正式部署方案

当前版本适合 PoC 和联调。正式部署应按以下顺序推进。

### 9.1 固化真实输入协议

- 确认华三服务器实际输出来源：串口、COM、Redfish、SNMP、Syslog 或其他
  管理接口。
- 保存脱敏后的真实日志样本，建立解析测试集。
- 为每类事件定义唯一代码、严重级别、设备 ID、时间戳和恢复条件。
- 不要依赖自由文本包含判断作为最终生产解析器。

### 9.2 实现真实动作适配器

把 `src/bdi_functions.h` 中的打印动作替换为经过授权的动作适配器，例如：

- 发送告警到运维平台。
- 调用经过认证的 Redfish 管理操作。
- 通过受控 COM 协议执行允许的恢复命令。
- 创建人工审批任务，而不是直接执行高风险操作。

真实解锁必须具备：

- 设备白名单
- 操作白名单
- 身份认证与最小权限账号
- 请求签名、防重放 nonce 和超时
- 人工审批或双人授权
- `request_id` 幂等处理
- 完整审计日志
- 最大重试次数与熔断

不得使用本 PoC 绕过服务器认证或安全控制。

### 9.3 生产 MQTT

- 将测试账号和密码移出仓库，使用密钥管理系统注入。
- 开启 TLS，并验证 Broker 证书。
- 为日志、状态、动作和命令分别配置 ACL。
- 使用唯一设备证书或唯一账号，不要让多台设备共享凭据。
- 设置消息保留、QoS、过期时间和离线队列策略。
- 为 Broker 配置持久化、备份、资源限制和监控。

### 9.4 网关服务化

- 将 Python 网关部署为 `launchd`、systemd 或容器化服务。
- 配置自动重启、单实例锁和串口重连。
- 记录结构化 JSON 日志，并限制日志大小。
- 提供 `/health` 或等价健康检查。
- MQTT 断线时应本地缓存关键事件，恢复后按顺序重放。

### 9.5 固件可靠性

- 启用 ESP32 看门狗和异常复位计数。
- 对 USB 输入设置长度、格式和速率限制。
- 为事件队列、意图队列溢出增加可观测告警。
- 增加配置版本和固件版本上报。
- 保留已验证的上一版本固件和回滚步骤。

### 9.6 上线门槛

正式接入真实服务器前至少完成：

1. 24–72 小时连续随机压力测试，无崩溃、内存异常或串口死锁。
2. 拔插 USB、重启 Broker、重启网关后的自动恢复测试。
3. 重复消息、乱序消息、超长消息和错误 ACK 测试。
4. 真实日志回放测试，并核对误报和漏报。
5. 解锁动作在隔离测试环境完成授权、幂等、超时和审计验证。
6. 安全评审通过后，才允许从模拟 ACK 切换到真实动作。

## 10. 故障排查

### 串口被占用

```bash
lsof /dev/cu.usbmodem101
./test/gateway.sh stop
```

不要同时运行 Python 网关和 PlatformIO Monitor。

### 没有 ESP32 输出

按一下开发板 `RESET`，不要按 `BOOT`，然后重新运行测试脚本。

### MQTT 无消息

```bash
cd Esp-diag/mqtt
docker compose ps
docker compose logs --tail=100
```

确认 MQTTX 使用 `127.0.0.1:1883`、用户名 `esp32`，并订阅
`h3c/#`。

### MQTT 客户端反复断线

确保没有同时运行多个 `h3c_server_sim.py`。多个实例使用同一 client ID，
Broker 会让旧连接下线。

## 许可证

Embedded-BDI 上游代码使用 GPL-3.0。正式分发固件或衍生代码前，应确认并
履行相应开源许可证义务。
