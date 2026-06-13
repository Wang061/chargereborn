#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// 启动横幅:打印芯片 / flash / 堆信息
void bsp_print_sysinfo(void);

#ifdef __cplusplus
}
#endif
