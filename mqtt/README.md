# MQTT Docker 依赖

MQTT 已统一到仓库根目录 `compose.yaml`，与 MongoDB、mongo-express 共同管理。

```bash
# 仓库根目录
./deploy/dependencies.sh up -d --wait
./deploy/dependencies.sh logs -f mosquitto
```

TCP：`127.0.0.1:18883`，WebSocket：`ws://127.0.0.1:9001/mqtt`。
密码和端口配置在私有 `deploy/mongodb.env`；Broker 拒绝匿名访问。

本目录 `compose.yaml` 是兼容入口，引用根目录，使用相同的 `esp-diag` 项目、
服务和持久卷，不再创建第二个 Broker；需先初始化环境文件。

完整配置、停止命令及验收见 [Docker 依赖指南](../docs/docker.md)。
