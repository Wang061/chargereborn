#include "battery_angle.h"
#include <math.h>
#include <stddef.h>

// —— 调参（命名常量，注单位；真实场景按 overlay 目视微调）——
#define BA_MIN_BOX_PX           8       // 框任一边 < 此(px) → 不可信
#define BA_MAX_SAMPLES_PER_AXIS 64      // 每轴最多采样点（令耗时与框大小解耦）
#define BA_ROI_SHRINK           0.85f   // 只在框中心此比例区域累加，避开框边背景过渡
#define BA_PI                   3.14159265f

// 即时灰度：(R*38 + G*75 + B*15) >> 7 ≈ Rec.601 luma（通道次序不影响角度结果）
static inline int ba_gray(const uint8_t *p)
{
    return (p[0] * 38 + p[1] * 75 + p[2] * 15) >> 7;
}

battery_angle_t battery_angle_estimate(const uint8_t *rgb888, int img_w, int img_h,
                                       int x1, int y1, int x2, int y2)
{
    battery_angle_t out = { -1.0f, 0.0f, false };
    if (!rgb888 || img_w < 3 || img_h < 3) return out;

    // clip 框到图像内
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > img_w) x2 = img_w;
    if (y2 > img_h) y2 = img_h;
    if (x2 - x1 < BA_MIN_BOX_PX || y2 - y1 < BA_MIN_BOX_PX) return out;

    // ROI 收缩到框中心 BA_ROI_SHRINK 区域
    int cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
    int hw = (int)((x2 - x1) * BA_ROI_SHRINK * 0.5f);
    int hh = (int)((y2 - y1) * BA_ROI_SHRINK * 0.5f);
    int rx1 = cx - hw, ry1 = cy - hh, rx2 = cx + hw, ry2 = cy + hh;
    // 中心差分需 ±1 邻域，整体留 1px 边界
    if (rx1 < 1) rx1 = 1;
    if (ry1 < 1) ry1 = 1;
    if (rx2 > img_w - 1) rx2 = img_w - 1;
    if (ry2 > img_h - 1) ry2 = img_h - 1;
    if (rx2 - rx1 < 2 || ry2 - ry1 < 2) return out;

    // 下采样步长：令每轴采样点 ≤ BA_MAX_SAMPLES_PER_AXIS（恒定耗时 ~1-2ms）
    int sx = (rx2 - rx1 + BA_MAX_SAMPLES_PER_AXIS - 1) / BA_MAX_SAMPLES_PER_AXIS;
    int sy = (ry2 - ry1 + BA_MAX_SAMPLES_PER_AXIS - 1) / BA_MAX_SAMPLES_PER_AXIS;
    if (sx < 1) sx = 1;
    if (sy < 1) sy = 1;

    const int stride = img_w * 3;
    int64_t Jxx = 0, Jyy = 0, Jxy = 0;
    int n = 0;
    for (int y = ry1; y < ry2; y += sy) {
        const uint8_t *row = rgb888 + (size_t)y * stride;
        for (int x = rx1; x < rx2; x += sx) {
            const uint8_t *p = row + (size_t)x * 3;
            int gx = ba_gray(p + 3)      - ba_gray(p - 3);       // ∂I/∂x（中心差分）
            int gy = ba_gray(p + stride) - ba_gray(p - stride);  // ∂I/∂y
            Jxx += (int64_t)gx * gx;
            Jyy += (int64_t)gy * gy;
            Jxy += (int64_t)gx * gy;
            n++;
        }
    }
    if (n < 4 || (Jxx + Jyy) == 0) return out;   // 无纹理 → 无效

    // 结构张量主方向（梯度最强方向）= 0.5*atan2(2*Jxy, Jxx-Jyy)，∈ [-pi/2, pi/2]
    float g_dir = 0.5f * atan2f((float)(2 * Jxy), (float)(Jxx - Jyy));
    // 电池长轴 = 梯度能量最弱方向 = 梯度主方向 + 90°
    float deg = (g_dir + BA_PI * 0.5f) * 180.0f / BA_PI;
    deg = fmodf(deg, 180.0f);
    if (deg < 0.0f) deg += 180.0f;

    // 各向异性（角度置信）= (λ1-λ2)/(λ1+λ2) = d/mean ∈ [0,1]
    double mean = 0.5 * ((double)Jxx + (double)Jyy);
    double diff = 0.5 * ((double)Jxx - (double)Jyy);
    double d    = sqrt(diff * diff + (double)Jxy * (double)Jxy);

    out.angle_deg  = deg;
    out.anisotropy = (mean > 1e-6) ? (float)(d / mean) : 0.0f;
    out.valid      = true;
    return out;
}
