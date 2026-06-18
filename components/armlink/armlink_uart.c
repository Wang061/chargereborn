#include "armlink_uart.h"
#include "sdkconfig.h"

#if CONFIG_ARMLINK_UART_ENABLE

#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "armlink_uart";
#define ARMLINK_UART_RX_BUF 256   // 字节；KM1 单向下发，给最小 RX 缓冲

esp_err_t armlink_uart_init(void)
{
    if (CONFIG_ARMLINK_UART_TX_GPIO < 0) {
        ESP_LOGE(TAG, "TX GPIO 未配置(-1)，拒绝初始化（防误驱动机械臂）");
        return ESP_ERR_INVALID_STATE;
    }
    uart_config_t cfg = {
        .baud_rate  = CONFIG_ARMLINK_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_param_config(CONFIG_ARMLINK_UART_PORT_NUM, &cfg);
    if (e != ESP_OK) return e;
    // RX = -1 (UART_PIN_NO_CHANGE) → 仅 TX
    e = uart_set_pin(CONFIG_ARMLINK_UART_PORT_NUM, CONFIG_ARMLINK_UART_TX_GPIO,
                     CONFIG_ARMLINK_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) return e;
    e = uart_driver_install(CONFIG_ARMLINK_UART_PORT_NUM, ARMLINK_UART_RX_BUF, 0, 0, NULL, 0);
    if (e != ESP_OK) return e;
    ESP_LOGW(TAG, "UART%d 启用 tx=%d @%d baud — 会驱动机械臂!",
             CONFIG_ARMLINK_UART_PORT_NUM, CONFIG_ARMLINK_UART_TX_GPIO, CONFIG_ARMLINK_UART_BAUD);
    return ESP_OK;
}

int armlink_uart_send(const char *s, int len)
{
    if (!s) return -1;
    if (len <= 0) len = (int)strlen(s);
    return uart_write_bytes(CONFIG_ARMLINK_UART_PORT_NUM, s, len);
}

#else  /* UART 禁用：空桩，不引 driver/uart 符号，零运行期风险 */

esp_err_t armlink_uart_init(void) { return ESP_OK; }
int armlink_uart_send(const char *s, int len) { (void)s; (void)len; return -1; }

#endif
