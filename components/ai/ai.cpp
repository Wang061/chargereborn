#include "ai.h"
#include "battery_angle.h"
#include "espdet4_detect.hpp"
#include "dl_image_jpeg.hpp"
#include "camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ai";

// esp-dl ESPDetPostProcessor::nms() 是全局(跨类别)贪心 NMS(阈值 espdet4_detect
// 的 nms_thr=0.7)：同一物体若被判成同一类别的多个框，高 IoU 的重复框已经在那一层
// 被杀掉。但实测量化误差会让"同一颗电池"在不同类别通道上各给出一个分数都不低的框
// (例如同时冒出 18650 与 21700 两个框)，这两个框的 IoU 常常不到 0.7、逃过原生 NMS。
// 这里在业务层补一次*跨类别*去重：只处理"不同类别之间"的中等以上重叠，同类别重叠
// 一律不动(交给上面的 0.7 阈值)——这样"挨在一起的两颗同类电池，框有部分重叠"会被
// 保留，"同一颗电池被误判成两个类"才会被合并成一个(留分数更高的类别)。
#define AI_CROSS_CLASS_DEDUP_IOU_THR 0.45f  // 跨类别重复判定的 IoU 阈值(低于原生同类 0.7，因误判框常偏移更多)

static dl::detect::Detect *s_det  = nullptr;
static SemaphoreHandle_t s_lock = nullptr;
static ai_result_t       s_last = {};   // 最近一次检测结果缓存（供 /detect 读，避免相机多消费者竞争）

// 标准 IoU(交并比)：两框均为 [x1,y1,x2,y2]，右下开区间不做 +1 修正(与 ai_box_t 语义一致)。
static float box_iou(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2) {
    int inter_x1 = ax1 > bx1 ? ax1 : bx1;
    int inter_y1 = ay1 > by1 ? ay1 : by1;
    int inter_x2 = ax2 < bx2 ? ax2 : bx2;
    int inter_y2 = ay2 < by2 ? ay2 : by2;
    int inter_w = inter_x2 - inter_x1;
    int inter_h = inter_y2 - inter_y1;
    if (inter_w <= 0 || inter_h <= 0) return 0.0f;
    float inter_area = (float)inter_w * (float)inter_h;
    float a_area = (float)(ax2 - ax1) * (float)(ay2 - ay1);
    float b_area = (float)(bx2 - bx1) * (float)(by2 - by1);
    float union_area = a_area + b_area - inter_area;
    return union_area > 0.0f ? inter_area / union_area : 0.0f;
}

// 调用方持锁；对已 decode 的 img 跑模型并填 out，同时刷新缓存 s_last。
static void run_img(dl::image::img_t &img, ai_result_t *out) {
    int64_t t0 = esp_timer_get_time();
    auto &results = s_det->run(img);   // esp-dl 保证按 score 降序(parse_stage 有序插入,nms 只删不排)
    int64_t t1 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->src_w = img.width; out->src_h = img.height;
    out->infer_ms = (uint32_t)((t1 - t0) / 1000);
    for (auto &r : results) {
        if (out->count >= AI_MAX_BOXES) break;
        // int8 量化致置信度偏低, 降阈值后须过滤边缘假框
        // 拒掉碰画面边框的框(典型: 右上角 9V 假框 [621,0,639,58]):
        int margin = 5;
        if (r.box[0] <= margin || r.box[1] <= margin ||
            r.box[2] >= (int)img.width - margin || r.box[3] >= (int)img.height - margin)
            continue;
        // 跨类别去重：results 按分数降序，out->boxes 里已收录的都是分数更高的框。
        // 若当前框与某个已收录框类别不同、IoU 又超过阈值，视为同一物体的重复判定，丢弃。
        // 类别相同则不比较(同类重叠已由 espdet4_detect 的 nms_thr=0.7 处理，允许挨着的
        // 两颗同类电池部分重叠)。
        bool is_dup = false;
        for (int i = 0; i < out->count; i++) {
            const ai_box_t *prev = &out->boxes[i];
            if (prev->cls == r.category) continue;
            if (box_iou(r.box[0], r.box[1], r.box[2], r.box[3], prev->x1, prev->y1, prev->x2, prev->y2)
                > AI_CROSS_CLASS_DEDUP_IOU_THR) {
                is_dup = true;
                break;
            }
        }
        if (is_dup) continue;
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
    if (!s_det)  s_det  = new ESPDet4Detect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet-Pico 4-class, lazy)");
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
    // 4 类电池模型, 类别 id 顺序 = 训练集 data.yaml (0:21700 1:18650 2:9V 3:AA)
    switch (cls) {
    case 0: return "21700";
    case 1: return "18650";
    case 2: return "9V";
    case 3: return "AA";
    default: return "obj";
    }
}
