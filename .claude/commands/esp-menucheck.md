---
description: 评估 sdkconfig/menuconfig 改动的风险与连锁影响。
---
按 `sdkconfig-change-review` skill：
1. 对比改了哪些 `CONFIG_*`（改 `sdkconfig.defaults*`，非生成的 `sdkconfig`）。
2. 标出高风险项（target/时钟/flash/PSRAM/分区/PM/看门狗/WiFi-BLE/优化级）及连锁 select/depends。
3. 说明影响 → `mcp__idf-bridge__build`（必要时 reconfigure）→ boot check。
4. 锁定仍为 esp32s3；影响启动/分区/功耗/安全的改动记入 `docs/ai/DECISIONS.md`。
5. 清除临时调试残留，别带进发版。
