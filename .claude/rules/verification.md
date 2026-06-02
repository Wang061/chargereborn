# Rule: 验证（改完必须验证到位）

| 改动类型 | 必做验证 |
|---|---|
| 改功能代码 | `mcp__idf-bridge__build` 通过（绿） |
| 改启动路径 / bootloader / app_main 早期 | build + `flash` + boot check（monitor 看是否正常启动、无 boot loop） |
| 改并发 / 任务 / ISR / 中断 | build + flash + `monitor` 验证（看 WDT/栈溢出/死锁迹象） |
| 改 sdkconfig / Kconfig | `/esp-menucheck` + build |
| 改 partitions.csv / OTA / NVS | `/esp-partition` layout 校验 + build |
| 改时钟 / 功耗 / flash / PSRAM | build + boot check（必要时测电流） |

## 原则
- "改了就 build"，不留未编译的改动。
- panic 必须解码回溯（idf monitor addr2line / coredump_summary）定位到 函数+file:line 再改。
- 失败→归类→选 skill→改最小范围→重建→重验，循环；用 `/esp-loop` 自动迭代到绿。
- 通过后 `/release-check`；修好的 bug 用 `/learn` 沉淀。
- 不把"看起来应该行"当成"验证过"——以 build/boot/monitor 实测为准。
