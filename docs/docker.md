# Docker 依赖

根目录 `compose.yaml` 统一管理本项目的三个 Docker 依赖。前端、归档 API、USB 网关
继续在宿主机运行；当前 Mac 的 ESP32 原生 USB 串口由宿主机网关读取。

| 服务 | 宿主机地址 | 持久化 |
|---|---|---|
| Mosquitto MQTT TCP | `127.0.0.1:18883` | `esp-diag_mqtt_data` |
| Mosquitto WebSocket | `ws://127.0.0.1:9001/mqtt` | 同一个 Broker |
| MongoDB | `127.0.0.1:27017`，库 `esp_diag` | `esp-diag_mongo_data` |
| mongo-express 管理界面 | http://127.0.0.1:8081/db/esp_diag | 数据保存在 MongoDB |

三个容器都有健康检查与 `unless-stopped` 重启策略。所有端口默认只绑定本机回环地址。
MongoDB 与 MQTT 的数据卷在容器重启、重建后保留。Mosquitto 保存 retained 数据、
持久会话与离线队列；客户端仍需使用持久会话、QoS 等正确参数。

## 一条命令启动

Docker Desktop 引擎启动后，在仓库根目录执行：

```bash
./deploy/dependencies.sh up -d --wait
./deploy/dependencies.sh ps
```

脚本会先创建或补齐 `deploy/mongodb.env`，保留已有密码和 API token，然后执行根目录
Compose。首次生成的文件权限为 600，已忽略提交。原始命令等价于：

```bash
python3 deploy/init_mongodb_env.py
docker compose --env-file deploy/mongodb.env up -d --wait
```

管理界面登录信息见本机 `deploy/mongo-express-login.txt`。
MQTT 本机实验默认账号为 `esp32`，密码由 `ESP_DEMO_MQTT_PASSWORD` 配置；
Broker 拒绝匿名访问，只允许配置的 `MQTT_TOPIC_ROOT/#` 主题。
启动时根据环境生成密码文件，不再挂载仓库中的固定密码哈希。
该实验账号用于本机网关和页面；正式环境的独立账号、只读权限与 TLS 参考
`deploy/mosquitto-acl.example` 和 [部署指南](deployment.md)。

完整 USB 实验仍使用：

```bash
.venv/bin/python test/live_demo.py start
```

启动器会自动启动这三个 Docker 依赖，再启动宿主机前后端、日志订阅与真实 USB 网关，
不会再创建 Python 演示 Broker。升级已有实验时会先停止该启动器记录的旧 Broker，
客户端会自动重连 Docker MQTT。硬件稍后插入也会自动连接。
`live_demo.py stop` 仅停止宿主机服务，Docker 依赖继续运行。

## 配置

修改 `deploy/mongodb.env`，模板是 `deploy/mongodb.env.example`：

| 配置 | 默认值 / 用途 |
|---|---|
| `MQTT_PORT`、`MQTT_WS_PORT` | `18883`、`9001`，宿主机端口 |
| `MQTT_USERNAME` | `esp32`，本机实验 Broker 用户 |
| `ESP_DEMO_MQTT_PASSWORD` | 本机实验 MQTT 密码，与网关及前端导出一致 |
| `MQTT_TOPIC_ROOT` | `h3c`，允许访问的主题根 |
| `MONGO_PORT`、`MONGO_DATABASE` | `27017`、`esp_diag` |
| `MONGO_ROOT_*`、`MONGO_USERNAME`、`MONGO_PASSWORD` | MongoDB 初始化管理员 / 应用账号 |
| `MONGO_EXPRESS_PORT`、`MONGO_EXPRESS_*` | 管理界面端口与独立登录信息 |
| `ESP_HISTORY_DAYS` | `0`，上报历史长期保留 |

MQTT 的端口、账号、主题根要与 `config/demo.json` 一致；改 MQTT 密码后重新导出
前端配置并重启宿主机客户端。演示启动器会检查端口、账号与主题根是否一致。
修改 Compose 环境后重新执行 `up -d --wait` 重建受影响容器，单纯 `restart`
不会加载新的环境变量。修改 MongoDB 环境文件不会自动修改已有卷内用户的密码。

`mqtt/compose.yaml` 仅为旧入口兼容，引用根目录定义，使用相同项目和数据卷。
不再单独启动占用 1883/9001 的第二个 MQTT 项目。旧入口需先初始化环境文件，
并使用支持 Compose `include` 的 Docker Compose。

## 日志、停止与验收

```bash
./deploy/dependencies.sh logs -f mosquitto mongodb mongo-express
./deploy/dependencies.sh restart mosquitto
./deploy/dependencies.sh stop
# 再次启动会恢复原有数据
./deploy/dependencies.sh up -d --wait
```

需要移除容器时用 `down`，保留卷；`down -v` 会删除数据库和 Broker 数据卷，
不要对需要保留数据的实验执行。

依赖验收只创建独立随机项目和端口，不连接正在运行的业务数据卷：

```bash
.venv/bin/python test/docker_dependencies_acceptance.py
```

验证数据库账号、管理登录、MQTT TCP/WebSocket、密码拒绝和重启持久化，结束仅清理
本次验收创建的容器与卷。结果保存在 `test/.runtime/docker-dependencies-acceptance.json`。
