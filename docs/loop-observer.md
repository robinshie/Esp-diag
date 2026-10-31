# 环路处置观察界面

在设备控制台点击 ESP 设备名称或设备行，弹出该 ESP 的环路处置观察界面；右侧详情也提供入口。
支持交换机/端口事件列表、五阶段进度、事件与请求 ID、原始证据时间线，以及离线提示。

本次仅实现只读前端和证据解析，不包含交换机环路隔离执行器，不会发送关闭端口指令。
当前无环路上报时显示“尚未收到明确的环路事件”，不能据此认定网络没有环路。
“查看流程演示”只在浏览器内展示虚拟设备和虚拟端口，不写数据库、不发布 MQTT、不注入设备。
真实数据与演示数据分开，切回真实数据后恢复当前 ESP 上报。

观察数据来自既有 schema_version=1 的 report.lines，每条沿用原始 at_ms。
API 归档模式只展示已入库且当前已加载的报告（现有 live 查询最近 50 包），定期刷新后按数据库结果重建，
删除的数据库记录不会被浏览器旧缓存复活；MQTT 会话模式保留最多 300 包。
因此弹窗不是完整长期环路审计历史，缺少阶段时明确显示缺少证据。

以下为界面支持的规范化上报约定，尚需采集、固件和执行器按此接入。
厂商原始日志必须先完成可靠的设备、端口和事件 ID 解析，不能假定原样即可进入现有固件。

```text
H3C LOG device=sw-1 port=GE1/0/8 fault=LOOP incident=loop-1 event=LOOP_DETECTED
[BDI][LOOP] device=sw-1 port=GE1/0/8 fault=LOOP incident=loop-1 stage=ASSESS allowed=true
[BDI][CMD] ISOLATE_LOOP_PORT device=sw-1 port=GE1/0/8 fault=LOOP incident=loop-1 request_id=boot-1
[BDI][ACK] device=sw-1 port=GE1/0/8 fault=LOOP incident=loop-1 request_id=boot-1 result=COMMAND_ACCEPTED
[BDI][RECOVERED] device=sw-1 port=GE1/0/8 fault=LOOP incident=loop-1 verified=true
```

- BDI 规则 allowed=false 展示“已阻止”；FAILED/ATTEMPT_FAILED 回执展示执行失败。
- 成功 ACK 只进入“恢复验证”，不能直接标记处理完成。
- 只有同事件明确的 verified=true 恢复证据才进入“处理完成”。
- 回执须匹配设备、端口、事件和已建立的当前请求；旧请求或其他设备回执不会推进状态。
- 普通 LINK_DOWN、RECOVER_INTERFACE、解锁和健康心跳都不是环路证据。
- 已完成事件不会被晚到 ACK 重新变为验证中；新环路应使用新的 incident。

验证：`node --test frontend/test/*.test.js`、`npm --prefix frontend run build`。
