# 交换机通信模式封装

## 连接可行性（2026-11-02）

| 接法 | 判断 | 依据与限制 |
| --- | --- | --- |
| ESP RJ45 网口 → H3C 普通以太网口 | 可用，已有实机 SSH 命令记录 | ESP `192.168.10.1` → H3C `192.168.10.2:22`；与 Console 无关。环路期间该管理链路曾中断。 |
| ESP RJ45 网口 → H3C RJ45 Console | 不可用 | ESP 的 W5500 是以太网控制器，交换机 Console 是 EIA/TIA-232，插座外形不代表协议或电平相同。 |
| ESP 原生 Type-C → 转接头 → 现有 PL2303 USB Console 线 → H3C RJ45 Console | 有可实现路径，尚未完成实机验证 | ESP 必须运行 USB Host，驱动 `067b:2303`，配置串口并为外设安排 VBUS 供电。 |

用户提供的 Windows 硬件 ID 是 `USB\\VID_067B&PID_2303&REV_0400`；只据此记录 PL2303 系列和 USB 版本描述，不据此保证芯片后缀、真伪或全部驱动兼容性。现有 USB Console 线已经能通过 SecureCRT 操作交换机，不需要再串联 MAX3232。Type-C 转接头只改变连接形式，不能把现有 USB Device 固件变成 USB Host，也不能代替驱动。

厂商 2024-12-21 原理图显示：Type-C D+/D− 接 GPIO20/19；CC1/CC2 各为 5.1kΩ 下拉；`USB VCC`、VBUS 和 `POE_5V` 同网，通过 D1 单向给 `VSYS/5V` 供电。因此仅给 `VSYS/5V` 供电不能据此推定 Type-C VBUS 有 5V；需要检查实际供电位置、转接方案与外设负载。图上存在 PoE 扩展接口不等于已安装 PoE 供电模块。软件没有提供板上不存在的 VBUS 电源开关。

原生 USB Host 与现有 USB Serial/JTAG 共用内部 PHY。接入主机驱动时要使用独立编译配置，关闭原生 USB CDC/JTAG 的占用；不能继续把同一个 Type-C 口同时接电脑作为 USB 设备和接 Console 线作为主机。本次没有切换 USB 模式、修改硬件供电或烧录固件。

## 软件接口

```text
SwitchRuntime / BDI / 命令页面
             │ ICommandTransport
             ├─ SwitchSsh → Ethernet SSH
             ├─ ManagedConsole → ConsoleSession → UartConsolePort → MAX3232 → RS232
             ├─ ManagedConsole → ConsoleSession → TcpConsolePort → 串口服务器 → RS232
             └─ SwitchUsbConsole → IUsbSerialHost → 待接入 PL2303 Host 驱动
```

`src/net/switch_communication.h` 是组合入口；固定模式只初始化选中的通道；双通道模式优先初始化 SSH，仅在 SSH 持续不可用且旧请求结束后才启动 UART，会话切换由路由器控制，绝不跨通道重放旧命令。
`src/config/communication_settings.h` 集中定义构建参数：

| PlatformIO 环境 | 通道 | 默认配置 |
| --- | --- | --- |
| `esp32-s3-devkitc-1` | SSH（默认不变） | 使用已保存的 IP、账号、密码、主机公钥 |
| `esp32-s3-console-uart` | GPIO UART Console | UART1，TX17、RX18，9600/8N1，无流控 |
| `esp32-s3-console-tcp` | TCP Console | 透明 TCP 服务器 `192.168.10.3:4001` |

构建示例：`pio run -e esp32-s3-console-uart`。更换模式需要选择对应固件构建并烧录，固定模式不支持页面切换；自动故障切换使用下述独立双通道构建。GPIO 参数仅允许 GPIO17/18 两种方向，避免占用 W5500、TF、BOOT、RGB、原生 USB；本项目未启用摄像头，后续接摄像头前需重新核对复用。

GPIO 模式接线：GPIO17 → MAX3232 T1IN；GPIO18 ← MAX3232 R1OUT；3V3 → VCC；GND → GND。模块的 RS232 侧再接 Console 的 TX/RX/GND，按现有 Console 转接头的实际针脚确认。模块必须支持 3.3V TTL；GPIO 不可直接接 RS232。使用板上 Type-C 5V 给 ESP 供电，MAX3232 从 ESP 的 3V3 供电。UART 模式不要求网线存在，MQTT 仍是可选网络能力。

TCP 模式要求串口服务器为 **原始透明 TCP Server**，不是 Telnet/RFC2217；服务器串口参数需另设为 9600/8N1/无流控，ESP 不会通过原始 TCP 自动配置远端波特率。TCP 建连和收发非阻塞，绑定本机 Ethernet 地址，链路中断撤销会话，5 秒建连超时。此路径仍依赖以太网，不能保证在同一风暴网络中可达。

Console 账号密码复用已保存的交换机凭据；`protocol=none` 仍关闭通道。Console 无 SSH 主机公钥握手。页面显示实际 `management.transport/state/session_accepted`；Console 心跳为 CLI 空闲提示符探测，不再依赖交换机管理 IP 的 ICMP。

