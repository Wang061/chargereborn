---
name: esp-panic-backtrace
description: 把 panic 的 Backtrace 地址解码成 函数+file:line。看到 "Backtrace: 0x.. 0x.." 时用。
---
# Panic 回溯解码

ESP32-S3 panic 会打印形如 `Backtrace: 0x42008a3c:0x3fc99... 0x...`。**不解码不下结论。**

1. **优先 monitor 自动解码**：`ESP_MONITOR_DECODE=1`（已在 settings.env），idf monitor 会自动把回溯标注成 函数+file:line。
2. **core dump（更全）**：若已配 Core Dump→Flash，`mcp__idf-bridge__coredump_summary` 给所有任务栈+寄存器+崩溃原因，比 live 回溯全。
3. **手动 addr2line**（无 monitor 解码时）：用 IDF 工具链
   `xtensa-esp32s3-elf-addr2line -pfiaC -e build/<proj>.elf 0x42008a3c 0x...`
   （工具链在激活后的 PATH 里；可经 idf.ps1 间接环境）。
4. 定位到源码行后，结合 esp-monitor-triage 的签名表判根因（指针/栈/堆/并发）。
5. 用 `build/<proj>.elf`——必须与崩溃固件**同一次构建**，否则地址对不上。

修好后 `/learn` 写 CRASH_SIGNATURES.md。
