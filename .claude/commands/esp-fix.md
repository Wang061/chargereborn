---
description: 读分类后的构建/运行错误，选对应 skill，改最小范围修复。
---
通用修复入口：
1. 取错误来源（最近 `mcp__idf-bridge__build` 的 errors/tail，或 logs/build|panic 最新，或 monitor 输出）。
2. 按类型选 skill：构建错→`esp-idf-build-fix`；崩溃→`esp-monitor-triage`+`esp-panic-backtrace`；堆→`heap-memory-triage`；并发→`freertos-task-design`。
3. 先查 `docs/ai/CRASH_SIGNATURES.md` 是否已有同款。
4. **只改与错误直接相关的最小范围**，按 rules/verification.md 验证（build / flash+boot / monitor）。
5. 修好用 `/learn` 沉淀。复杂构建死循环用 `/esp-loop`。
