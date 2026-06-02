---
description: 检查 partitions.csv 与 OTA/factory/NVS/coredump 布局。
---
按 `esp-partition-ota` skill：
1. 读 `partitions.csv`，核对各分区 offset/size 不重叠、对齐（app 0x10000）。
2. app 分区 ≥ 固件大小（对照 `mcp__idf-bridge__size`）；OTA 则 ota_0/ota_1 各自够、有 otadata。
3. 确认保留 `coredump` 分区（否则 panic 无法取证）。
4. NVS 改动有迁移/默认值，兼容旧数据。
5. 校验 + build + flash + boot check（rules/verification.md）。属高风险，记 DECISIONS.md。
