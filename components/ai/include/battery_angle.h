#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 结构张量法估计检测框内 18650 电池长轴角度。
// 只用灰度梯度（对包皮颜色/光照鲁棒），不需要 minAreaRect/轮廓/分割。
typedef struct {
    float angle_deg;   // 长轴 vs 图像 +x 轴，范围 [0,180)，图像 y 向下；valid=false 时为 -1
    float anisotropy;  // 角度置信 0..1（结构张量各向异性，越大越可信）
    bool  valid;       // 框太小或全无纹理 → false
} battery_angle_t;

// rgb888     : 整帧 RGB888 像素（行优先，行距 = img_w*3）
// img_w/img_h: 整帧尺寸（像素）
// x1,y1,x2,y2: 检测框（源像素坐标，左上/右下；内部会 clip 到图像内）
battery_angle_t battery_angle_estimate(const uint8_t *rgb888, int img_w, int img_h,
                                       int x1, int y1, int x2, int y2);

#ifdef __cplusplus
}
#endif
