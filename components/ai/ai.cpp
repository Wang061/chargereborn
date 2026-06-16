#include "ai.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ai";

extern "C" esp_err_t ai_init(void) {
    ESP_LOGW(TAG, "ai_init: stub (M1 骨架，未接 esp-dl)");
    return ESP_OK;
}
extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out) {
    (void)jpg; (void)len;
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;   // M2 实现
}
extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
}
extern "C" const char *ai_class_name(int cls) {
    return (cls == 0) ? "cat" : "obj";   // 占位
}
