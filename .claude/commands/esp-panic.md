---
description: 分析 panic / backtrace / core dump，定位到 函数+file:line。
---
1. 若已配 Core Dump→Flash：`mcp__idf-bridge__coredump_summary` 取全任务栈+寄存器+原因（最全）。
2. 否则从 monitor 输出取 Backtrace，按 `esp-panic-backtrace` 解码（monitor 自动 addr2line 或手动 `xtensa-esp32s3-elf-addr2line -pfiaC -e build/<proj>.elf <addrs>`）。**必须用同一次构建的 elf。**
3. 按 `esp-monitor-triage` 签名表判根因（指针/栈/堆/并发/brownout/boot loop）。
4. 先查 `docs/ai/CRASH_SIGNATURES.md`。给最小修复方向 → 改 → /esp-build → 复现验证。
5. `/learn` 写入签名→根因→修法。
