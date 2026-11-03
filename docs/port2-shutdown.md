# 通过 ESP32 关闭交换机 2 口

用户明确要求“现在让 2 口 down，用 ESP32”。`esp32-s3-port2-down` / r19 是此命令的一次性构建，普通故障 BDI 保持只读。它只允许对固定 SSH 目标 192.168.10.2:22 的 GigabitEthernet1/0/2 发送一次 `shutdown`，没有启用端口或恢复端口步骤，不操作 3 口。

执行前实际读取机型/版本、接口、Down 原因、环路、STP 和聚合状态，要求精确适配器身份、已接受的 SSH 会话、同一轮完整新鲜观察、管理口 1/0/1 UP，以及 2 口的访问口/非聚合/非上联等保护条件。2 口无须处于某个特定 VLAN，关闭动作不修改 VLAN 配置。

若 2 口已经管理关闭，消耗本次一次性授权并进行两轮读取确认，不重复发送写命令。否则先持久化 `h3c-actions/port2-down` 授权标记，再经现有 ESP SSH 执行器关闭 2 口；核对请求 ID、目标、动作及回执，随后用两轮独立完整观察同时确认 ADM 与 Administratively。错误/断线/含糊回执停止且不重发。固件不会自动重新打开 2 口，重启也不会重放已消耗的授权。

HTTP `/device/status` 的 `port_shutdown` 为结果：`state=passed`，reason 为 `port_2_shutdown_verified` 或 `port_2_already_administratively_down`，writes 分别为 1 或 0。`needs_review` 不能当作成功。普通 `bdi.enabled=false` 是预期状态，此操作来自明确的操作者关闭命令，不是虚构的故障判断。

本机检查：`python3 -m unittest test.test_port_shutdown test.test_port_exercise -v`。烧录环境为 `esp32-s3-port2-down`，实机记录保存在 `test/.runtime/r19-port2-down-result.json`。当前后续广播实验的 r18 构建与脚本只做了本地准备，没有烧录或发包。

2026-11-03 设备回读结果：用户提供的 r19 状态为 `port_shutdown.state=passed`、`reason=port_2_shutdown_verified`、`writes=1`，说明 ESP32 实际执行一次关闭并通过控制器两轮状态确认。SSH connected、主机密钥 trusted、会话 accepted，心跳 RTT 1ms/misses 0；随后第 6 轮普通只读观察六张表同周期完整新鲜。2 口保持管理关闭，没有自动恢复。该结果来自设备状态；本轮未取得独立 Console 输出或分步 TF 记录。单口关闭模式不使用 carrier_up 字段判断操作前的物理状态。
