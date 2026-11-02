# 原生设备后台与 MQTT

正式运行链路是交换机 → ESP32 网口 SSH → 本机 BDI / 模型 → TF 日志。后台是可选的监视与归档接收端；停用 MQTT、后台或 Ollama 不影响设备采集、心跳、分析和本机处置。后台没有 SSH 凭据，不订阅处置指令，也不向交换机或设备发送操作。

## 目录边界

- `esp_backend/`：MQTT 持久收件箱、MongoDB 归档、监视 API、可选 AI 参考解释。
- `esp_protocol/`：无网络副作用的凭据环境读取、环境文件解析、协议尺寸及历史网关上报配置工具。
- `esp_gateway/`：历史 USB / 主机 SSH 实验与兼容入口；正式后台不导入此包。原有公共导入由兼容包装转至 `esp_protocol`。
- `config/native.json`、`deploy/compose.native.yaml`：正式设备监视配置。`config/demo.json` 和根 `compose.yaml` 保留原有演示用途。

## 启动可选后台

1. 复制 `deploy/native.env.example` 到 `deploy/.env.native`，权限设为 `600`，替换所有 `REPLACE_...` 密钥。该实际环境文件被 Git 忽略。
2. 将 `MQTT_BIND_ADDRESS` 和 `ESP_ARCHIVE_MQTT_HOST` 设置成**后台电脑真实的局域网 IP**。例子的 `192.168.10.10` 仅适用于电脑确实持有这个地址；ESP32 自身是 `192.168.10.1`，不能把它当 broker。`127.0.0.1` 只适用于同一电脑中的程序，真实 ESP32 无法访问电脑的回环地址。
3. 在仓库根目录启动依赖和 API：

```sh
docker compose --env-file deploy/.env.native -f deploy/compose.native.yaml up -d
.venv/bin/python -m esp_backend --config config/native.json --env-file deploy/.env.native
```

此配置显式绑定 broker 的 LAN 地址，MQTT TCP 为 `1883`，WebSocket 为 `9001`；MongoDB 和 API 仍只绑定电脑回环地址。设备仅需在长按 BOOT 的设置页开启 MQTT，填写同一 LAN 地址、设备发布账号及密码，前缀 `esp-switch`。已保存的 `esp-lab` 前缀仍可读取，但 broker、后台和浏览器须使用一致前缀。

已有 broker / MongoDB 时可直接使用它们，并修改环境变量和账号 ACL；不要再启动占用相同监听端口的容器。Linux 服务模板是 `deploy/esp-backend-native.service`，对应独立的 `esp-backend` 系统用户和 `/etc/esp-diag/native.json` / `native.env`；需确保该用户对本机收件箱目录有写权限。

`deploy/native-mosquitto-entrypoint.sh` 创建三个独立账号：设备只能发布 `report/status/availability`，后台与浏览器账号只能订阅这些主题。默认没有命令主题权限。正式配置不包含交换机用户名、密码或公钥，也没有模拟器和 USB 配置。

复制 `config/native-web.example.json` 到 `frontend/public/runtime-config.json`，替换实际设备 ID、浏览器只读账号密码和 API token。设备 ID 是 `esp-` 加 12 位 MAC 十六进制，可从 broker 的 `esp-switch/+/availability` 主题确认。开发前端的 `/api` 由 Vite 代理至 `127.0.0.1:8080`；部署静态站点时使用现有 `deploy/Caddyfile` 的同源 API / MQTT 反向代理，并为浏览器配置 `wss://` URL。不要把设备发布账号放进浏览器配置。

## 原生 MQTT 契约

主题为 `<prefix>/<device_id>/<channel>`，默认 prefix `esp-switch`。版本为 `schema_version: 1`。

| channel | 内容与语义 |
| --- | --- |
| `availability` | retained `ONLINE`；遗嘱为 QoS 1 retained `OFFLINE`。后台也兼容旧 `state=ONLINE` 格式。在线快照不代表交换机 SSH 成功。 |
| `status` | QoS 0 retained JSON：`source: "esp_native"`, `boot`, `uptime_ms`, `ip`, `ssh_state`, `heartbeat`, `bdi`, `analysis`, `mqtt`。可替换状态快照，不能当作完整日志归档。 |
| `report` | QoS 1、非 retained、完整 TF JSON：`source: "esp_native"`, `clock: "uptime"`, `boot`, `report_id`, `sent_at_ms`, `lines: [{at_ms,text}]`, `analysis: [...]`。兼容缺省的 `analysis`；不需要 USB 的 `observations/status`。 |

`status.analysis` 为本机模型结果对象（`model`, `label`, `score`, `accepted`），`report.analysis` 为本机结果数组，可包含端口、周期、来源等证据字段。后端按接收结果保存，不覆盖设备判定。

