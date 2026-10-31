# USB protocol 2

ASCII，空格分隔的 `key=value`，以 LF 结束；输入最多 511 字节（不含 LF）。
字段名不得重复，匹配为完整字段，不使用包含判断。设备 ID 不能含 `/`；
设备、部件、事件及 incident 标识最长 64 字符。协议用于本机可信适配器，
没有把物理 USB 当作经过认证的生产管理协议。

## 事件与恢复

```text
H3C LOG device=a kind=server rack=rack-1 epoch=1 seq=1 level=INFO event=HEARTBEAT status=OK component=system
H3C LOG device=a kind=server rack=rack-1 epoch=1 seq=2 level=CRITICAL event=FAN_FAILURE status=ALARM component=fan1 incident=1-2
H3C LOG device=a kind=server rack=rack-1 epoch=1 seq=3 level=INFO event=RECOVERY status=OK component=fan1 fault=FAN_FAILURE
```

`kind` 为 `server` 或 `switch`。`epoch` 是源会话的正整数，重启后必须增大；
`seq` 为该会话内递增正整数，可跨设备使用全局计数器，固件按设备检查顺序。
新会话不会清故障。报警等级为 `WARN` 或 `CRITICAL`，其他事件类型可通过
相同模型扩展；锁定使用 `event=AUTH_LOCKED status=LOCKED component=<account>`。

故障键为 `device + component + event`；只有对应 `fault` 的恢复可以启动
2 秒观察期。WARN 聚合为降级，CRITICAL 聚合为故障。每台设备独立计算
90 秒数据新鲜度，心跳不清故障。

每条合法 LOG 收到 `RECEIPT`：

```text
[BDI][RECEIPT] device=a epoch=1 seq=2 result=ACCEPTED
```

重发最后一条已处理事件返回 `DUPLICATE`，不重复动作；其他错误返回拒绝及
诊断。接收确认只证明状态机处理了输入，不代表恢复动作成功。超长帧不会被
截取后执行。网关保留未确认输入，确认丢失时重发同一序号。

重连时网关重新分配序号，使用 `replay=true age_ms=<源数据年龄>` 重建当前
快照，`incident` 保持原故障身份。过期快照保留故障但不恢复在线或执行动作。

## 动作

```text
[BDI][CMD] UNLOCK request_id=<boot>-1 device=a component=admin target=admin fault=AUTH_LOCKED incident=1-2 attempt=1
H3C ACK action=UNLOCK request_id=<boot>-1 device=a component=admin status=OK
```

请求 ID 带固件启动随机 ID。ACK 需匹配当前请求、设备、部件和动作，状态为
`OK` 或 `FAILED`。成功 ACK 进入 VERIFY，直到对应故障稳定恢复才 DONE；
ACK 不清故障。ACK 超时 3 秒，最多 3 次同 ID 尝试；验证超时 10 秒后失败。

接口恢复使用 `RECOVER_INTERFACE`，仅 `LINK_DOWN cause=ADMIN_DOWN` 可请求，
`component` 为实际端口。电源、温度、断线等故障只产生告警，不凭管理命令修复。

稳定恢复后输出：

```text
[BDI][RECOVERED] device=a component=fan1 fault=FAN_FAILURE incident=1-2
```

网关必须用 incident 关联此通知；普通状态快照不能提前删除尚未确认的恢复。
动作执行器以 `device + component + fault + incident` 持久化幂等结果，避免
固件重启生成新 request ID 后再次执行同一故障操作。

## 运行时参数

新版固件支持在 PING 握手后、源事件发送前配置时间参数：

```text
H3C CONFIG nonce=<PING nonce> stale_ms=90000 settle_ms=2000 ack_ms=3000 verify_ms=60000
H3C CONFIGURED nonce=<同 nonce> stale_ms=90000 settle_ms=2000 ack_ms=3000 verify_ms=60000
```

网关收到匹配 nonce 和全部参数的确认后才开放 USB 事件发送。
参数单位为毫秒：新鲜度 1,000–3,600,000，稳定窗口 100–60,000，ACK 100–60,000，
验证 1,000–600,000，验证窗口必须大于稳定窗口。有活动动作时拒绝改变参数；
相同参数可在重连时重申。设备重启后由网关重新下发，不依赖重新编译。
旧固件不识别 CONFIG 时网关握手超时退出，不静默使用错误的时间窗口。

## 运行状态上报

`[BDI][STATE]` 给出 device、kind、rack、availability、state、active_faults、
故障键列表及四个 BDI 信念。状态是设备故障聚合的结果；`UNKNOWN` 表示数据
过期或容量导致信息不完整，不能解释为正常。设备输出最多 8192 字节，可分片。
网关转发时附上源采样的 `seen_at_ms`，前端不会把 retained 消息到达时间当作
新的设备采样。`H3C PING nonce=...` 返回同 nonce 及 boot ID，用于重连握手。
