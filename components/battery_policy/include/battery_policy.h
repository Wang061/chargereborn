#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTERY_POLICY_VERTICAL_PICK_Z_MM       7.0f
#define BATTERY_POLICY_HORIZONTAL_PICK_Z_MM     0.0f
#define BATTERY_POLICY_VERTICAL_WRIST_EXTRA_DEG 90.0f
#define BATTERY_POLICY_VERTICAL_CLOSE_PWM       1640
#define BATTERY_POLICY_DEMO_FORCE_DANGER        1

typedef enum {
    BATTERY_GRASP_HORIZONTAL = 0,
    BATTERY_GRASP_VERTICAL = 1,
} battery_grasp_mode_t;

typedef struct {
    battery_grasp_mode_t mode;
    float pick_z_mm;
    float wrist_extra_deg;
    int gripper_close_pwm;
} battery_grasp_policy_t;

typedef enum {
    BATTERY_RISK_NORMAL = 0,
    BATTERY_RISK_DANGEROUS = 1,
    BATTERY_RISK_UNKNOWN = 2,
} battery_risk_level_t;

#define BATTERY_RISK_REASON_NONE               0u
#define BATTERY_RISK_REASON_AI_BAD_AA          (1u << 0)
#define BATTERY_RISK_REASON_SENSORS_UNAVAIL    (1u << 1)
#define BATTERY_RISK_REASON_DEMO_FORCE_DANGER  (1u << 2)

typedef struct {
    battery_risk_level_t level;
    uint32_t reasons;
} battery_risk_result_t;

void battery_grasp_policy_for_class(int cls, int horizontal_close_pwm, battery_grasp_policy_t *out);
void battery_risk_eval_for_class(int cls, battery_risk_result_t *out);
const char *battery_grasp_mode_name(battery_grasp_mode_t mode);
const char *battery_risk_level_name(battery_risk_level_t level);

#ifdef __cplusplus
}
#endif
