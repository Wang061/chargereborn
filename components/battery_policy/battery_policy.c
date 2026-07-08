#include "battery_policy.h"
#include "ai_classes.h"
#include <stddef.h>

static bool is_aa_family(int cls)
{
    return cls == AI_CLASS_AA || cls == AI_CLASS_BAD_AA;
}

void battery_grasp_policy_for_class(int cls, int horizontal_close_pwm, battery_grasp_policy_t *out)
{
    if (out == NULL) return;

    if (is_aa_family(cls)) {
        out->mode = BATTERY_GRASP_HORIZONTAL;
        out->pick_z_mm = BATTERY_POLICY_HORIZONTAL_PICK_Z_MM;
        out->wrist_extra_deg = 0.0f;
        out->gripper_close_pwm = horizontal_close_pwm;
        return;
    }

    out->mode = BATTERY_GRASP_VERTICAL;
    out->pick_z_mm = BATTERY_POLICY_VERTICAL_PICK_Z_MM;
    out->wrist_extra_deg = BATTERY_POLICY_VERTICAL_WRIST_EXTRA_DEG;
    out->gripper_close_pwm = BATTERY_POLICY_VERTICAL_CLOSE_PWM;
}

void battery_risk_eval_for_class(int cls, battery_risk_result_t *out)
{
    if (out == NULL) return;

    out->level = BATTERY_RISK_NORMAL;
    out->reasons = BATTERY_RISK_REASON_SENSORS_UNAVAIL;

    if (cls == AI_CLASS_BAD_AA) {
        out->level = BATTERY_RISK_DANGEROUS;
        out->reasons |= BATTERY_RISK_REASON_AI_BAD_AA;
    }
}

const char *battery_grasp_mode_name(battery_grasp_mode_t mode)
{
    switch (mode) {
    case BATTERY_GRASP_HORIZONTAL: return "horizontal";
    case BATTERY_GRASP_VERTICAL: return "vertical";
    default: return "unknown";
    }
}

const char *battery_risk_level_name(battery_risk_level_t level)
{
    switch (level) {
    case BATTERY_RISK_NORMAL: return "normal";
    case BATTERY_RISK_DANGEROUS: return "dangerous";
    case BATTERY_RISK_UNKNOWN: return "unknown";
    default: return "unknown";
    }
}
