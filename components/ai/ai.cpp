#include "ai.h"
#include "battery_angle.h"
#if CONFIG_AI_DETECTOR_BATTERY_YOLO
#include "battery_yolo_detect.hpp"
#elif CONFIG_AI_DETECTOR_BATTERY_DETECT4
#include "espdet4_detect.hpp"
#else
#include "espdet_detect.hpp"
#endif
#include "dl_image_jpeg.hpp"
#include "camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ai";

static dl::detect::Detect *s_det  = nullptr;
static SemaphoreHandle_t s_lock = nullptr;
static ai_result_t       s_last = {};   // 最近一次检测结果缓存（供 /detect 读，避免相机多消费者竞争）

// 调用方持锁；对已 decode 的 img 跑模型并填 out，同时刷新缓存 s_last。
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
        // 角度估计：img 已是解码后 RGB888 且在作用域内，零拷贝；框为源像素坐标
        battery_angle_t a = battery_angle_estimate((const uint8_t *)img.data, img.width, img.height,
                                                   b->x1, b->y1, b->x2, b->y2);
        b->angle_deg  = a.valid ? a.angle_deg : -1.0f;
        b->anisotropy = a.anisotropy;
    }
    s_last = *out;   // 刷新缓存（调用方持锁）
}

extern "C" esp_err_t ai_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
#if CONFIG_AI_DETECTOR_BATTERY_YOLO
    if (!s_det)  s_det  = new BatteryYoloDetect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl YOLOv8n 4-class, lazy)");
#elif CONFIG_AI_DETECTOR_BATTERY_DETECT4
    if (!s_det)  s_det  = new ESPDet4Detect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet-Pico 4-class, lazy)");
#else
    if (!s_det)  s_det  = new ESPDetDetect();   // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet, lazy)");
#endif
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

extern "C" void ai_get_last(ai_result_t *out) {
    if (!out) return;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_last;
    if (s_lock) xSemaphoreGive(s_lock);
}

extern "C" const char *ai_class_name(int cls) {
#if CONFIG_AI_DETECTOR_BATTERY_YOLO || CONFIG_AI_DETECTOR_BATTERY_DETECT4
    // 4 类电池模型, 类别 id 顺序 = 训练集 data.yaml (0:21700 1:18650 2:9V 3:AA)
    // battery_detect4 的 battery4.yaml 沿用同一顺序(NAMES 建集时对齐)
    switch (cls) {
    case 0: return "21700";
    case 1: return "18650";
    case 2: return "9V";
    case 3: return "AA";
    default: return "obj";
    }
#else
    return (cls == 0) ? "18650" : "obj";   // 单类 18650 模型(M5)
#endif
}
