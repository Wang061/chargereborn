#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifndef ARMCAL_HOST_TEST
#include "esp_err.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif

// 机械臂标定/参数集（NVS 持久化）。所有 mm/us/deg。
typedef struct {
    float H[9];                 // 单应性 px->台面mm(中轴平面,行主序)
    float link_mm[4];           // L0..L3 卷尺实测(mm)
    float observe_x, observe_y, observe_z;  // 观察位姿(mm)
    int   wrist_center_pwm;     // 腕#004 中位
    float wrist_k;              // 腕 pwm/deg
    int   wrist_zero_deg;       // 腕零位角偏移
    int   gripper_open_pwm, gripper_close_pwm;  // 夹爪#005
    int   gripper_time_ms;
    float pick_z, approach_z, carry_z, place_z; // 抓取高度
    float blade_x, blade_y, blade_safe_z, blade_contact_z, cut_offset_x;
    int   cut_times;
    bool  valid;                // H 已标定?
    uint32_t magic;             // 版本/校验
} armcal_t;

#define ARMCAL_MAGIC 0x41430201u  // 'AC' + ver 02 01

void armcal_defaults(armcal_t *c);
#ifndef ARMCAL_HOST_TEST
esp_err_t armcal_load(armcal_t *out);
esp_err_t armcal_save(const armcal_t *c);
#endif

// 单应性像素->台面mm。H 行主序 9 元。
void homography_apply(const float H[9], float px, float py, float *mm_x, float *mm_y);
// 图像长轴角(deg)->世界角(deg): 用 H 的 2x2 线性部分变换方向向量。
float homography_angle(const float H[9], float img_angle_deg);

#ifdef __cplusplus
}
#endif
