---
description: 非阻塞抓取串口日志并解码分析（panic/WDT/栈溢出/boot loop）。
---
**绝不同步阻塞等串口**——一律用后台 + 轮询：

1. `mcp__idf-bridge__monitor_start`（一次性诊断可设 `seconds=8~15`；持续观察设 `seconds=0`，记得收尾 stop）。
2. `mcp__idf-bridge__monitor_read`（lines 默认 200）轮询读取最近输出。
3. 分析时关注：`Guru Meditation` / `Task watchdog` / `brownout` / 栈溢出 / 堆损坏 / `assert` / `abort` / boot loop。按 `esp-monitor-triage` 思路（或调该 subagent）定位。
4. 若有 panic 且已配 Core Dump→Flash：`mcp__idf-bridge__coredump_summary` 拿全任务栈+寄存器+原因。
5. 完成后 `mcp__idf-bridge__monitor_stop`。

定位到根因（函数+file:line）后再改，改完回 /esp-build。修好用 /learn 沉淀。
