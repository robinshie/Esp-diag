# 通过配置部署 SSH 真实环境

## 一次性安装

网关使用 Python 3.11+，连接 ESP32-S3 原生 USB。Broker 需提供 TCP/TLS 和
供浏览器订阅的 WebSocket。本机 MongoDB、管理界面与 MQTT 的统一 Compose 见
[Docker 依赖指南](docker.md)。

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
npm --prefix frontend ci
npm --prefix frontend run build
```

本版固件新增运行时配置，旧固件需更新一次；此后改变环境无需重新编译固件。
烧录端口也从配置读取：

```bash
ESP_CONFIG=config/local.json ./flash.sh
```

也可使用 `SERIAL_PORT=/dev/serial/by-id/... ./flash.sh`。运行网关时不要同时
打开其他串口监视器，控制台通过 MQTT 观看 ESP32 输出。

## 准备配置与凭据

```bash
cp config/ssh.example.json config/local.json
cp deploy/gateway.env.example deploy/gateway.env
chmod 600 deploy/gateway.env
```

编辑 JSON 与环境文件，替换以下内容：

- `agent.id`、`mqtt.client_id` 每块板/网关唯一；`web.agent_ids` 与之对应。
- `serial.port` 为实际 USB 路径；Linux 推荐 `/dev/serial/by-id/...`。
- `storage.state_db` 为可写持久路径，生产与演示分开，重启后保留。
- `mqtt` 为真实 Broker、写入账号、TLS 与证书；`web` 为实际 `wss` 地址和只读账号。
- `devices` 为 SSH 主机、认证、信任文件、轮询周期、采集命令与解析规则。
- `actions` 先保持 `enabled=false`，核对设备权限、白名单和命令后再逐项开启。
- `reporting.interval_ms` 控制正常合并上报周期（1000–300000，省略时默认 5000；当前演示为 60000）；
  `batch_ms` 控制状态变化/动作/恢复的短合并窗口（50–1000，默认 250）。

凭据写环境文件，不写 JSON 或源码。加载环境变量：

```bash
set -a
source deploy/gateway.env
set +a
```

准备 `config/known_hosts`：从设备管理渠道核验主机指纹后保存 host key。
`ssh-keyscan -p 22 <设备IP>` 可收集候选 key，使用 `ssh-keygen -lf` 核对，
不要把未核验的 key 当作可信主机。网关使用 RejectPolicy；非 22 端口的
known_hosts 主机名为 `[host]:port`。

## 检查、采集核验、运行

```bash
.venv/bin/python -m esp_gateway check --config config/local.json
.venv/bin/python -m esp_gateway probe --config config/local.json
.venv/bin/python -m esp_gateway export-web --config config/local.json --output frontend/dist/runtime-config.json
.venv/bin/python -u -m esp_gateway run --config config/local.json
```

`check` 不访问设备。`probe` 只执行配置的会话初始化与采集命令，不调用
`actions.commands`；先确认采集命令本身只读。输出必须明确匹配健康或故障之一。

示例检查指定接口：`ADM` 为管理关闭，`DOWN` 为物理异常，`UP` 为恢复。
软件版本导致列名/缩写不同，仅修改 `checks` 命令和正则。
账户锁定可添加 `event=AUTH_LOCKED` 检查，`component` 为目标账户，配置明确
的锁定/正常输出。使用独立管理账号采集；SSH 登录失败不能猜测为目标账户锁定。

Web 服务器提供 `frontend/dist`。以后更换 Broker、ESP32 ID 或只读账号，
重新导出运行时 JSON 即可，不需要重编译 JavaScript。本地开发输出到
`frontend/public/runtime-config.json`。生产域名缺此文件会报错，也拒绝加载
`mode=demo` 的导出配置。

导出文件包含浏览器只读凭据，浏览器用户可见，不能导出网关写账号或 SSH 凭据。
Broker 为浏览器配置订阅权限，样例见 `deploy/mosquitto-acl.example`；TLS 终止与
WebSocket 反代见 `deploy/Caddyfile`。按现场要求配置 Web 访问认证及证书。

## 持续运行

设备档案与历史数据库使用 Docker MongoDB，后端单独运行；按
[数据库指南](database.md) 初始化凭据、启动容器和归档服务。`ESP_API_TOKEN`
在网关导出前端配置和后端之间保持一致，`web.api_url` 使用 `/api`。
MongoDB 的应用账号与 SSH、网关 MQTT 写账号分开。

Linux 模板为 `deploy/esp-gateway.service`。工程安装在 `/opt/esp-diag`，配置与
环境文件放 `/etc/esp-diag`，创建服务用户并授予串口和状态目录访问权限。
调整模板中的路径与串口用户组后，由部署人员在目标主机执行：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now esp-gateway
journalctl -u esp-gateway -f
```

配置改变后先 `check` 再重启网关；前端参数改变后重新导出 JSON 并刷新。

## 动作、时序与状态库

- 仅执行显式开启的白名单动作，不执行来自 MQTT 的控制命令。
- `protocol` 每次握手下发。时间窗口需覆盖 SSH 延迟和实际排队，校验器检查基本
  超时预算。排队到期的动作取消，执行中也检查截止时间；设备慢或验证失败时
  保留故障，不伪造恢复。
- 有动作进行中时固件拒绝改变时间参数。维护窗口等待动作结束或复位 ESP32；
  网关会持续重试串口和握手，启动时未插硬件不会因等待 30 秒退出。
- 失败熔断不会无限重试。首次开启自动动作或遇到结果不确定的旧故障，先核验
  实际设备；必要时在维护窗口复位 ESP32，持久账本仍防止重复执行已认领操作。
- 保留并备份 SQLite，不要为“重试”删除生产账本。演示/生产或不同 Agent 不混用状态库。

## 验收

```bash
.venv/bin/python -m unittest discover -s test -p 'test_*.py'
node --test frontend/test/*.test.js
.venv/bin/python -m pip install -r test/requirements-integration.txt
.venv/bin/python test/ssh_config_acceptance.py
```

SSH 集成验收使用本机假交换机、真实 SSH/MQTT 和同固件状态模型的 PTY，
覆盖配置、host key、参数下发、管理恢复、物理断线、采集失联和 ESP32 专属主题。
报告为 `test/.runtime/ssh-config-report.json`。真实设备还需用实际版本、账号权限、
TLS/ACL、命令样本和长时间运行验证，模拟测试不能替代这些验收。
