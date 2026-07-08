#include "voicelink.h"
#include "voicelink_frame.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "voicelink";

#if CONFIG_VOICELINK_ENABLE

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "armctrl.h"

#define VOICELINK_UART_RX_BUF 256   // 字节；单向接收，帧很短(<16B)，给最小缓冲

// 语音是低频离散事件（人说一句话才来一帧），栈 3072：逐字节读 + 两次 strcmp
// 短串比较，无深调用链，留有余量；优先级 3：与 detect_task 同级，不需要抢占
// 摄像头/网络任务。
static void voicelink_task(void *arg)
{
    (void)arg;
    voicelink_frame_state_t st;
    voicelink_frame_reset(&st);
    uint8_t byte;
    while (1) {
        int n = uart_read_bytes(CONFIG_VOICELINK_UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(200));
        if (n <= 0) continue;
        voicelink_cmd_t cmd = voicelink_frame_feed(&st, (char)byte);
        if (cmd == VOICELINK_CMD_START) {
            ESP_LOGI(TAG, "收到 #Start! -> armctrl_request_run(true,true)");
            armctrl_request_run(true, true);
        } else if (cmd == VOICELINK_CMD_STOP) {
            ESP_LOGI(TAG, "收到 #Stop! -> armctrl_request_run(false,...)");
            armctrl_request_run(false, armctrl_is_continuous());
        }
    }
}

esp_err_t voicelink_init(void)
{
    if (CONFIG_VOICELINK_UART_RX_GPIO < 0) {
        ESP_LOGE(TAG, "RX GPIO 未配置(-1)，拒绝初始化（防误触发连续抓取）");
        return ESP_ERR_INVALID_STATE;
    }
    uart_config_t cfg = {
        .baud_rate  = CONFIG_VOICELINK_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_param_config(CONFIG_VOICELINK_UART_PORT_NUM, &cfg);
    if (e != ESP_OK) return e;
    // TX = -1 (UART_PIN_NO_CHANGE)：只收不发
    e = uart_set_pin(CONFIG_VOICELINK_UART_PORT_NUM, UART_PIN_NO_CHANGE,
                     CONFIG_VOICELINK_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) return e;
    e = uart_driver_install(CONFIG_VOICELINK_UART_PORT_NUM, VOICELINK_UART_RX_BUF, 0, 0, NULL, 0);
    if (e != ESP_OK) return e;

    BaseType_t ok = xTaskCreate(voicelink_task, "voicelink", 3072, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "voicelink_task 创建失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "UART%d 启用 rx=%d @%d baud — 语音可直接触发连续抓取!",
             CONFIG_VOICELINK_UART_PORT_NUM, CONFIG_VOICELINK_UART_RX_GPIO, CONFIG_VOICELINK_UART_BAUD);
    return ESP_OK;
}

#else  /* 禁用：空桩，不引 driver/uart 符号，零运行期风险 */

esp_err_t voicelink_init(void) { return ESP_OK; }

#endif
