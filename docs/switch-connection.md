# 交换机管理连接配置

## 使用

1. 将 ESP 网口接入交换机所在的管理网络。长按 BOOT 三秒，蓝灯闪烁后手机连接 ESP-Config 热点并打开配置页。
2. 填写 ESP 的 IP、掩码、网关、DNS；在同一表单中选择 SSH、Telnet 或“暂不配置”。交换机管理 IP 是交换机自己的地址，不能填成 ESP 地址。
3. 填写管理端口（SSH 默认 22，Telnet 默认 23）、用户名、登录密码、可选特权密码，以及连接/命令超时（1–30 秒）。SSH 可选交互终端或单命令执行；自动处置使用交互终端。主机公钥自动配对并只读显示，无需填写指纹。Telnet 必须确认明文传输提示，但目前仍不连接。
4. 点击底部唯一的“保存全部配置”。网口、交换机和 MQTT 参数全部通过校验后，一次写入设备 NVS。网口一秒后应用，绿灯亮三秒后关闭热点、蓝牙和 HTTP。断电保留，重新配置须再次长按 BOOT 三秒。

密码输入留空保留原值，清空复选框优先于输入；读接口仅返回密码是否存在。切换目标时请明确替换或清空旧密码。密码不写入日志，不发送到浏览器控制台或 MQTT。设备 NVS 未启用加密，配置热点和 HTTP 也未加密，应在可信现场操作。

首次连接时，ESP 自动取得并持久保存当前目标的 SSH 主机公钥，在发送登录密码前完成校验。之后重连必须匹配可信指纹；换钥时进入 `mismatch` 并停止登录，不自动信任新钥。现场确认更换或重配置后，长按 BOOT 3 秒进入配置页，点击“重新配对当前交换机公钥”；这只清当前目标的旧配对并重新尝试。普通保存不提交指纹，也不能关闭校验。

MQTT 参数随全部配置一次保存，不需要验证码、二次确认或先测试连接；仍检查 IP、端口等基本格式，不改变服务器本身的账号认证要求。

## 当前能力边界

ESP 通过板载 W5500 使用 LibSSH-ESP32 建立密码认证会话。独立 SSH 任务串行处理连接、只读查询和受限动作，主循环运行实际 Embedded-BDI。`shell` 请求 PTY 与交互终端；`exec` 支持单命令查询，自动隔离只在 shell 下执行。只有认证及所需会话建立完成才报告 connected。Telnet、SSH 用户密钥认证、SNMP 和串口管理目前不提供。

SSH 首次登录后启动独立 BDI ping 心跳；正式 H3C 运行时同时轮询下文九项固定命令、判断真实返回并执行授权业务口隔离。只读采集不等于允许写入：身份未验证、解析不完整、采样过期、跨目标或混轮均阻止动作。AI 分类结果不能派发写命令。

网络或会话失败后按重试周期重新连接；认证失败或主机钥不匹配停止自动重试，后者必须现场重新配对。配置页每两秒刷新实际 SSH 状态。事件进入本机 TF 日志，不打印密码；正式固件不依靠串口来查看或控制。历史 Python Paramiko 客户端仅用于旧网关实验，与设备 NVS 的 SSH 配置相互独立。

## 存储与接口

- `GET /switch`：仅在配置窗口内返回 URL 编码的非秘密参数、`has_password`、`has_enable_password`、`sw_host_key_mode=auto`、`sw_host_key_state`、只读 `sw_fingerprint`，以及 `client_supported=1&state=<实际 SSH 状态>`；设置 no-store。
- `POST /network`：统一表单带 `settings_version=2`；原有网口字段与 `sw_protocol/host/port/username/password/enable_password/transport/connect_timeout/command_timeout` 一并提交。另有 `sw_clear_password`、`sw_clear_enable_password`、`sw_telnet_ack` 以及 `mqtt_enabled/host/port/username/password/prefix/clear_password`。POST 要求配置窗口开启和合法 Origin。任何校验失败都不更新 NVS 或运行参数。
- NVS `network/setup-v2`：版本号与长度分帧的网口、交换机、MQTT 三部分，整份一次写入；启动优先读取新记录，并兼容旧 `setup-v1`、`ipv4` 和 `mqtt-lab`。旧 BLE 四字段写入会保留交换机及 MQTT 配置。
- 手机热点配置页支持全部字段。电脑本地配置页始终显示 SSH / Telnet 区域，未连到设备接口时禁用编辑并说明入口；旧固件或接口读取失败时显示升级/重新开启配置窗口的提示，不再隐藏区域。Web Bluetooth 四字段协议保持兼容，仅配置网口，不传输交换机凭据。
- 页面只有一个保存按钮。MQTT 与网口、交换机统一保存，旧 `POST /mqtt` 返回 409 提示刷新。MQTT 能力始终编译、由 NVS 开关控制且默认关闭；SSH 已接入，Telnet 尚未接入。
- `POST /switch/host-key/reset`：只在物理配置窗口中接受 `confirm=repair`，检查 Origin，清除当前目标的旧信任并重新连接；失败不自动重放。主机钥独立保存在 NVS 信任记录中，配置表单不控制是否校验。

