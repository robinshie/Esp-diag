# ESP 本机多条件环路探索

`native-bdi-20261102-r4` 新增独立只读 BDI 目标 `explore_loop`。真实 SSH 每轮在原九条查询之后执行 `display mac-address mac-move`，与本轮接口、down cause、LPDT、STP、聚合和 CPU 结果一起推理。规则直接运行于 ESP32，不依赖后台或外部 AI。

## 多条件判断

先取得可信 SSH、匹配型号与版本，以及同轮、完整、新鲜的五项核心表。采集不完整、截断、命令失败、目标不匹配、未知格式或超过 45 秒时，报告 `unknown`，不会推断网络安全。

探索范围为已允许的业务 access 口。两个口均须 UP、PVID 与 MAC VLAN 相同，无 down cause，且不属于管理口、额外保护口或聚合口；STP 表中的角色不得为 ROOT/MAST，不得存在保护或非转发状态。这个初版不推测 trunk、跨 VLAN 或上联路径的环路。

| 条件 | 判断 |
| --- | --- |
| 首次取得有效 MAC 漂移表 | 建立基线，累计历史不计为当前异常 |
| 同一个 MAC、VLAN、端口对，两方向计数每轮均增加至少 3 | 双向持续漂移信号 |
| 上述信号，同时本轮 5 秒 CPU 使用率至少 80%，或该端口对在最近 45 秒出现 STP 表变化 | 多条件异常，第一轮 `watch` |
| 同一个 MAC、VLAN、端口对连续两轮满足多条件 | `suspected`，疑似环路 |
| 当前 LPDT 明确列出端口，且链路与 down cause 一致；或当前 DOWN 原因为 loopback | `confirmed`，交换机直接证据 |
| 当前无上述信号 | `monitoring`，仅表示当前证据不足以提出疑似 |

阈值 3、80%、两轮是本项目的初始策略，不是 H3C 推荐的通用判定标准。STP 变化通过相邻有效轮次的 instance/role/state/protection 比较取得；稳定的阻塞状态不算新变化。CPU 取本轮各有效 CPU 的 5 秒值最大值。MAC 比较键包含 MAC、VLAN 和方向，不能把不同终端或 VLAN 的移动拼成证据。

重复轮次不会增加计数。缺轮、间隔过长、SSH 断线、切换目标或配置、无效证据会重新建立基线；计数降低或交换机记录时间倒退也会重置。新出现的 MAC 记录先建立基线。记录时间仅用于检测倒退，不把交换机未校时的 2013 年日期当作 ESP 的当前时间。

`LPDT is not configured.` 仍然表示检测器未配置。探索无需自动启用 LPDT、MAC 通知或修改 STP；未知的 MAC 空表格式报告证据不可用，待实机原始输出确认后适配。无漂移记录不证明无环路，CPU 正常也不能排除环路。只有 MAC 漂移而无独立佐证时停留在 `watch`。

## BDI 与动作边界

实际使用 EmbeddedBDI 的 BeliefBase、Context、PlanBase、IntentionBase 和 Agent。八项信念为 evidence_ready、direct_loop、indirect_only、multi_signal、persistent、watch_needed、quiet、evidence_missing；五个计划分别发布 confirmed、suspected、watch、monitoring、unknown。

新代理唯一动作是 `observe_only`，没有 SSH 写命令回调。`suspected` 不输入原有隔离代理；原隔离代理继续要求明确 LPDT 证据、端口保护条件和持久意图，再执行 shutdown 和两轮读回。新增探索结果不构成自动关闭或恢复端口的授权依据，也不声称物理环路已经修复。

代码按职责分为 `src/adapters/h3c/discovery_observations.h`（MAC/CPU 解析）、`src/bdi/strategies/loop_discovery.h`（状态与真实 BDI 计划）、`src/application/switch_runtime.h`（采集与调度）。

## 查看与验收

三击 BOOT、连接 ESP-Logs 热点后，`/archive` 的“环路探索”显示状态、原因、端口对、VLAN 和连续轮数；`/device/status` 的 `bdi.discovery` 同时包含 CPU、双向增量和 evidence。TF 记录 `[BDI][DISCOVERY]` 与信念变化。若启用 MQTT，原状态上报中的完整 bdi 对象会带上 discovery，无需后台参与推理。长按 BOOT 配置页的只读查询也可单独执行 MAC 漂移查询。

离线采集入口不变：

```sh
python3 tools/collect_device_logs.py
```

程序开始等待后，三击 BOOT 并连接热点。采集完成切回有互联网的 Wi-Fi，读取 `test/.runtime/esp-offline-diagnostics.json`；摘要增加 loop_discovery。r4 后续实机采集已确认 SSH 与心跳正常、MAC 空表与 CPU 解析有效、探索状态 monitoring，TF dropped=0；证据见 `test/.runtime/loop-discovery-r4-hardware-result.json`。规则故障样本通过不等于实机已形成、发现或解除物理环路。

自动回归：

```sh
.venv/bin/python -m unittest test.test_loop_discovery test.test_h3c_bdi_policy -v
node --test frontend/test/*.test.js
```

原生回归链接真实 EmbeddedBDI，启用 ASan/UBSan；覆盖持续双向漂移、高 CPU/STP 佐证、计数重置、旧数据、管理/聚合/STP 等排除条件、重连基线、未知输出，以及完整 Runtime 中疑似结果无写操作。前端回归检查疑似与确认的展示区别、掉线后清除旧状态。

命令格式、累计计数含义以及“频繁 MAC 移动可能提示二层环路”的依据见 [H3C R63xx MAC address table commands](https://www.h3c.com/en/Support/Resource_Center/EN/Home/Public/00-Public/Technical_Documents/Reference_Guides/Command_References/H3C_CRs_R63xx-9413/03/202303/1801117_294551_0.htm)。实机同轮查询样本保存在 `test/fixtures/h3c-r6328p03-r4/`；目前验证的是零记录空表，有漂移记录时的实机判断尚待现场验证。
