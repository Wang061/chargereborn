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
    int   x1, y1, x2, y2; // 框（640x480 源像素坐标，左上/右下；已反映射回源帧）
    float angle_deg;      // 电池长轴 vs 图像 +x 轴，[0,180)，y 向下；<0 = 无效
    float anisotropy;     // 角度置信 0..1（结构张量各向异性，越大越可信）
} ai_box_t;

typedef struct {
    int       count;                 // 命中框数（<=AI_MAX_BOXES）
    ai_box_t  boxes[AI_MAX_BOXES];
    uint32_t  infer_ms;              // 本次推理耗时(ms)
    int       src_w, src_h;          // 推理所用源帧尺寸
    int64_t   capture_ts_us;         // 相机拿到这帧的时刻(esp_timer_get_time(),since-boot us)；
                                      // 供 armlink 目标跟踪器的时间门限用，根治"检测结果落后于
                                      // 臂运动"的陈旧帧污染(见 2026-07-06 晚失败场景)
} ai_result_t;

// 构造检测器（lazy）。重复调用安全。
esp_err_t ai_init(void);
// 对一帧 JPEG 推理，结果写 out。capture_ts_us 由调用方传入(相机取帧时刻)。线程安全。
esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out, int64_t capture_ts_us);
// 便捷：抓一帧相机(JPEG)→detect→归还帧。线程安全。
esp_err_t ai_detect_oneshot(ai_result_t *out);
// 读取最近一次检测结果缓存（由后台任务更新；不触发相机/推理，线程安全）。供 /detect。
void ai_get_last(ai_result_t *out);
// 类别 id → 人类可读名（占位）。
const char *ai_class_name(int cls);

#ifdef __cplusplus
}
#endif
