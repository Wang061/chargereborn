#include "armcal.h"
#include <math.h>

void homography_apply(const float H[9], float px, float py, float *mm_x, float *mm_y)
{
    float w = H[6] * px + H[7] * py + H[8];
    if (w == 0.0f) w = 1e-6f;
    if (mm_x) *mm_x = (H[0] * px + H[1] * py + H[2]) / w;
    if (mm_y) *mm_y = (H[3] * px + H[4] * py + H[5]) / w;
}

float homography_angle(const float H[9], float img_angle_deg)
{
    // 方向向量(cos,sin)过 2x2 线性部分,取变换后角度
    float rad = img_angle_deg * (float)M_PI / 180.0f;
    float dx = cosf(rad), dy = sinf(rad);
    float wx = H[0] * dx + H[1] * dy;
    float wy = H[3] * dx + H[4] * dy;
    float a = atan2f(wy, wx) * 180.0f / (float)M_PI;
    while (a >= 180.0f) a -= 180.0f;
    while (a < 0.0f) a += 180.0f;
    return a;
}
