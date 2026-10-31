# 交换机管理连接配置

## 使用

1. 将 ESP 网口接入交换机所在的管理网络。长按 BOOT 三秒，蓝灯闪烁后手机连接 ESP-Config 热点并打开配置页。
2. 填写 ESP 的 IP、掩码、网关、DNS；在同一表单中选择 SSH、Telnet 或“暂不配置”。交换机管理 IP 是交换机自己的地址，不能填成 ESP 地址。
3. 填写管理端口（SSH 默认 22，Telnet 默认 23）、用户名、登录密码、可选特权密码，以及连接/命令超时（1–30 秒）。SSH 可选交互终端或单命令执行；“验证 SSH 主机指纹”为可选项，不勾选可不填写指纹。勾选后需填写可信渠道取得的 SHA256 主机指纹。Telnet 必须确认明文传输提示。
4. 点击底部唯一的“保存全部配置”。网口、交换机和 MQTT 参数全部通过校验后，一次写入设备 NVS。网口一秒后应用，绿灯亮三秒后关闭热点、蓝牙和 HTTP。断电保留，重新配置须再次长按 BOOT 三秒。

密码输入留空保留原值，清空复选框优先于输入；读接口仅返回密码是否存在。切换目标时请明确替换或清空旧密码。密码不写入日志，不发送到浏览器控制台或 MQTT。设备 NVS 未启用加密，配置热点和 HTTP 也未加密，应在可信现场操作。

不勾选指纹验证时，保存空指纹，表示跳过 SSH 主机身份核验；无法确认连接对象是否为预期交换机。已保存指纹的旧配置仍显示勾选状态，取消勾选并保存可改为跳过验证。该选项只记录后续客户端应采用的策略，当前 ESP 直连客户端仍未实现。

MQTT 参数随全部配置一次保存，不需要验证码、二次确认或先测试连接；仍检查 IP、端口等基本格式，不改变服务器本身的账号认证要求。

## 当前能力边界

此功能完整覆盖**连接参数配置与保存**，不启动连接，不代表 SSH / Telnet 已登录成功。ESP 固件尚无 SSH / Telnet 执行客户端，界面明确标示此状态。现有 Python 网关的 Paramiko SSH 客户端仍独立使用 `config/ssh.example.json` 与 known_hosts，不自动读取此配置。没有新增自动执行、远程命令、环路端口隔离或连接测试按钮。SSH 密钥认证、SNMP、串口管理目前不提供。

下一阶段需将协议客户端接入这些参数，实现可信主机校验、限时登录、只读采集、断线重连、状态上报，然后接入端口白名单和隔离后恢复验证，才能完成真实自动处置闭环。不能仅凭配置保存成功显示“已连接”或“已解决环路”。

## 存储与接口

- `GET /switch`：仅在配置窗口内返回 URL 编码的非秘密参数、`has_password`、`has_enable_password`、`verify_fingerprint`，以及 `client_supported=0&state=not_connected`；设置 no-store。
- `POST /network`：统一表单带 `settings_version=2`；原有网口字段与 `sw_protocol/host/port/username/password/enable_password/fingerprint/transport/connect_timeout/command_timeout` 一并提交。另有 `sw_clear_password`、`sw_clear_enable_password`、`sw_telnet_ack` 以及 `mqtt_enabled/host/port/username/password/prefix/clear_password`。POST 要求配置窗口开启和合法 Origin。任何校验失败都不更新 NVS 或运行参数。
- NVS `network/setup-v2`：版本号与长度分帧的网口、交换机、MQTT 三部分，整份一次写入；启动优先读取新记录，并兼容旧 `setup-v1`、`ipv4` 和 `mqtt-lab`。旧 BLE 四字段写入会保留交换机及 MQTT 配置。
- 手机热点配置页支持全部字段。电脑本地配置页始终显示 SSH / Telnet 区域，未连到设备接口时禁用编辑并说明入口；旧固件或接口读取失败时显示升级/重新开启配置窗口的提示，不再隐藏区域。Web Bluetooth 四字段协议保持兼容，仅配置网口，不传输交换机凭据。
- 页面只有一个保存按钮。MQTT 与网口、交换机统一保存，旧 `POST /mqtt` 返回 409 提示刷新。普通固件仍不会连接 MQTT；SSH / Telnet 客户端尚未接入。
- `sw_verify_fingerprint=0` 清空指纹并记录跳过验证；`=1` 要求提供合法的 `sw_fingerprint`。旧客户端未提供此开关时，是否验证由非空指纹决定。

## 验证

`c++ -std=c++11 test/switch_settings.cpp -o /tmp/esp-switch-settings && /tmp/esp-switch-settings`

修改页面后运行 `python3 tools/embed_network_page.py` 更新固件内嵌页面。固件编译通过后仍需单独授权烧录和真实交换机联调。
