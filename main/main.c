#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp.h"
#include "net.h"
#include "camera.h"
#include "ai.h"

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

    if (ai_init() != ESP_OK) {
        ESP_LOGW(TAG, "ai init failed — 推理不可用，继续运行");
    } else {
        ai_result_t air;
        if (ai_selftest_builtin(&air) == ESP_OK) {
            ESP_LOGI(TAG, "ai selftest: n=%d infer=%ums", air.count, air.infer_ms);
            for (int i = 0; i < air.count; i++) {
                ESP_LOGI(TAG, "  #%d %s %.2f [%d,%d,%d,%d]", i,
                         ai_class_name(air.boxes[i].cls), air.boxes[i].score,
                         air.boxes[i].x1, air.boxes[i].y1, air.boxes[i].x2, air.boxes[i].y2);
            }
        } else {
            ESP_LOGW(TAG, "ai selftest failed");
        }
    }

    uint32_t sec = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
