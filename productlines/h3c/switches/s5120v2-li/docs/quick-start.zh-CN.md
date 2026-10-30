# H3C S5120V2-LI 快速使用说明

## 1. 确认设备与版本

S5120V2-LI 是 Comware 7 固定端口交换机。接入真实设备后，先执行只读命令：

```text
display version
display device
display device manuinfo
```

保存软件版本，但提交样本前必须删除序列号、MAC 地址、管理地址和站点名称。

## 2. Console 连接

使用交换机的 CONSOLE 接口及匹配的 Console 线缆，不是 ESP32 的 USB 口。
华三安装指南给出的终端参数为：

```text
波特率：9600
数据位：8
停止位：1
奇偶校验：无
流量控制：无
```

macOS 找到串口后可使用终端工具连接，例如：

```bash
ls /dev/cu.*
screen /dev/cu.usbserial-XXXX 9600
```

退出 `screen`：依次按 `Ctrl+A`、`K`，然后确认退出。

## 3. 读取真实日志

查看日志缓冲区概要和最新日志：

```text
display logbuffer summary
display logbuffer reverse
```

只看告警等级及以上日志：

```text
display logbuffer reverse level warnings
```

通过 SSH/Telnet 的 VTY 终端观察实时输出时：

```text
terminal monitor
terminal logging level informational
```

完整的只读巡检命令位于 `../commands/read-only.txt`。

## 4. 日志格式

Console、日志缓冲区和日志文件通常使用：

```text
%Oct 30 11:01:15:304 2026 ACCESS-SW-01 IFNET/3/PHY_UPDOWN: Physical state on the GigabitEthernet1/0/1 changed to down.
```

关键字段：

- `ACCESS-SW-01`：设备名称。
- `IFNET`：产生日志的模块。
- `3`：严重度，数值越小越严重。
- `PHY_UPDOWN`：稳定的消息标识，解析器应优先使用它。
- 冒号后内容：带接口名、状态等变量的正文。

## 5. 转发到日志网关

先确认交换机管理网络可以到达日志网关，再审查
`../commands/syslog-template.txt`。该文件会修改设备配置，不应在未知网络中直接
粘贴。Syslog 默认使用 UDP 514；后续 ESP 项目应由独立网关接收 Syslog，再转成
USB/以太网输入供 BDI 处理。

## 6. 与现有服务器模拟器的关系

当前 `test/h3c_server_sim.py` 模拟的是 UniServer/HDM 服务器日志。S5120V2-LI
使用 Comware 日志，两者字段和动作语义不同，因此暂时只新增产品资料与测试样本，
不替换现有服务器模拟器。后续应在本产品目录的 `parser/` 和 `simulator/` 下添加
交换机专用实现。