默认 SSH 目标为 `192.168.10.2:22`，用户名 `admin`，密码使用当前固件预置值。已有 NVS 配置优先，不会被固件默认值覆盖。ESP 默认网口为 `192.168.10.1/24`，网关 `192.168.10.10`。

## 验证

`c++ -std=c++11 test/switch_settings.cpp -o /tmp/esp-switch-settings && /tmp/esp-switch-settings`

修改页面后运行 `python3 tools/embed_network_page.py` 更新 `src/generated/` 中的固件内嵌页面。已通过编译和模拟 API 工作流测试。实机验收以设备页面和本机日志中的实际 IP、链路和 SSH 状态为准，不能仅凭烧录成功判定已登录。

SSH 工作流测试（先安装 PlatformIO 依赖）：

```sh
c++ -std=c++11 -Itest/ssh_stubs -I.pio/libdeps/esp32-s3-devkitc-1/LibSSH-ESP32/src test/switch_ssh.cpp -o /tmp/esp-switch-ssh
/tmp/esp-switch-ssh
```

## 历史现场初始化构建与登录记录

`esp32-s3-site-setup` 会在首次启动时将 ESP 网口及交换机连接参数替换为源码默认值，保留 MQTT 和无关 NVS 数据。使用 `site-defaults` 标记 20261102，后续启动不会重复覆盖手动保存的配置。仅在需要更新该现场旧配置时显式烧录此环境，普通构建仍优先采用已有 NVS。

```sh
PLATFORMIO_CORE_DIR="$PWD/.pio-core" ~/.platformio/penv/bin/pio run -e esp32-s3-site-setup --target upload --upload-port /dev/cu.usbmodem1201
```

此前开发固件串口每五秒输出 `[SSH][STATUS]`，包含实际 IP、链路和 SSH 目标，不包含密码。连接阶段失败输出 `connect_error`，用于区分超时和 SSH 协议协商错误。

### 2026-11-02 实机烧录结果（此前登录阶段）

已向 ESP32-S3 烧录现场配置环境并通过写入哈希校验。设备确认保存现场默认值，实际网口 `192.168.10.1/24`、网关 `192.168.10.10`，SSH 目标 `192.168.10.2:22`。网线链路随后为 up，主循环持续输出状态，无重启。SSH 返回 `Timeout connecting to 192.168.10.2`，未进入密码认证，尚未验证登录成功。需核对交换机管理 IP、接入端口所属管理 VLAN、SSH 服务及访问控制。

重试时交换机已响应，日志明确报 `no match for method server host key algo: server [ssh-rsa]`。固件为该旧 Comware 兼容主机密钥签名算法，在 libssh 默认列表后追加 `ssh-rsa`，保留默认现代算法的优先顺序。此项不更改密码认证方式，也不跳过配置的主机指纹检查。

追加 ssh-rsa 后，实机已出现 `authenticating` 和 `connected`。随后曾发生一次 TF 卡目录字符串分配失败，回溯为 `operator new` → `SdDatabaseMirror::listing` → `SdJournal::snapshot`。构建现启用板载 OPI PSRAM（`qio_opi`、`BOARD_HAS_PSRAM`），启动输出 `[MEMORY]` 用于确认实际容量，需结合 SSH 和 TF 卡状态观察验证。

最终 PSRAM 构建已烧录并校验，启动实测 `psram_bytes=8388608`。从启动观察约 100 秒：第一次连接超时，自动重试后在约 45 秒进入 `connected`，持续至观察结束；约 75 秒 TF 卡进入 `ready`，随后 SSH 和 TF 卡并行运行，无再次崩溃或重启，内部可用堆约 66 KB。最终现场地址为 ESP `192.168.10.1` → 交换机 `192.168.10.2:22`。此记录仅验证登录与短时稳定性，未执行交换机业务命令。

