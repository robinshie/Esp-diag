# ESP 设备网页接口

设备设置和日志分析共用同源 HTTP，无后台调用。长按 BOOT 3 秒开放配置窗口；连按三下开放日志窗口。配置窗口关闭时，不允许修改网络、SSH、主机公钥或 MQTT 参数。

| 接口 | 使用 |
| --- | --- |
| `GET /network`、`POST /network` | 网口参数读取；网络、SSH、MQTT 一次保存，`settings_version=2`；空密码保留已存值 |
| `GET /switch` | URL 编码的连接参数与实际 SSH、心跳状态；`sw_host_key_mode=auto`、`sw_host_key_state=unpaired/trusted/mismatch/error`、`sw_fingerprint` 只读 |
| `POST /switch/host-key/reset` | 仅用户明确点击重新配对时提交 `confirm=repair`；失败不自动重发 |
| `POST /switch/command`、`GET /switch/command` | allowlist 只读命令及其 request_id 结果，不任意拼接 CLI |
| `GET /mqtt` | 可选 MQTT 连接参数；启用、密码和前缀随 `/network` 保存 |
| `GET /device/status` | 日志窗口只读：`ai_on_device`、`ssh_state`、`heartbeat`、`bdi`、`analysis`、`mqtt` |
| `GET /storage/files`、`GET /storage/read` | TF 列表、异步分片原文读取；保留字节位置、UTF-8 与包格式校验 |
| `GET /model/explain?log=…` | 在 ESP 上运行小模型；分类结果与证据关联解释分开呈现 |
| `POST /storage/close` | 关闭日志窗口，不改监测策略 |

`bdi` 包含 `state/reason/port/action/enabled`，`analysis` 包含 `model/label/score/accepted`，`mqtt` 包含 `state/enabled`。心跳兼容现有 URL 编码字符串和对象。状态接口失败时显示不可用，不沿用“正常”；已保存的日志仍可读取。当前状态与历史证据分开显示，命令成功或模型分类不代替恢复验证。

`shared/` 模块可嵌入离线设备页面，也可用于后台的历史解释。设备页不导入 `backend/`，不持有 Broker 或后台数据库凭据，不发送后台控制命令。

设备 HTML 与共享模块是源文件；`tools/embed_network_page.py` 输出 `src/generated/network_page.h`、`src/generated/archive_page.h`，这些头文件不手动修改。ESP 模型部署位于 `src/ai/`，设备页面通过同源推理接口使用它，不导入训练目录或后台 provider。
