---
description: 发版前全套检查。
---
调用 **esp-release-guardian** subagent：
- `mcp__idf-bridge__build` 绿 + `mcp__idf-bridge__size` 各区余量
- 分区表/OTA/coredump 布局、NVS 兼容、app rollback
- target 锁 esp32s3、版本号、无调试残留、无密钥/大文件入 git
输出红/黄/绿清单 + 阻断项。发版决策记 `docs/ai/DECISIONS.md`。
