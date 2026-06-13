#include "bsp.h"
#include <inttypes.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"

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

bool bsp_psram_selftest(void)
{
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM NOT initialized! check SPIRAM/OCT in sdkconfig");
        return false;
    }
    size_t total = esp_psram_get_size();
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM total=%uB (%uMB) free=%uB",
             (unsigned)total, (unsigned)(total / (1024U * 1024U)), (unsigned)psram_free);

    const size_t test_sz = 1024U * 1024U;   // 1MB
    uint8_t *buf = (uint8_t *)heap_caps_malloc(test_sz, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM malloc %uB failed", (unsigned)test_sz);
        return false;
    }
    for (size_t i = 0; i < test_sz; i += 4096) buf[i] = (uint8_t)(i & 0xFF);
    bool ok = true;
    for (size_t i = 0; i < test_sz; i += 4096) {
        if (buf[i] != (uint8_t)(i & 0xFF)) { ok = false; break; }
    }
    heap_caps_free(buf);
    ESP_LOGI(TAG, "PSRAM alloc+rw test: %s", ok ? "OK" : "FAIL");
    return ok;
}
