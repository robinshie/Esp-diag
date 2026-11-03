# 日志页的环路风暴标记

页面对已读取的 TF / 后台日志显示两种历史标记：

- **已确认环路 · 风暴风险待核对**：精确识别到 H3C `LPDT_VLAN_LOOPED` 事件，显示设备、VLAN、端口和原文。
- **疑似环路风暴**：除环路原文外，同一时段还存在可关联的 ESP 管理心跳丢失和 SSH 断开。没有广播速率、流量或丢包计数时，不显示“风暴已确认”。

入口是 ESP 日志页的“一键分析最近日志”和历史报告分析；后台共用同一分析组件。标记仅依据当前读取范围，不代表当前仍在发生风暴，不表示自动隔离、物理修复或恢复验证成功。若相关记录不在最近 10 包 / 10 分钟范围内，需要选择相应历史记录；上下文不足时只显示环路事实，不拼接普通断线为风暴。

## 判断边界

`frontend/shared/loop-storm.js` 从正式 `esp_native / ETH / uptime` 包读取精确 H3C LPDT、SHELL_LOGIN、SHELL_LOGOUT 原文。H3C 时钟可能未校时，不能直接把 2013 年日志时间当作真实日期，也不能用“读取日志”的时间当作“发生环路”的时间。

关联先以同一 logbuffer 请求中的登录事件与 ESP 实际 SSH 重连对齐，再校验退出事件与 ESP 断线相差不超过 10 秒；环路发生后 60 秒内须有退出与 lost 心跳，心跳和断线相差不超过 15 秒。目标、管理 IP、boot 和时钟须一致；只有 SSH 中断、旧环路重放、跨设备或跨 boot 的记录不会触发疑似。阈值是页面初始证据规则，不是厂家风暴流量阈值。设备时钟跳变或证据不足时保留“风险待核对”。

所有输出仅用于展示，未接入 BDI 动作门控，也不向 SSH 提交命令。推断由离线页面中的规则执行；没有新增或重新训练 ESP 小模型。现有模型辅助分类仍由 ESP 提供。当前规则只识别已验证的 H3C 格式，其他品牌需补充对应事件适配。

## 实机样本回放与验证

`frontend/test/fixtures/h3c-loop-storm.json` 是本次 r5 真实 H3C 测试的 7 包、53 行精简原文，保留 boot、请求 ID 和时间。它包含 VLAN 10、GE1/0/2、GE1/0/3 的环路、管理失联与重连，回放产生一个“疑似环路风暴”历史标记。原始采集见 `test/.runtime/loop-test-r5-followup-capture.json`，SHA-256 为 `b2ee38cfd708a3b3f5648902c8b2f6922c1b1fbd88361bfbd6321f68864156b0`。

```sh
node --test frontend/test/*.test.js
ESP_STORM_FIXTURE=1 node test/archive_viewer_browser.mjs
```

浏览器测试使用精确固件 HTML、真实日志回放及受控只读 HTTP / 模型接口，检查 390 像素页面、标记、原文与历史范围说明；不等于 r6 在硬件上的运行验收。

机制依据：[H3C 环路检测说明](https://www.h3c.com/en/d_202306/1860071_294551_0.htm)说明二层环路会导致广播、组播或未知单播重复传输；[H3C CPU 故障排查](https://www.h3c.com/en/Support/Resource_Center/EN/Home/Public/00-Public/Technical_Documents/Technology_Literature/Technical_Topics/Excessive_CPU_Usage_Technical-23081/?CHID=1093115)将反复收包、CPU 和网络状态作为进一步排查依据。本页面的疑似标记不是对流量风暴的直接测量。
