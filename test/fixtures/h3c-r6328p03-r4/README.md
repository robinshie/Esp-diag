来自 r4 实机离线采集的只读 SSH 原文。设备为 H3C S5130S-52S-EI，Comware 7.1.070 Release 6328P03。

Boot：`5f64d2551d3abe0930bee36953b9266e`。同一采集轮次 3：version/interfaces/down/loops/stp/aggregation 对应 bdi-read-21 至 26，cpu 为 bdi-read-28，mac_moves 为 bdi-read-30；所有请求均返回 ok，输出完整至提示符。

MAC 表明确返回 0 条漂移记录，CPU 5 秒值为 14%。本组用于回归真实空表格式、完整 Runtime 的只读探索和无写动作，不是物理环路故障样本。回放重新赋予有效时间戳，不将历史原文当作当前证据。
