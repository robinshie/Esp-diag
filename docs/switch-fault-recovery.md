# H3C SSH 故障处理与模拟实验

适用设备：H3C S5130S-52S-EI，Comware 7.1.070 R6328P03。新增固件版本为 `native-bdi-20261103-r15-faultrecovery`。初轮完成模拟故障和实机只读检查；随后用户要求烧录和自动测试，已将只读验收构建烧录到 ESP32。实机范围仍为只读，未执行 `shutdown`、`undo shutdown`、清计数器或故障注入。

## 新增处理能力

| 场景 | BDI 判断与动作 | 成功条件 |
| --- | --- | --- |
| 持续入站广播风暴 | 同一端口连续三个有效采样窗口达到显式配置的广播 pps 阈值，才提出 `shutdown` | 命令成功后，至少两轮新鲜状态同时确认 ADM 与 Administratively |
| 恢复本次运行中由 BDI 隔离的端口 | 操作者确认已排除故障，再申请 `undo shutdown`；恢复前重新检查目标、端口状态和保护规则，并持久化恢复意图 | 风暴端口恢复 UP 且广播速率连续低于阈值至少 60 秒；环路端口恢复 UP 且至少两个检测周期没有环路报告 |

原有直接环路证据隔离仍保留。广播风暴的默认 `stormBroadcastLimits` 为空，因此默认不授权风暴隔离。需要先取得正常业务基线，再逐端口设置阈值。模拟使用的 **1000 pps 仅是测试参数**，不能当作现场建议值；本轮未修改现场阈值。

采集命令是 `display counters inbound interface`。以累计广播包数差值除以采样时间计算 pps；至少 10 秒的采样窗口，超过 45 秒、缺轮、配置版本改变、计数回退、输出不完整或不支持的列都会中断连续证据。一个累计大数和一次短时突发不会授权隔离。这里只处理入站广播，未将组播、未知单播、CPU 高或 MAC 迁移直接升级为风暴隔离证据。

管理口 `GigabitEthernet1/0/1` 永久保护。动作仍需经过已绑定的管理目标、明确允许的业务端口、Access 类型、非聚合成员、非 STP 根/主端口且未受保护等检查。设备适配器只生成有界的端口命令，SSH 执行器逐步验证设备视图，Runtime 再核对请求、目标与动作的回执身份。

恢复不是定时自动打开端口。入口为配置窗口内的同源 `POST /switch/restore`，仅接受 `port` 和 `confirm=repair` 两个字段；返回 `queued` 只代表等待下一轮完整观察及授权，不代表恢复成功。状态可从 `/status` 的 `bdi.recovery` 查看。这里只提供专用接口，没有增加任意命令执行入口。重启后失去端口归属，不会恢复历史关闭端口；SSH 结果不明、持久化失败或故障恢复后复发，都停止自动重试，等待人工检查。复发时不会反复启停端口，也不承诺自动再次隔离。

## 可重复的模拟实验

在项目根目录执行：

```sh
python3 -m unittest test.test_switch_fault_recovery -v
```

测试运行真实的 EmbeddedBDI、Runtime、H3C 解析器和持久化账本，使用模拟设备及命令传输，不连接交换机。另由 SSH worker 回归检查实际生成的端口命令、会话视图和回执边界。

| 实验组 | 预期 |
| --- | --- |
| 实机计数器格式回放 | 管理口与业务口捕获样本均解析成功 |
| 证据边界 | 重复采样不累积，计数回退、缺轮、长间隔、短窗口、版本变化及无效数据重置证据 |
| 风暴隔离与恢复 | 三个连续超限窗口后仅一次模拟 shutdown；确认故障已排除后一次模拟 undo shutdown，低速率观察满 60 秒后 recovered |
| 单次突发 | 不执行动作 |
| 六种保护条件 | 未配置阈值、受保护端口、Trunk、聚合、STP 根端口、不支持的计数列都不执行动作；另单独检查管理口保护 |
| 隔离结果不明及重启 | 不重放隔离命令，不恢复无归属端口 |
| 恢复意图存储失败 | 不执行恢复；存储恢复后同一意图仍不自动重放 |
| 恢复结果不明及重启 | 不重放恢复命令，重启不继承恢复授权 |
| 恢复后风暴复发 | needs_review，不反复启停 |
| 原有环路流程 | 隔离、确认修复、恢复和两个检测周期回读验证全部走完整 Runtime |

输出：`test/.runtime/switch-fault-recovery-experiment.json`。模拟写命令只发给替身，`real_switch_writes` 为 0。测试启用 AddressSanitizer 和 UndefinedBehaviorSanitizer。模拟不能证明现场线路故障已解决，也不能替代未来经授权的实机写入验收。

## 烧录与离线自动验收

现场验收使用 `esp32-s3-readonly-acceptance` 环境，定义 `ESP_BDI_READ_ONLY=1`，在站点策略中关闭全部 BDI 端口写入授权，观察与采集继续运行。`/device/status` 同时给出 `acceptance_read_only=true` 与 `bdi.enabled=false`。普通构建仍按站点策略工作，风暴阈值为空时不授权风暴隔离。

