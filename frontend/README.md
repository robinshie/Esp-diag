# 前端边界与设备页面

正式设备独立采集 H3C SSH 输出、执行 BDI、运行本机小模型并记录 TF 日志。前端呈现设备证据；可选后台只接收和归档，不替代现场处置。

| 目录 | 职责 |
| --- | --- |
| `device/network.html` | 长按 BOOT 3 秒进入的设置页：网口、SSH 自动主机公钥配对、只读查询、可选 MQTT |
| `device/archive.html` | 连按三下 BOOT 进入的本机日志分析页：ESP 模型、BDI、心跳、MQTT 状态与 TF 原文 |
| `device/journal.html` | 旧静态日志预览地址的转向入口 |
| `backend/` | 后台管理控制台的 Model / View / Controller / Service、MQTT 订阅和数据库接口 |
| `shared/` | 两类前端共用的纯日志解释、证据关联和视图模块；不含 MQTT、后台接口或控制动作 |
| `index.html`、`console.html`、`history.html` | 保持既有部署 URL 的后台入口 |
| `test/` | 设备网页、归档时间、MQTT 新鲜度、后台页面和共享解释测试 |

设备网页嵌入固件后使用 `/portal`（设置窗口）和 `/archive`（日志窗口），无 CDN、后台或互联网依赖。`tools/embed_network_page.py` 将 `device/` 与所需 `shared/` 模块合并到 `src/generated/network_page.h`、`src/generated/archive_page.h`，并检查最终 JavaScript 语法。Vite 的 `/device/*.html` 是源页面预览；完整设置接口在 ESP 配置窗口内提供。

SSH 首次连接自动保存主机公钥，每次重连校验。页面只读显示状态和指纹，无需填写密钥。换钥会停止登录；现场点击“重新配对当前交换机公钥”才清除旧配对并重新连接。保存网络参数不会提交或清空可信指纹。

MQTT 默认关闭，默认主题前缀 `esp-switch`，已保存的历史前缀保留。后台运行时配置须与设备前缀及设备 ID 一致；仪表盘只订阅 `report`、`status`、`availability`。原生报告保留 `clock: uptime` 与 `boot`；历史列表以外层 `archive_at_ms` 显示接收归档时间，原文显示“本次开机 +…秒”。retained 状态仅恢复内容，不能单凭重新收到消息判定在线。

接口说明见 [device/README.md](device/README.md)。设备页修改后执行：

```sh
python3 tools/embed_network_page.py
cd frontend
node --test test/*.test.js
npm run build
```

下面保留既有后台控制台及 USB 实验部署说明。USB 网关和模拟器仅用于历史实验，正式设备不依赖这些进程。

## 后台控制台与历史实验

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
“实时上报”在设备列表及详情中按 ESP 分别展示，不再显示所有设备的合计；计数来自
MongoDB 中该 ESP 当前 USB 连接与硬件 boot 的实际记录数，
刷新、后端重启不归零；重新建立 USB 连接或硬件重启另起实时计数。
本机网关管理的 ESP 详情提供上报间隔输入框（1–300 秒），当前演示默认 60 秒。
保存调用已认证的后端接口，原子更新该网关配置，网关约 2 秒内读取并应用；页面
其他设备的计数不受影响。故障、动作、恢复、首次状态及缓冲区满时可提前打包。
在线判断根据包内的实际间隔容忍两次周期，仍尊重原采样时间、断线通知和 MQTT 断开。
独立日志控制台同样同步当前连接最近 50 包，保留原时间；数据库恢复后自动重试加载。
`/history.html` 可按 ESP32 ID、日期查询所有连接的历史，支持分页和展开原文，
也包含已移除设备。默认查询最近 30 天，“全部历史”可查更早数据。
历史默认长期保留（`ESP_HISTORY_DAYS=0`），30 天是查询范围，不是清理期限。
未配置数据库 API 的独立模式仍使用 `localStorage`。详见 [数据库指南](../docs/database.md)。

设备编号可从列表编号或铅笔按钮编辑，支持 `ESP-001` 等自定义编号，并持久保存；连接 ID、
计数与历史仍绑定原设备。代码已按 Model / View / Controller / Service 拆分，入口 `app.js`
只负责启动，目录边界和扩展示例见 [设备管理 MVC](../docs/management-mvc.md)。

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
