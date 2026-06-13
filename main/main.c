#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp.h"

static const char *TAG = "main";

void app_main(void)
{
    bsp_print_sysinfo();

    uint32_t sec = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
