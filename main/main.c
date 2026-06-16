#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp.h"
#include "net.h"
#include "camera.h"

static const char *TAG = "main";

void app_main(void)
{
    bsp_print_sysinfo();
    bsp_psram_selftest();

    if (camera_init() != ESP_OK) {
        ESP_LOGW(TAG, "camera init failed — 图传将不可用，继续运行便于排查接线");
    }

    net_softap_start();
    net_http_start();
    net_stream_start();

    uint32_t sec = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
