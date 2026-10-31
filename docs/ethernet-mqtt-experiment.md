# ESP32 网口 MQTT 实验（未运行）

本次只实现和编译，不烧录、不连接 Broker、不改当前开发板和运行中的网关。
普通构建 `esp32-s3-devkitc-1` 编译时关闭 MQTT 实验；新增实验构建
`esp32-s3-eth-mqtt-lab`，运行时配置仍默认 `enabled=false`。
`platformio.default_envs` 明确指向普通构建，原 `flash.sh` 不会默认烧录实验构建。

## 后续手动实验方法

以下为后续操作说明，本轮未执行：

1. 将实验构建烧录到开发板，并将网线接入配置 IP 所在网络。
2. 按 BOOT 三秒开启配置，手机连接 Wi-Fi 配置热点。
3. 页面新增“网口 MQTT 实验”区，手动填写 Broker 的局域网 IPv4、TCP 端口、
   用户名、密码和 `esp-lab` 开头的实验主题前缀，勾选启用并保存。
4. 设备绿灯亮三秒后关闭配置入口，MQTT 仍在后台工作。
5. 在 Broker 上订阅下面的独立实验主题检查消息；关闭实验时重新按 BOOT 三秒，取消启用并保存。

Broker 地址不能填电脑的 `127.0.0.1` 或 `localhost`，也不能填 ESP32 自己的地址。
Broker 必须监听能从设备网段访问的地址，端口需按现场配置，例如 1883；
本项目本机演示端口可能仅绑定回环，单改设备参数不等于 Broker 已开放局域网访问。
本实现不修改服务器监听地址、防火墙或 ACL。

网口、交换机和 MQTT 参数通过底部唯一的“保存全部配置”统一提交，
全部校验后一次写入 NVS `setup-v2`。任何部分校验失败均不更新配置，
成功后三秒关闭配置窗口；兼容读取原有独立 MQTT 存储。
密码不回传网页、不打印串口；留空保留原密码，勾选“清空已保存的密码”才删除。
实验参数使用设备 NVS 普通存储，热点配置走 HTTP；本轮不提供 NVS 加密或 TLS。
仅用于受控局域网实验，不建议复用正式环境密码。
蓝牙现有特征仍仅修改网口参数，MQTT 实验参数通过 Wi-Fi / 网口配置页填写。

## 消息与边界

默认主题：

- `esp-lab/esp-lab-<芯片ID>/availability`：retained `ONLINE`，连接异常断开时 Broker 发布遗嘱 `OFFLINE`。
- `esp-lab/esp-lab-<芯片ID>/status`：每五秒 JSON，包含实际 IP、`transport=ETH`、运行时长、空闲内存和丢弃计数。
- `esp-lab/esp-lab-<芯片ID>/serial`：固件 `bdi_output` 输出的 BDI 协议原文。

采用 Arduino 框架内置 ESP-MQTT 异步任务，TCP socket 绑定 W5500 对应的网络接口。
只在网口 link up 且有 IP、实验构建开启且运行配置启用时启动连接。
连接超时两秒、重连间隔十秒；不在主循环等待网络连接。
上报使用 QoS 1，内存 outbox 上限 16 KiB，单条输出上限 8192 字节。
离线或队列不足时计数丢弃，状态消息会显示 `dropped`；消息可能重复，重启不保留队列。
状态消息和原文均不 retained，不会把实验原文冒充生产报告。

这是**单向实验上报**，不订阅远程命令，不通过 MQTT 注入 BDI 输入。
原 USB 输入、电脑端采集和 USB → 网关 → MQTT 链路继续存在。
拔掉 USB 后实验固件可以通过网口发送自己的运行状态，但尚未增加 ESP32 自主 SSH 采集。
实验消息不采用生产 `/report` 数据格式，不会自动出现在现有后端历史/前端；
完整以太网业务替换和后端归档需要后续适配与实机验收。

## 本轮离线检查

只构建，无 upload 目标：

```sh
PLATFORMIO_CORE_DIR="$PWD/.pio-core" ~/.platformio/penv/bin/pio run -e esp32-s3-devkitc-1 -e esp32-s3-eth-mqtt-lab
npm --prefix frontend run build
c++ -std=c++11 test/mqtt_settings.cpp -o /tmp/esp-mqtt-settings-test
/tmp/esp-mqtt-settings-test
```

编译不能代替网线链路、认证、重连、遗嘱和实际数据上报测试。