设备从**已提交 TF 文件**逐条发送报告，最多一条等待 PUBACK。只有匹配的 PUBACK 才让 TF 任务持久化游标；换卡/目标切换的 generation 不匹配时拒绝旧 ACK。断线、重启或游标损坏会重发原 `report_id`，后台按设备 ID 与 `report_id` 去重。后端 MQTT 在 SQLite 收件箱提交成功后 ACK；MongoDB 不可用时继续保留收件箱。

该保证限于设备 TF 的保留窗口：最多 256 个文件、12 MiB，空间不足时可能覆盖尚未上报的旧记录。`/storage` 的 `mqtt_gaps` 和本机 `[MQTT] state=archive_gap` 明确标记缺口。它不是无限保留，也没有跨断电保存尚未提交的 RAM 日志。状态快照不保证逐条送达。

设备推进游标的 PUBACK 来自 broker，表示 broker 已接收，不等于 MongoDB 已入库。首次建立后台订阅之前的报告、broker 丢失持久会话或其未刷盘数据，不能承诺自动补齐；设备 TF 仍可本机查看。后台持久订阅及 SQLite 收件箱覆盖订阅建立后的断线与数据库暂时不可用场景。

## 时间和监视 API

设备没有可靠墙钟时，`sent_at_ms` / `lines[].at_ms` 均是本次开机毫秒数；重启归零。后台保存原文，另设 epoch `archive_at_ms`，用于跨设备、跨启动的历史排序和日期筛选；旧网关报告继续使用其原有 epoch 时间。

- `GET /api/devices`：含 `latest_report`, `latest_report_at_ms`, `telemetry`, `telemetry_at_ms`, `telemetry_retained`。
- `GET /api/devices/{id}/telemetry`：含 `status` 原文、`received_at_ms`、`retained` 及可用性。retained 内容和接收时间本身不能证明实时在线。
- `GET /api/reports`：每条外层 envelope 含 `archive_at_ms` 和 `received_at`，内层 `report` 保留设备 uptime；`scope=live` 按当前 boot 分组。
- `GET /api/reports/{id}/context?agent=...`：只在同一设备、boot 与 clock 内关联原始证据。

以上接口除健康检查外均使用 `Authorization: Bearer <ESP_API_TOKEN>`。历史网关 reporting / SD-copy API 保留兼容；它们不会修改正式设备的本机采集周期。

## 可选 Ollama / 兼容 AI

默认 `ESP_AI_PROVIDER=disabled`，不会连接模型服务。设备模型和 BDI 不依赖此开关。需要后台额外参考解释时，设置：

```dotenv
ESP_AI_PROVIDER=ollama
ESP_AI_BASE_URL=http://127.0.0.1:11434
ESP_AI_MODEL=已安装的模型名
ESP_AI_TIMEOUT_SECONDS=15
```

Ollama 使用其 [非流式 Chat API](https://docs.ollama.com/api/chat)。兼容服务可设 `ESP_AI_PROVIDER=openai-compatible`，`ESP_AI_BASE_URL` 指向该服务的 API 根路径（例如 `http://127.0.0.1:8000/v1`），调用 `/chat/completions`；必要时设置 `ESP_AI_API_KEY`。接口格式参照 [Chat Completions API](https://developers.openai.com/api/reference/resources/chat)。

`GET /api/ai/status` 返回是否启用及模型名；`POST /api/reports/{id}/analyze?agent=...&line=0` 只解释数据库已保存的证据，省略 line 则解释有界报告文本。返回 `source: "backend_provider"`, `execution: "backend"`, `purpose: "advisory"`, `device_control: false`。provider 失败、禁用或繁忙返回 503，不降级为设备动作。已有 `/explain` 是后台小模型的只读复算，也标注 `execution: "backend"`。

请求限制为 12 KiB 脱敏证据、一次并发、1–60 秒超时和 64 KiB 响应，禁用 HTTP 重定向；不传递 SSH 凭据、不开放模型 tools、不执行生成文本。模型输出仍需结合原始证据阅读。

## 自动验证

```sh
.venv/bin/python -m unittest test.test_backend test.test_backend_native test.test_backend_provider test.test_backend_configuration test.test_reporting_config test.test_local_report
ESP_TEST_MONGO=1 .venv/bin/python -m unittest test.test_backend_native.NativeRepositoryTest -v
.venv/bin/python test/backend_native_integration.py
```

第一组包含持久收件箱、原生契约、AI 禁用/故障/限额、API 鉴权和旧网关兼容性。第二组需要本机 MongoDB，使用随机独立命名空间并只清理自己的数据；默认跳过，不可把跳过或连接失败计为通过。真实 broker、设备重启/断网和 TF 换卡仍属于硬件集成验收。

第三条命令需要运行中的 Docker 和已缓存的 `mongo:8.0`、`eclipse-mosquitto:2` 镜像。它创建随机回环端口、UUID 名称的临时容器，运行真实 MQTT / MongoDB / HTTP 故障与重启验收，完成后只删除自己的临时容器，结果写到 `test/.runtime/backend-native-integration.json`。这覆盖软件链路，不替代真实 ESP32 / TF 断电测试。
