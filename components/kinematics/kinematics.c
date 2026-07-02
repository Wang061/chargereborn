#include "kinematics.h"
#include <math.h>

#define KIN_PI 3.14159265358979f

void kin_setup(const float link_mm[4], float out_links[4])
{
    for (int i = 0; i < 4; i++) out_links[i] = link_mm[i];
}

int kin_solve(const float links[4], float x, float y, float z, float alpha_deg, int out_pwm[4])
{
    float l0 = links[0], l1 = links[1], l2 = links[2], l3 = links[3];
    float theta3, theta4, theta5, theta6;
    float aaa, bbb, ccc, zf;

    // 底座旋转: 弧度->度用 180/pi (绝不用 270/pi)
    theta6 = (x == 0.0f) ? 0.0f : atan2f(x, y) * 180.0f / KIN_PI;

    float yy = sqrtf(x * x + y * y);
    yy = yy - l3 * cosf(alpha_deg * KIN_PI / 180.0f);
    float zz = z - l0 - l3 * sinf(alpha_deg * KIN_PI / 180.0f);
    if (zz < -l0) return 1;
    if (sqrtf(yy * yy + zz * zz) > (l1 + l2)) return 2;

    ccc = acosf(yy / sqrtf(yy * yy + zz * zz));
    bbb = (yy * yy + zz * zz + l1 * l1 - l2 * l2) / (2.0f * l1 * sqrtf(yy * yy + zz * zz));
    if (bbb > 1.0f || bbb < -1.0f) return 3;
    zf = (zz < 0.0f) ? -1.0f : 1.0f;
    theta5 = (ccc * zf + acosf(bbb)) * 180.0f / KIN_PI;
    if (theta5 > 180.0f || theta5 < 0.0f) return 4;

    aaa = -(yy * yy + zz * zz - l1 * l1 - l2 * l2) / (2.0f * l1 * l2);
    if (aaa > 1.0f || aaa < -1.0f) return 5;
    theta4 = 180.0f - acosf(aaa) * 180.0f / KIN_PI;
    if (theta4 > 135.0f || theta4 < -135.0f) return 6;

    theta3 = alpha_deg - theta5 + theta4;
    if (theta3 > 90.0f || theta3 < -90.0f) return 7;

    out_pwm[0] = (int)(1500.0f - 2000.0f * theta6 / 270.0f);
    out_pwm[1] = (int)(1500.0f + 2000.0f * (theta5 - 90.0f) / 270.0f);
    out_pwm[2] = (int)(1500.0f + 2000.0f * theta4 / 270.0f);
    out_pwm[3] = (int)(1500.0f + 2000.0f * theta3 / 270.0f);
    return 0;
}

int kin_move_best(const float links[4], float x, float y, float z, int out_pwm[4])
{
    if (y < 0.0f) return -1;
    int best_alpha = 0, found = 0, tmp[4];
    for (int i = 0; i >= -135; i--) {
        if (kin_solve(links, x, y, z, (float)i, tmp) == 0) {
            if (i < best_alpha) best_alpha = i;
            found = 1;
        }
    }
    if (!found) return -1;
    return kin_solve(links, x, y, z, (float)best_alpha, out_pwm);  // 0
}