新 Console 固件默认只采集、分析，不继承 SSH 路径的自动隔离绑定。绑定需现场确认物理设备、保护端口和通道 identity 后显式调用 `bindManagementProfile(..., path, identity)`；identity 包含 GPIO/波特率或桥接端点/波特率，修改后必须重新确认。不要在每次配置更新时自动绑定。


- `src/net/command_transport.h`：统一命令提交、内部隔离请求、异步结果和会话状态。SSH 仍要求自动保存的主机公钥通过验证；Console 的物理会话接受状态不等同于加密身份认证。
- `src/net/usb_serial_host.h`：USB 驱动边界，固定目标 VID/PID，要求完成 PL2303 初始化、9600/8N1/无流控设置后才能报告 ready；读写必须非阻塞。每次插拔递增 generation 并清空旧收发缓冲，禁止跨设备或跨会话重放。
- `src/net/switch_usb_console.h`：组合驱动与会话，双重核对 VID/PID，设备插拔或 generation 改变时撤销旧会话。当前没有提供或自动实例化真实 PL2303 驱动。
- `src/net/console_session.h`：与硬件无关的 Console 登录、提示符同步、分页设置、命令白名单、分段收发、限长输出、密码脱敏、超时和空闲 CR 探测。支持已处于用户视图、仅密码、用户名加密码三种入口；登录失败停止重试，需显式重新配置。
- `src/config/console_settings.h`：独立会话配置；默认 disabled，默认波特率 9600；target 只是设备标识，不用于联网。

默认固件仍运行 SSH；UART 和 TCP 使用独立构建环境。USB 仅保留接口，没有可用的生产构建或真实 PL2303 驱动。

BDI 中的 `Snapshot.managementConnected` 表示已接受的管理会话。USB Console 和 SSH 的管理绑定独立：SSH 绑定不能自动授权 USB 写操作。未来显式组合 USB 驱动后，需使用 `configure(target, revision, boot, true, 1, "usb_console")`，并对经过现场确认的同一设备建立 `bindManagementProfile(target, 1, protectedPort, "usb_console")`。这两个函数不会初始化 USB 或自行建立物理连接。设备型号、完整新鲜观察、端口范围、持久化意图和处置后回读校验继续生效。

现有 ICMP 心跳专属于 SSH/以太网路径；UART/TCP Console 构建已关闭该 ICMP 判据。Console 空闲探测只证明 CLI 提示符响应，不证明业务流量正常。交换机 CPU 过载时串口同样可能延迟或不响应。

## 验证边界

`test/test_console_session.py` 使用模拟 USB 驱动，在 ASan/UBSan 下验证 16 个场景：禁用、非法配置、错误设备、逐字节登录、密码登录、登录失败停止重试、命令白名单和 ID、密码脱敏、截断、命令错误、视图逐步校验、错误端口不发 shutdown、超时不重放、拔插撤销请求、设备代际切换、空闲探测。

`test/h3c_bdi.cpp` 增加管理路径切换测试：旧 SSH 绑定不授权 USB 写操作；显式 USB 绑定后沿用已有策略；无效 USB 端点不采集。以上均为主机模拟测试，不是 USB 枚举、PL2303 兼容或真实交换机 Console 验收。

## 厂商资料

