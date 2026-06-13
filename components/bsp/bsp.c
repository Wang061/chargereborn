#include "bsp.h"
#include <inttypes.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

static const char *TAG = "bsp";

void bsp_print_sysinfo(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "==== ChargeReborn Brain boot ====");
    ESP_LOGI(TAG, "chip=%s cores=%d rev=v%d.%d",
             CONFIG_IDF_TARGET, info.cores,
             info.revision / 100, info.revision % 100);
    ESP_LOGI(TAG, "flash=%" PRIu32 "MB", flash_size / (1024U * 1024U));
    ESP_LOGI(TAG, "heap(internal) free=%" PRIu32 "B",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