## SSH 登录后的 BDI ping 心跳

首次 SSH 登录成功后，`+ssh_connected <- ping_switch` 触发第一次探测，随后每 5 秒由 `!verify_switch_heartbeat` 计划触发一次 ICMP Echo。每次超时为 1 秒，socket 绑定 W5500 以太网接口，回复地址必须匹配当前交换机管理 IP。SDK ping 回调仅提交结果，主循环执行 BDI 推理，不在 SSH 或 ping 任务里修改信念。

- 第一份有效回复：`switch_heartbeat_ok=true`，执行 `report_heartbeat_ok`。
- 1–2 次连续失败：`suspect`；连续 3 次失败：`switch_heartbeat_lost=true`，执行告警计划。
- 告警后第一份回复：`recovering`；连续 2 次有效回复：清除心跳告警并报告恢复。
- 超过 15 秒没有新结果：`unknown`，不显示正常，也不清除已知心跳告警。
- 本地探测资源错误：`PROBE_ERROR`、`unknown`，不猜测交换机故障。
- 首次 SSH 登录前不发 ping。登录后即使 SSH 断开仍继续 ping，可分别观察会话状态和 IP 可达性；这不代表 SSH 会话仍正常。
- 修改目标或网口配置时重置心跳上下文，等待新配置下的 SSH 登录，丢弃旧一轮探测结果。

心跳上下文使用 `ethernet-switch-<IP>` 标识，只更新 SSH 与 ping 信念，不设置 `server_healthy`，不清除端口、风扇、电源等业务故障，也不执行恢复命令。心跳功能不自动启用 MQTT。配置窗口内 `/switch` 附带 `heartbeat_state`、`heartbeat_rtt_ms`、`heartbeat_misses` 和 `heartbeat_reason`；配置页分别显示 SSH 与 ping 状态。

本机日志示例格式（此前串口验收也使用同样内容）：

```text
[BDI][ACTION] device=ethernet-switch-192.168.10.2 decision=PING_SWITCH transport=ICMP
[BDI][HEARTBEAT] target=192.168.10.2 transport=ICMP state=healthy rtt_ms=... misses=0 successes=... reason=ECHO_REPLY observed_ms=...
```

故障阈值、抖动、恢复、监测过期、切换目标及业务故障隔离使用实际 Embedded-BDI 计划测试，并启用 ASan/UBSan：

```sh
.venv/bin/python -m unittest discover -s test -p test_heartbeat_bdi.py
```

心跳实测准备期间，TF 卡历史目录扫描曾触发 CPU0 空闲任务看门狗。扫描、淘汰查找和历史目录翻页的逐条循环已加入短暂调度让出；不关闭看门狗，也不跳过存储初始化。

修复后实机连续观察至约 150 秒：SSH 保持 connected，BDI 每 5 秒收到 ECHO_REPLY，RTT 约 1 ms、连续失败 0，TF 卡完成初始化并进入 ready；观察期内无重启。断线告警和恢复阈值已通过原生测试，尚未拔线验证。

## 使用登录后的 SSH 执行手动只读查询