- [ESP32-S3-ETH 原理图](https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf)
- [Espressif USB Host：PHY 与 VBUS 要求](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)
- [Arduino USB Host：板端 VBUS 和会话 API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_host.html)
- [TinyUSB PL2303 支持与 VID/PID 表](https://github.com/hathach/tinyusb/blob/master/src/tusb_option.h)
- [H3C S5130S-EI Console 规格](https://www.h3c.com/en/d_202211/1719936_294551_0.htm)

上游 TinyUSB 包含 PL2303 不等于本工程安装的预编译 TinyUSB 已包含该驱动；接入时必须核对实际库与驱动版本并独立编译验证。

UART/TCP 的 `ManagedConsole` 另外使用模拟字节端口验证登录、命令结果、断线取消、不重放、重连和禁用；`test/console_ports.cpp` 使用硬件/Socket 桩执行真实适配器代码，验证 UART 引脚、分段收发、非阻塞 TCP 建连、超时、EOF、网线断开。这些测试不代表已经接上真实 MAX3232 或串口服务器。

实现依据：[Arduino UART API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)、[ESP-IDF lwIP BSD Sockets](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/lwip.html)。

## SSH + GPIO Console 双通道

构建环境 `esp32-s3-ssh-console`（`ESP_SWITCH_CHANNEL=3`）：网口接 H3C 普通业务/管理口，GPIO17/18 经 3.3V MAX3232 接独立 Console 口，两条线同时保留。默认环境仍为单 SSH，本次未自动烧录或改动设备接线。

`src/net/failover_transport.h` 是与硬件无关的路由器，BDI/命令页面仍调用同一个接口。

1. SSH 是主通道；SSH 正常时不启动 UART、不发送 Console 登录或探测字符。网口、MQTT 可以继续工作。
2. SSH 受信任会话持续不可用达到配置的等待时间（默认 5 秒），且旧请求已结束，才启动 UART 并尝试 Console 登录；登录成功才切换。启动时首次 SSH 一直无法登录也适用。Console 尚未登录时不假报成功；等待登录期间 SSH 若恢复，会停止 Console 尝试并继续使用 SSH。
3. 如果旧 SSH 请求还在排队/执行，必须等原通道返回终态并取走结果后才切换。不能只改指针或按计时器丢弃结果，因为旧交换机操作可能仍在执行。旧命令不会在 Console 重放；永久未结束的请求会阻止切换，需人工检查。
4. 切换后清除 BDI 旧快照、重新识别和采集。原 SSH 的现场绑定不会自动授权 Console 写操作；在未确认 Console 物理设备绑定前，仅采集、诊断，不自动隔离。
5. 切入 Console 后暂停 SSH 工作线程，等线程确认停止才允许 Console 业务命令。Console 请求执行结束后进入空闲等待，固定 5 秒内若又有新请求则等新请求结束重新计时。空闲达到 5 秒才恢复 SSH 工作线程，开始登录尝试；登录及主机公钥校验成功后立即回切 SSH 并关闭 UART，不再额外等待“SSH 成功后稳定 5 秒”。尝试登录期间暂停接收新的 Console 业务命令；连接失败或尝试超过 60 秒则暂停 SSH、保留 Console，等待线程停止后继续业务。认证失败、公钥不匹配/保存失败、缺少密码时停止自动重复登录，需修改配置，避免反复错误认证。修改配置时若仍有未结束请求，路由器停止新命令并显示 `reconfiguration_requires_restart`，避免旧操作归属不明。
6. 状态与 MQTT 同时报告 `management.policy=ssh_uart_failover`、实际活动 `transport`、`ssh_state`、`console_state` 和会话接受状态；切换事件写入 TF 日志。ICMP 仍仅描述 SSH 网络，不决定 Console 健康。

本机 ASan/UBSan 测试覆盖 SSH 优先、5 秒阈值、无 Console 时不切换、旧隔离命令未结束时禁止切换、超时终态不重放、用户结果保留/ID 冲突、显式关闭、配置变更阻断和 Console 结束→等待→SSH 登录成功后自动回切。实际 MAX3232 和 H3C Console 尚待接线验收。

按需启动测试另覆盖：SSH 正常时从不启动备用口、5 秒前不启动、登录尝试只启动一次、SSH 在 Console 登录前恢复时停止备用口、再次掉线重新计时，以及旧隔离命令未结束时连备用 UART 都不启动。

## 配置切换等待时间

长按 BOOT 进入设置页，在交换机连接参数中填写“SSH 不可用后等待多久尝试 Console（秒）”，允许 1–300 秒，默认 5 秒。读取字段为 `/switch` 的 `failover_wait`，统一保存字段为 `/network` 的 `sw_failover_wait`，设备 NVS 保存后断电保留；旧配置未包含此字段时自动采用 5 秒，旧页面未提交该字段时保留当前值。

该时间仅作用于双通道固件。SSH 可用时仍不启动 UART。单独修改此字段仅更新等待策略，不重启 SSH/Console 或丢弃正在执行的请求；若已经处于掉线等待中，使用本次掉线已持续的时间与新阈值比较。其他连接参数变更继续遵守未结束命令的阻断规则。等待时间不替代 SSH 建连/命令超时，也不跳过旧命令结束或 Console 登录校验。

回切不等于确认业务故障已修复，只表示 SSH 管理会话恢复；回切同样清空旧快照并重新采集。Console 结果会保留供原请求读取，命令不会在 SSH 重放。Console 命令结束后的空闲等待固定 5 秒，与配置页的 SSH 掉线等待阈值相互独立。

顺序回归测试还检查：Console 隔离命令未结束时不恢复 SSH、结束后完整等待、等待中接入新 Console 命令重新计时、登录尝试中拒绝新 Console 业务命令、SSH 工作线程暂停确认、连接失败继续 Console、单次尝试截止，以及原请求结果保留且不跨通道重放。

## 小范围结构整理

- `src/config/communication_timing.h` 集中定义固件掉线等待的默认值/范围、Console 空闲等待与 SSH 尝试截止；设置校验与主备路由共用这些定义。
- `SwitchSsh::pause()` 请求暂停；`stopped()` 表示工作线程已完成停止；`resume(profile)` 使用保存的配置重新连接。通信装配不再直接用 `protocol=none` 表达暂停，也不再自行解析 `disabled` 状态字符串。
- 主备路由复用同一个“等待 SSH 停止确认”判断，保证状态展示与命令准入一致。
- 新增真实 SSH worker 代码的模拟回归：暂停立即撤销可用会话、等待旧连接/终端资源释放、恢复后重新认证，再次暂停并确认资源释放。通信顺序与原等待值均保持不变。
