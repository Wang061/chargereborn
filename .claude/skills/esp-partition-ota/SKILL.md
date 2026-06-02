---
name: esp-partition-ota
description: 分区表与 OTA/factory/NVS/coredump 布局审查与设计。改 partitions.csv 或上 OTA 时用。
---
# 分区表 / OTA

- **结构**：`partitions.csv` 列 `Name,Type,SubType,Offset,Size`。常见：`nvs`、`phy_init`、`factory`(app) 或 `ota_0/ota_1`+`otadata`、`coredump`(data,coredump)、自定义 `storage`。
- **大小匹配**：app 分区必须 ≥ 固件大小（看 `mcp__idf-bridge__size`）；OTA 双分区各自都要够。改大特性后复核。
- **OTA 布局**：`otadata` 记录当前/回滚槽；启用 app rollback + 版本校验，防变砖。
- **coredump 分区**：保留 `coredump` 分区（menuconfig: Core Dump → Flash），panic 才能 `coredump_summary` 取证。
- **NVS 兼容**：改命名空间/键/结构时提供迁移或默认值，避免读旧数据崩；NVS 满了要处理。
- **偏移/对齐**：app 分区 0x10000 对齐；offset 不重叠不越界。
- **校验**：改完用 `/esp-partition`（parttool 校验）+ build + flash + boot check（rules/verification.md）。

属高风险改动，按 rules/safety.md 显式说明并记 DECISIONS.md。
