# 端口 2、3 真实 SSH 启停实验

用户于 2026-11-03 明确允许在本台 H3C 交换机的 1/0/2、1/0/3 上进行隔离、恢复实验，包括临时打开已有管理关闭端口。管理口 1/0/1 不参与。两个业务口的原始配置均为 Access VLAN 10、shutdown、undo stp enable、loopback-detection enable vlan 10，原始记录保存在 `test/.runtime/port23-baseline.json` 和 SecureCRT 控制台日志中。

本实验验证 ESP32 通过真实 SSH 下发命令并回读设备状态，使用已实现的 SSH 执行器与 H3C 适配器。实验不是自动故障判断，也不制造真实广播风暴；不会把实验动作伪装成 BDI 检测到了故障。

`esp32-s3-port23-exercise` 是临时实验构建，普通 BDI 自动写授权仍关闭。仅实验控制器能执行固定端口上的有界命令序列。它要求固定目标 192.168.10.2:22、SSH 主机信任通过、精确设备身份、管理口 UP、两个测试口的 Access/VLAN10 基线、无聚合及上联保护冲突、没有当前环路报告，并要求另一个测试口始终保持 ADM。测试开始前持久化一次性标记；重启不自动重放。没有新增任意 CLI、HTTP 写端点或任意端口入口。

每个端口单独执行：临时 undo shutdown → 两轮状态回读 → shutdown → 两轮确认 ADM → undo shutdown → 两轮确认管理启用 → shutdown → 两轮确认回到原始 ADM，然后才开始下一个端口。预计共 8 次端口写命令。每轮回读均包含身份、接口、Down 原因、环路、STP 和聚合信息；两个端口不同时启用。

配置恢复与物理链路 UP 分别记录。如果另一端没有载波，undo shutdown 后为物理 DOWN，只能证明管理启用成功，不能宣称链路或业务恢复。服务记录 `carrier_up_2`、`carrier_up_3`。命令结果不明、身份/拓扑改变或 SSH 中断会停止后续实验并要求独立控制台核查及恢复基线，不会盲目重发命令。

控制器的本机测试启用 ASan/UBSan，覆盖顺序执行、固定端口、原始基线、持久化失败/已有一次性标记、错误目标/机型/聚合、错误回执与断线不重放。运行：

```sh
python3 -m unittest test.test_port_exercise -v
```

实机完成后，必须独立读回两个端口的配置，与原始配置比较，并恢复 `esp32-s3-readonly-acceptance` 固件。实验构建的设备状态显示 `acceptance_read_only=false` 和独立的 `port_exercise` 状态，避免把存在临时写能力的构建标为纯只读。

实验结果另存至 `test/.runtime/port23-live-experiment.json`；此说明不是成功证明，应以最终结果和原始设备记录为准。

2026-11-03 实机结果：交换机审计确认来自 ESP32 192.168.10.1 的 8 次启停命令，独立控制台回读确认 2、3 口的配置与原始基线完全一致。实验控制器终态为 passed、writes=8；两个端口的 carrier_up 均为 false，因此仅验证了真实 SSH 管理启停与基线恢复，未验证物理链路或业务流量恢复。实验后已重新烧录只读验收构建并通过写入校验；HTTP 实机状态确认 r15、acceptance_read_only=true、bdi.enabled=false、ssh_state=connected。

TF 批量采集保存了 23 个后续日志包，但发生 HTTP 超时，未取回实验每一步的 TF 记录；定向历史读取也遇到旧文件不可读。不能据此宣称已归档全部 16 次状态回读。最终恢复烧录之前还观察到一次管理链路 down/up，原因尚未确认。用户随后要求改为手动操作，自动网络切换、采集及命令下发已停止；此后由用户提供日志继续分析。

用户随后明确要求“用 BDI 自动一遍”。新增 `esp32-s3-bdi-port23-exercise`（r17），以独立的 EmbeddedBDI 验收代理执行明确的操作者目标 `verify_port_roundtrip`。代理以当前管理连接、身份、完整新鲜观察、持久化授权、另一端口 ADM 及本端口实际状态为信念，通过四个上下文计划分别选择准备启用、隔离、恢复和还原基线。执行器独立核对计划的动作、端口、周期、目标版本和时间，再调用现有真实 SSH 执行器；每步仍需两轮实际状态回读。验收代理不伪造环路或风暴证据，也不启用普通故障 BDI 的现场写授权。

r17 使用独立 NVS 一次性键 `bdi23-test`，不会删除或复用 r16 的实验标记。任意断线、含糊回执或保护条件变化会停止，不盲目重放；中途失败须用操作者 Console 核查基线。HTTP 状态另外提供 `acceptance_bdi.goal/decisions/scope` 与 `port_exercise` 结果，避免将正常故障策略仍为 disabled 误解成没有执行验收代理。电脑 Wi-Fi 与操作者 Console 不由本轮测试接管。

运行 `python3 -m unittest test.test_bdi_port_exercise test.test_port_exercise -v`；BDI 变体的 17 个安全场景包含真实 Agent 选出的八个语义意图，以及缺失/拒绝/错误端口/错误周期/错误动作的计划、缺少持久化授权和过期观察的拒绝。实机证据保存在 `test/.runtime/r17-bdi-port23-experiment.json`，未取得终态和独立回读前不能标为通过。

r17 设备控制器终态已由操作者提供的两次 HTTP 状态确认：同一 boot `71fc645f3be6aa557decac8fbb2fe862`，先为 decisions=4/writes=4/running，后为 decisions=8/writes=8/passed，reason=both_ports_returned_to_administrative_down。SSH 会话已接受、心跳 RTT=1ms/misses=0；完成后的普通只读观察已恢复，cycle=8，六张表同周期完整新鲜。两个端口 carrier_up 均为 false。此结论标为“设备控制器回读通过”；本轮独立 Console 终态输出与分步 TF 日志尚未提供，不能标为已完成独立核验或物理业务恢复。验收后恢复只读固件，烧录证据另附在结果 JSON 中。