```sh
/Users/robin/.platformio/penv/bin/pio run -e esp32-s3-readonly-acceptance --target upload --upload-port /dev/cu.usbmodem11201
```

首次烧录的固件 SHA-256 为 `64486ca53a14c7c683710ea0ecb4b9ced1821649e7cfc9f15251186d791b00ef`，esptool 写入及哈希校验成功。额外的 ASan/UBSan 实验验证：在该编译开关下同时出现模拟环路与持续广播超限，Runtime 仍不下发端口写命令。此构建与完整策略实验可一起运行：

```sh
python3 -m unittest test.test_switch_fault_recovery test.test_switch_release_validation -v
```

ESP 日志热点没有互联网。实机 HTTP 验收程序无需 Codex 在线：先三击 BOOT 打开十分钟只读窗口，再运行本地程序。已连接热点时直接运行：

```sh
python3 tools/verify_switch_release.py --observe 60
```

也可传入 `--join-network ESP-Logs-f4697090 --restore-network '原来的Wi-Fi名称'`，由系统网络工具临时切换并在 finally 阶段恢复。程序不会输入、读取或更改 Wi-Fi 密码。若网络工具报错，会保留错误和恢复到原 IPv4 地址的检查结果；地址检查不是 SSID 身份证明。

验收连续采样至少 60 秒，要求同一启动、SSH 与心跳正常，至少两轮六张核心表完整新鲜、三项策略已装配、风暴默认关闭、全部状态读取命令成功，配置入口关闭且捕获记录中无端口写命令。日志缓冲区命令允许报告 `truncated`，并明确写入警告；截断不会被计为完整日志，也不放宽接口、Down 原因、环路、STP、聚合和流量表的完整性要求。

状态与下载原始证据保存在 `test/.runtime/r15-hardware-diagnostics.json`，验收结果保存在 `test/.runtime/r15-hardware-acceptance.json`；重跑会保留上一份结果。窗口到期、读取失败或网络中断会保留部分数据，并将整体通过标志置为 false；`runtime_passed` 单独表示已经采集到的运行时证据是否满足检查，不等于完整下载成功。第一次下载在窗口关闭后中断，已保留该失败记录，不能将其描述为完整验收通过。

第二次自动验收的设备侧检查全部通过：15 次状态采样、6 个完整周期（65、66、67、68、69、71），同一 boot `1ff35348ebd91abb5cc438180e20ff29`，SSH 与心跳保持正常。接口、Down 原因、环路、STP、聚合、流量六表完整且新鲜，10 个状态读取命令返回 ok，日志缓冲区读取返回有界截断。设备处于 monitoring、action=none、只读保护启用；捕获记录中无端口写命令。

共保存 25 包 TF 日志，所选文件读取无错误；补采达到 5 包上限，因此不承诺保存下载期间产生的全部日志。Mac 网络工具返回 `-3900`，原始自动流程保留 passed=false；随后已通过系统设置界面确认原联网 Wi-Fi 已连接。设备验收通过与网络命令失败分开记录，见 `test/.runtime/r15-release-validation.json`。没有将日志截断或末段上限解释为 SSH 掉线，也没有据此宣称此前物理链路抖动的根因已消除。

## 2026-11-03 实机只读观察

通过已连接的 SecureCRT 串口控制台执行以下查询，没有更改交换机配置：

```text
display counters inbound interface GigabitEthernet 1/0/1
display counters inbound interface GigabitEthernet 1/0/3
display users
display counters inbound interface GigabitEthernet 1/0/3
display interface GigabitEthernet 1/0/3
```

计数器列为 `Interface / Total (pkts) / Broadcast (pkts) / Multicast (pkts) / Err (pkts)`，与新增解析器匹配。供自动回放的两个文本文件是根据屏幕输出人工转录的样本，不是原始串口日志，位于 `test/fixtures/h3c-r6328p03/counters-inbound-ge*-readonly.txt`。

管理口样本：总包 227717，广播 2584，组播 1372，错误 0。业务口最后一次采样：总包 492767061，广播 492766905，组播 64，错误 11。业务口当时为 **Administratively DOWN**，Access/PVID 10；这些累计大数不能证明当前仍在发生广播风暴。本轮没有打开它，也没有假定其管理关闭状态属于当前 BDI。

`display users` 显示 VTY 0，类型 SSH，用户 admin，来源 192.168.10.1，登录时间 Jan 02 02:03:12，空闲 00:00:00。该会话在本轮检查时仍在使用。设备时钟仍是 2013 年，不能作为真实日期。此结果支持“单条旧会话 logout 日志不能判定当前 SSH 失效”；不能据此排除此前 GE1/0/1 的物理链路抖动。

## 官方命令依据

[H3C R63xx 以太网接口命令参考](https://www.h3c.com/en/d_202108/1434446_294551_0.htm) 定义了入站包计数器、Overflow/不支持字段和接口 shutdown 命令。本实现使用累计计数器和端口隔离，没有自动下发设备级 storm-constrain 或修改 VLAN、STP、LACP 配置。