现场 `display version` 确认实际型号为 **S5130S-52S-EI**，Comware 7.1.070、Release 6328P03。命令已核对 [H3C S5130S-EI R63xx 命令参考](https://www.h3c.com/cn/Service/Document_Software/Document_Center/Home/Public/00-Public/Command/Command_Manual/H3C_S5130S_EI_D_CR_R63xx-25569/99/)，开放以下固定查询；本地 S5120V2-LI 产品线表不作为现场型号识别依据：

| 查询标识 | 发给交换机的命令 | 用途 |
| --- | --- | --- |
| `version` | `display version` | 型号与软件版本 |
| `device` | `display device` | 设备状态 |
| `interfaces` | `display interface brief` | 接口概要 |
| `cpu` | `display cpu-usage` | CPU 使用率 |
| `memory` | `display memory` | 内存情况 |

ESP 复用已经认证的 SSH 连接，一次执行一个查询。shell 模式先同步提示符并执行仅影响当前终端的 `screen-length disable`，随后读取到相同提示符为止。输出最多保留 8192 字节，超限明确标记 truncated；超时或断线不自动重发，且关闭边界不明确的会话。查询不会修改 BDI 业务故障状态；心跳继续独立运行。

网页入口：按住 BOOT 三秒开启配置窗口，通过当前 ESP 网口地址（本现场 `http://192.168.10.1`）或配置热点访问页面。在“交换机只读查询”选择项目并点击“执行查询”，下面显示交换机原始返回。只查询时无需保存配置。

正式版通过配置窗口内的 `POST /switch/command` 提交 request_id、command，随后 `GET /switch/command?request_id=...` 读取结果；只接受表中标识，检查 Origin。交换机返回会直接进入 ESP 本机日志。正式版不提供 USB 查询入口，历史 USB 测试记录仅作开发阶段参考。

### 2026-11-02 五项只读查询实机验收（此前版本）

该次只读查询固件已烧录并通过哈希校验。SSH 就绪后，上表五项查询全部返回 `ok`、未截断，并验证每份结果只有一个结束提示符。接口输出覆盖至 `XGE1/0/52`，其中 `Vlan10` 为 `192.168.10.2`、UP，`GE1/0/1` 为 UP、100M、PVID 10。设备型号及版本为 S5130S-52S-EI / R6328P03，CPU 与内存统计均有真实数据。

查询后继续观察 15 秒：SSH 保持 connected，TF 卡 ready，BDI 心跳 healthy、RTT 1–2 ms、连续失败 0，无重启。重启后 SSH 尚未就绪时曾有一次查询被明确拒绝为 `not_connected`，未发往交换机；等待实际 connected 后才进行最终验收。此前开发阶段的 USB 实测结果保存在 `test/.runtime/ssh-queries-live.json`，查询后运行状态记录在 `test/.runtime/ssh-queries-heartbeat-live.log`。网页查询交互通过模拟 HTTP 测试，未在本轮进行真实浏览器点击验收。

## 正式自动采集与 BDI 处置

自动轮询固定九项，按真实完成时间写入同一采集周期：

| 标识 | 命令 | 作用 |
| --- | --- | --- |
| `version` | `display version` | 核对现场型号与版本 |
| `interfaces` | `display interface brief` | 端口链路、access/trunk/hybrid、PVID |
| `down` | `display interface brief down` | 区分物理断链、管理关闭及保护原因 |
| `loops` | `display loopback-detection` | 环路检测是否开启、检测周期、环路端口及既有动作 |
| `stp` | `display stp brief` | STP 角色、转发/丢弃和保护 |
| `aggregation` | `display link-aggregation verbose` | 聚合成员，包括非选中成员 |
| `logs` | `display logbuffer reverse` | 真实交换机日志，供 ESP 本机分类与记录 |
| `cpu` | `display cpu-usage` | CPU 实际输出 |
| `memory` | `display memory` | 内存实际输出 |

`display device` 仍作为手动查询保留，不属于这九项自动轮询。前述五项只读实机结果只证明当时的命令返回，不能作为新增自动写操作或故障注入的验收结论。

H3C BDI 使用真实 `Agent / BeliefBase / PlanBase / IntentionBase`，维护 `loop_confirmed / action_allowed / management_ready`。当前现场授权全部业务口，保护管理口 `GE1/0/1`；trunk/hybrid、聚合成员、STP ROOT/MAST、已有保护与限制转发的端口仍不允许写入。解析未知、截断、过期、混轮或目标变化时保持 unknown / blocked，不将空闲物理断开的端口视为需要 `undo shutdown` 的故障。

符合全部条件时，先持久化本目标、端口和 incident 的意图，再执行 `system-view`、指定接口、`shutdown`、`return`。每步核对返回，失败或会话边界不明时停止且不盲目重放。ACK 只是命令结果，后续至少两个不同且完整的采集周期同时确认 ADM 与管理关闭原因，才标记 `contained`。此状态说明端口已隔离，不能声称物理环路已经消除。

正式流程不会自动恢复隔离端口、改 STP、开启全局环路检测、保存交换机启动配置或重启交换机。现场排除物理环路后，恢复仍须按动作所有权、当前保护条件和独立状态验证处理。ping 成功、SSH 登录成功或 AI 给出环路分类都不能替代这些条件。

原生日志使用统一 `device / port / fault / incident / request_id` 关联观察、命令、回执与验证：`[BDI][OBSERVE]`、`[BDI][CMD]`、`[BDI][ACK]`、`[BDI][VERIFY]`。三击 BOOT 的日志页与可选 MQTT 后台均展示同一份真实证据。
