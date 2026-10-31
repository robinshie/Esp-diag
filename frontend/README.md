# ESP-DIAG Web Console

本地设备管理控制台，通过 MQTT over WebSocket 订阅 ESP32 的运行状态、监测结果及处理日志，实时显示：

- 设备在线/离线状态
- 健康、严重告警和账户锁定状态
- Embedded-BDI 信念、动作和解锁请求
- CPU、温度、运行时间等已上报字段
- MQTT 实时消息流与过滤
- 设备名称、位置、是否纳入监控等本地管理信息

设备列表只展示 ESP32。其健康状态来自固件运行状态；服务器等监测对象不会
生成独立设备行。固件的 `[BDI][STATE]` 随 `h3c/<agent>/report` 的监测快照
上报，汇总为 ESP32 的监测故障、账户锁定和 BDI 信念；动作日志保留目标 ID。
前端不订阅模拟服务器直接发布的日志。命令成功 ACK 不能清除监测故障，
必须等待固件观察到恢复并完成稳定性验证。

状态包括正常、降级、严重和未知。监测对象默认 90 秒没有新数据后变为离线/未知，
真实环境由 `protocol.stale_ms` 配置并交给 ESP32 判断；
ESP32 运行状态以 15 秒为窗口。retained 消息携带源采样时间，页面重连不会让
旧快照重新变新；MQTT 连接断开时也不继续显示在线。

正常状态每 5 秒合并上报，故障、动作和恢复在 250 毫秒窗口内打包。每包只显示
一条摘要，展开查看实际 USB 原文；不再同时订阅串口原文与语义副本。
MQTT QoS 1 重复投递按 report ID 去重，retained 包只恢复状态，不新增实时日志。

配置 `web.api_url` 后，设备档案写入 MongoDB，页面每 5 秒从 API 读取当前 USB 连接的最新状态和最近 50 包上报。
数据库是展示数据的来源；删除历史后，下次同步移除对应日志，实时 MQTT 不直接追加页面记录。
删除设备档案后，在线、锁定、上报统计及日志不再包括该设备；同次连接的上报不恢复档案。
重新插入或重启硬件、新建 USB 连接后自动发现，也可手动添加同一 ESP32 ID 恢复展示。
编辑失败会提示；不发送 MQTT 控制命令。未配置数据库时，会话实时列表最多保留 300 条。
“实时上报”和设备消息计数来自 MongoDB 当前 USB 连接与硬件 boot 的实际记录数，
刷新、后端重启不归零；重新建立 USB 连接或硬件重启另起实时计数。
独立日志控制台同样同步当前连接最近 50 包，保留原时间；数据库恢复后自动重试加载。
`/history.html` 可按 ESP32 ID、日期查询所有连接的历史，支持分页和展开原文，
也包含已移除设备。默认查询最近 30 天，“全部历史”可查更早数据。
历史默认长期保留（`ESP_HISTORY_DAYS=0`），30 天是查询范围，不是清理期限。
未配置数据库 API 的独立模式仍使用 `localStorage`。详见 [数据库指南](../docs/database.md)。

## 启动

真实环境先按 [部署指南](../docs/deployment.md) 导出 `runtime-config.json`，仪表盘
和日志控制台共同读取该文件。改 Broker、ESP32 ID、账号和主题根无需修改 JS，
前端只使用单独的只读账号；生产域名缺运行时配置会报错，不回退到演示地址。

单服务器实时演示可在仓库根目录双击 `观看实时演示.command`，或运行
`python test/live_demo.py start`（Python 需安装 `test/requirements-integration.txt`，且 PATH 中有 npm）。
该入口读取 `config/demo.json`，使用本地 Broker、真实 USB ESP32 和唯一模拟服务器
`sim-01`；串口与演示端口都从此配置读取。
打开 <http://127.0.0.1:4173/console.html> 观看 ESP32 原始 USB 输出和监测结果；
`python test/live_demo.py stop` 停止这些服务。
此演示的 TCP MQTT 端口是 `18883`，WebSocket 为 `9001`；MongoDB 使用 Docker，
首次运行按数据库指南初始化 `deploy/mongodb.env`。
实时演示使用 `realistic` 模式：命令返回延迟约 0.7–1.2 秒，恢复观测再等待
5.5–6.5 秒，固件额外稳定验证 2 秒；部分事件首次应答丢失，需要重试。
每第四次锁定按策略冷却处理：拒绝解锁尝试，30 秒到期后才产生恢复观测。
锁定不会被随机心跳或随机恢复清除。以上为演示时序，不代表特定 H3C 型号的默认策略。

先在仓库根目录启动支持 WebSocket 的 Broker 和 USB-MQTT 网关：

```bash
./deploy/dependencies.sh up -d --wait
.venv/bin/python test/live_demo.py start
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
Topic:     h3c/<配置的 ESP32 ID>/...
```

## 构建

```bash
cd frontend
node --test test/*.test.js
npm run build
npm run preview
```

构建结果位于 `frontend/dist/`。

## 安全边界

当前配置仅用于本机 PoC，Docker 端口绑定 `127.0.0.1`。正式部署必须使用
`wss://`、独立只读账号、主题 ACL，并避免在浏览器代码中内置生产密码。
