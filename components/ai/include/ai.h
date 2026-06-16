#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define AI_MAX_BOXES 10

typedef struct {
    int   cls;            // 类别 id
    float score;          // 置信度 0..1
    int   x1, y1, x2, y2; // 框（模型输入坐标系，左上/右下，像素）
} ai_box_t;

typedef struct {
    int       count;                 // 命中框数（<=AI_MAX_BOXES）
    ai_box_t  boxes[AI_MAX_BOXES];
    uint32_t  infer_ms;              // 本次推理耗时(ms)
    int       src_w, src_h;          // 推理所用源帧尺寸
} ai_result_t;

// 构造检测器（lazy）。重复调用安全。
esp_err_t ai_init(void);
// 对一帧 JPEG 推理，结果写 out。线程安全。
esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out);
// 便捷：抓一帧相机(JPEG)→detect→归还帧。线程安全。
esp_err_t ai_detect_oneshot(ai_result_t *out);
// 类别 id → 人类可读名（占位）。
const char *ai_class_name(int cls);

// M2 临时自检：对内置测试 jpg 推理（M3 接相机后可删）。
esp_err_t ai_selftest_builtin(ai_result_t *out);

#ifdef __cplusplus
}
#endif
