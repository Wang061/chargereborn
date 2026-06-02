---
name: sdkconfig-change-review
description: 评估 sdkconfig/menuconfig 改动的风险与连锁影响。改任何 CONFIG_* 前用。
---
# sdkconfig 改动评审

- **源**：用 `sdkconfig.defaults` / `sdkconfig.defaults.esp32s3` 做可复现的源，**生成的 `sdkconfig` 不入 git**（已 gitignore）。改默认值改这两个文件。
- **高风险项**（改前必说明+验证）：
  - target / CPU 频率 / flash 大小·模式·频率 / PSRAM 类型 → build + boot check。
  - 分区表选择、Core Dump 目标、log level、optimization (-Og/-Os/-O2) → 影响大小与可调试性。
  - Wi-Fi/BLE、PM(自动 light sleep)、看门狗(TWDT/IWDT) → 影响功耗/稳定性/共存。
  - Heap poisoning / stack canary → 调试期开、发版可关。
- **连锁**：某选项 `select`/`depends on` 其他选项；改一个可能默认带开/关一串。
- **流程**：改 → `idf reconfigure`/build 看是否触发大重编 → boot check；用 `/esp-menucheck`。
- **记录**：影响启动/分区/功耗/安全的改动写 DECISIONS.md。

不要把临时调试用的 sdkconfig 改动残留进发版。
