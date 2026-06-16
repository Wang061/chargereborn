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

// 后台检测任务：周期抓相机帧→推理→日志。
// 栈 8192：esp-dl run 调用栈较深 + 首帧含模型加载；优先级 3：低于 WiFi(23)/httpd，不抢网络。
static void detect_task(void *arg)
{
    (void)arg;
    ai_result_t r;
    while (1) {
        if (ai_detect_oneshot(&r) == ESP_OK) {
            ESP_LOGI(TAG, "detect: n=%d infer=%ums", r.count, r.infer_ms);
            for (int i = 0; i < r.count; i++) {
                ESP_LOGI(TAG, "  #%d %s %.2f [%d,%d,%d,%d]", i,
                         ai_class_name(r.boxes[i].cls), r.boxes[i].score,
                         r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));   // ~2Hz，留 CPU 给 WiFi/图传
    }
}

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
        xTaskCreate(detect_task, "detect", 8192, NULL, 3, NULL);
    }

    uint32_t sec = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
