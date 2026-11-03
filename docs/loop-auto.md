# 自动定位并隔离环路端口

用户明确要求开启自动处置，并补充“不是只操作 2、3 口，定位到哪个问题口就操作哪个”。本次授权构建为 `esp32-s3-loop-auto`，固件版本 `native-bdi-20261103-r20-loop-auto`。之前仅限 2、3 口的草案未烧录。

## 工作方式与权限

与 r19 的一次性指定关闭不同，r20 运行真实的 `loop_discovery` 与 `loop_isolation` 策略。ESP32 持续通过固定 SSH 管理目标 `192.168.10.2:22` 采集状态；只有交换机明确报告尚未被处置的环路、当前端口 UP、同轮核心表完整新鲜且通过保护条件时，才提出并执行隔离。目标来自当前故障证据，不是预设的 2、3 口。MAC 漂移、CPU 高或日志分类结果只用于观察，不直接授权关闭。

授权范围为这台设备的全部业务物理端口：`GigabitEthernet1/0/2` 至 `GigabitEthernet1/0/48`，以及 `Ten-GigabitEthernet1/0/49` 至 `Ten-GigabitEthernet1/0/52`，共 51 个候选口。每次动作仍须检查 Access 类型、非聚合成员、非 STP 根/主端口、无 STP 保护或非转发状态。管理口 `GigabitEthernet1/0/1` 永久排除。Trunk/Hybrid 端口不在可执行范围内。交换机已经执行环路阻断/关闭时保持观察，不叠加关闭命令。此构建不装配风暴隔离策略，也不设置广播阈值。

自动动作经过现有持久化意图账本，先写入并核验记录，再由现有 SSH 执行器发送 `system-view`、实际目标接口、`shutdown`、`return`，随后两轮回读确认管理关闭。结果不明确或持久化失败时停止，不反复重发。关闭后保持隔离；恢复须由操作者确认修复后通过已有恢复入口申请，固件没有定时自动打开端口的流程。开启权限本身不会启用当前已关闭的端口，也不修改 VLAN、STP 或环路检测配置。

## 验证与运行状态

```sh
python3 -m unittest test.test_loop_auto test.test_switch_fault_recovery -v
/Users/robin/.platformio/penv/bin/pio run -e esp32-s3-loop-auto
```

专用测试使用真实 EmbeddedBDI、H3C 解析器、Runtime 和持久化账本，传输层为模拟设备。核对全部 51 个候选口，分别对 2、3、4、24、48、49、52 口注入明确环路证据，确认自动选中相应端口、生成对应 SSH 命令并两轮确认。另覆盖无环路时即使广播较高也不关闭、无定时恢复、越权端口、原有管理关闭、上联/聚合保护、不完整证据、存储失败，以及断线和重启后的含糊执行不重放。保留完整故障恢复及只读构建的回归验证。模拟通过不等于真实环路实验已经通过。

烧录后通过 `/device/status` 核对：

- `firmware_version` 为 `native-bdi-20261103-r20-loop-auto`。
- `acceptance_read_only=false`，且 SSH 已登录、主机密钥 trusted、管理会话 accepted。
- `bdi.enabled=true`、`topology_confirmed=true`，管理口为 1 口；有效观察应为 true。
- `bdi.strategy_ids` 为 `loop_discovery` 与 `loop_isolation`。
- `loop_automation.scope=all_business_access_ports`；范围字段为 GE 2–48、XGE 49–52；`requires_explicit_loop_evidence=true`、`automatic_restore=false`。
- 没有当前环路时 `action=none` 属于正常监测，不会为了演示而关闭端口。

构建、烧录和实机状态证据分别记录在 `test/.runtime/r20-loop-auto-build.log`、`test/.runtime/r20-loop-auto-upload.log` 与 `test/.runtime/r20-loop-auto-result.json`。Console 与电脑 Wi-Fi 均由用户保留控制。

2026-11-03：3 项原生测试全部通过，专用自动隔离测试覆盖 17 个场景，使用 ASan/UBSan。正式范围构建成功，耗时 61.67 秒；ESP32 烧录成功，耗时 36.32 秒，esptool 写入哈希校验通过。固件 SHA-256 为 `ccaa23726d341e650c1f58b1c1178fdf3a30b1be0391735ea4e676fbb0ce1c9d`。随后用户提供的实机状态已确认 r20 正在运行：`acceptance_read_only=false`、`bdi.enabled=true`、固定 SSH 已连接且密钥 trusted/会话 accepted、心跳 RTT 1ms 且无丢失。启动 ID 为 `32c64bb568cbf3c738f75a6907ce9e25`，剩余 heap 42424 字节。第 7 轮接口、Down 原因、环路、STP 和聚合五张核心表同周期完整新鲜，`observation_valid=true`、环路检测已启用；策略为探索与隔离，范围为全部业务 Access 口，管理口 1 口受保护。当前 monitoring/action=none，没有本次自动隔离事件。该快照流量表尚未填充，探索仍为第 6 轮，符合本轮采集尚未完成的情况；未据此宣称流量表有效或真实环路实验通过。
