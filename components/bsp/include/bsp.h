#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// 启动横幅:打印芯片 / flash / 堆信息
void bsp_print_sysinfo(void);

// PSRAM 运行时自检:已初始化 + 1MB 分配 + 写读回一致 -> 返回 true
bool bsp_psram_selftest(void);

#ifdef __cplusplus
}
#endif
