#include "ai.h"
#include "espdet_detect.hpp"
#include "dl_image_jpeg.hpp"
#include "camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ai";

static ESPDetDetect    *s_det  = nullptr;
static SemaphoreHandle_t s_lock = nullptr;

// 调用方持锁；对已 decode 的 img 跑模型并填 out。
static void run_img(dl::image::img_t &img, ai_result_t *out) {
    int64_t t0 = esp_timer_get_time();
    auto &results = s_det->run(img);
    int64_t t1 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->src_w = img.width; out->src_h = img.height;
    out->infer_ms = (uint32_t)((t1 - t0) / 1000);
    for (auto &r : results) {
        if (out->count >= AI_MAX_BOXES) break;
        ai_box_t *b = &out->boxes[out->count++];
        b->cls = r.category; b->score = r.score;
        b->x1 = r.box[0]; b->y1 = r.box[1]; b->x2 = r.box[2]; b->y2 = r.box[3];
    }
}

extern "C" esp_err_t ai_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_det)  s_det  = new ESPDetDetect();   // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet, lazy)");
    return (s_det && s_lock) ? ESP_OK : ESP_ERR_NO_MEM;
}

extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out) {
    if (!s_det || !out) return ESP_ERR_INVALID_STATE;
    dl::image::jpeg_img_t j = {.data = (void *)jpg, .data_len = len};
    dl::image::img_t img = dl::image::sw_decode_jpeg(j, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    run_img(img, out);
    xSemaphoreGive(s_lock);
    heap_caps_free(img.data);   // sw_decode_jpeg 输出必须释放
    return ESP_OK;
}

extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    camera_fb_t *fb = camera_capture();          // 相机直出 JPEG VGA
    if (!fb) { memset(out, 0, sizeof(*out)); return ESP_FAIL; }
    esp_err_t e = ai_detect_jpeg(fb->buf, fb->len, out);
    camera_return(fb);                            // 与 camera_capture 配对，杜绝帧缓冲泄漏
    return e;
}

extern "C" const char *ai_class_name(int cls) {
    return (cls == 0) ? "cat" : "obj";   // 占位（M5 换电池模型时改这张表）
}
