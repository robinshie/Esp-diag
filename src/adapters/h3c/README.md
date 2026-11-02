# H3C 设备适配器

正式实现为 `DeviceAdapter`，负责 S5130S-52S-EI / R6328P03 的固定查询、解析、提示符和动作翻译。授权与通用 BDI 策略分别在 `domain/`、`bdi/strategies/`，调度在 `application/`。详见 [固件策略开发](../../../docs/firmware-strategies.md)。

解析器的正常、未知、截断和冲突行为由 `test/h3c_bdi.cpp` 验证，真实样本在 `test/fixtures/h3c-r6328p03/` 和 `test/fixtures/h3c-r6328p03-r4/`。其他型号或版本不会获得自动写动作。
