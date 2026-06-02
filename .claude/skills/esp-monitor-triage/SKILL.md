---
name: esp-monitor-triage
description: 串口日志/崩溃分类速查（Guru/WDT/brownout/栈/堆/boot loop）。看 monitor 输出或 panic 时用。
---
# 串口/崩溃签名速查

非阻塞取日志：`monitor_start`(seconds=8~15) → `monitor_read`。有 core dump 用 `coredump_summary`。

| 签名关键字 | 含义 | 首查方向 |
|---|---|---|
| Guru Meditation: LoadProhibited/StoreProhibited | 空/野指针、越界读写 | 解码回溯到 file:line，看该处指针 |
| Guru Meditation: InstrFetchProhibited | 跳到非法地址 | 函数指针/回调被破坏、栈坏 |
| Task watchdog got triggered | 任务长期不让出 | 忙等/死循环/不当阻塞，加 delay/事件驱动 |
| Interrupt wdt timeout | ISR 太久/关中断太久 | 精简 ISR、缩临界区 |
| ***ERROR*** A stack overflow / canary | 任务栈不足 | 调大栈并记理由 |
| CORRUPT HEAP / heap poisoning | 越界写/重复 free/UAF | 开 heap poisoning 定位，查最近内存改动 |
| Brownout detector | 供电不足/瞬时大电流 | 查电源预算、舵机峰值（SAFETY.md）|
| rst:0x... boot loop | 启动早期崩 | 分区/bootloader/早期初始化 |

**铁律**：先把回溯解码到 函数+file:line 再下结论。先查 `docs/ai/CRASH_SIGNATURES.md` 有无同款。修好用 `/learn` 沉淀。
