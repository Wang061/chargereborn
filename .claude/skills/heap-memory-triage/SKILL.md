---
name: heap-memory-triage
description: 堆/内存问题排查——泄漏、碎片、越界、heap corruption、PSRAM 分配。内存吃紧或 CORRUPT HEAP 时用。
---
# 堆/内存排查

- **看余量**：`esp_get_free_heap_size()` / `heap_caps_get_free_size(MALLOC_CAP_8BIT/INTERNAL/SPIRAM)`；启动横幅打印 min free heap，观察是否单调下降（泄漏）。
- **泄漏定位**：`heap_caps_print_heap_info`；menuconfig 开 **Heap Tracing**（`heap_trace_start/stop/dump`）抓未释放分配点。
- **越界/UAF（CORRUPT HEAP）**：menuconfig 开 **Heap Poisoning（Comprehensive）** + assert on corruption，崩溃点更靠近真凶；用 canary 区分越界写。
- **碎片**：长期运行后大块分配失败但总余量够 → 碎片；改用内存池/预分配/减少频繁大小变动的 malloc。
- **PSRAM**：大缓冲放 SPIRAM（`heap_caps_malloc(.., MALLOC_CAP_SPIRAM)` 或 menuconfig 允许 malloc 走 PSRAM）；注意 DMA 缓冲需 `MALLOC_CAP_DMA`（内部 RAM）。
- **栈 vs 堆**：栈溢出看 freertos-task-design / esp-monitor-triage，不同于堆问题。

修好把签名写 CRASH_SIGNATURES.md（标签 #heap）。
