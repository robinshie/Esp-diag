# ESP-DIAG Web Console

本地设备管理控制台，通过 MQTT over WebSocket 订阅 `h3c/#`，实时显示：

- 设备在线/离线状态
- 健康、严重告警和账户锁定状态
- Embedded-BDI 信念、动作和解锁请求
- CPU、温度、运行时间等已上报字段
- MQTT 实时消息流与过滤
- 设备名称、位置、是否纳入监控等本地管理信息

当前 PoC 只有一块物理设备 `esp32-s3-eth`。`sim-01` 是 Python 生成的华三
服务器日志来源，控制台会把它的日志归并到这块 ESP32，不会另算一台设备。

设备管理信息保存在当前浏览器的 `localStorage`，不会发布回 MQTT。消息最多在
内存中保留 300 条，刷新页面后清空。

## 启动

先在仓库根目录启动支持 WebSocket 的 Broker 和 USB-MQTT 网关：

```bash
cd mqtt
docker compose up -d --force-recreate
cd ..
./test/gateway.sh start
```

再启动前端：

```bash
cd frontend
npm install
npm run dev
```

浏览器打开 <http://127.0.0.1:4173>。默认连接参数：

```text
WebSocket: ws://127.0.0.1:9001/mqtt
Username:  esp32
Password:  esp32-bdi-test
Topic:     h3c/#
```

## 构建

```bash
cd frontend
npm run build
npm run preview
```

构建结果位于 `frontend/dist/`。

## 安全边界

当前配置仅用于本机 PoC，Docker 端口绑定 `127.0.0.1`。正式部署必须使用
`wss://`、独立只读账号、主题 ACL，并避免在浏览器代码中内置生产密码。
