#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 UART（仅 CONFIG_ARMLINK_UART_ENABLE 时实际配置硬件；否则空桩返回 ESP_OK）。
esp_err_t armlink_uart_init(void);
// 发送字符串（仅启用时实际写；否则空桩返回 -1）。返回写出字节数或 <0。
int armlink_uart_send(const char *s, int len);

#ifdef __cplusplus
}
#endif
