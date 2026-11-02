# 兼容包含目录

此目录保留历史头文件路径和类型别名，具体实现已迁移：

- 标准数据与策略门控：`src/domain/`。
- BDI 策略：`src/bdi/strategies/`。
- 设备采集与动作调度：`src/application/`。
- H3C 命令与解析：`src/adapters/h3c/`。

新增功能请直接使用新模块；Runtime 需显式注入设备适配器、策略组合和现场 Policy。完整接口与开发方式见 [固件策略开发结构](../../docs/firmware-strategies.md)。
