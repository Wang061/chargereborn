#include "armlink_frame.h"
#include <stdio.h>

#define ARM_PWM_MIN 500
#define ARM_PWM_MAX 2500

int armlink_clamp_pwm(int pwm)
{
    if (pwm < ARM_PWM_MIN) return ARM_PWM_MIN;
    if (pwm > ARM_PWM_MAX) return ARM_PWM_MAX;
    return pwm;
}

int armlink_encode_arm_frame(const int pwm[4], int move_ms, char *out, size_t n)
{
    if (!pwm || !out || n == 0) return -1;
    return snprintf(out, n,
        "{#000P%04dT%04d!#001P%04dT%04d!#002P%04dT%04d!#003P%04dT%04d!}",
        armlink_clamp_pwm(pwm[0]), move_ms,
        armlink_clamp_pwm(pwm[1]), move_ms,
        armlink_clamp_pwm(pwm[2]), move_ms,
        armlink_clamp_pwm(pwm[3]), move_ms);
}

int armlink_encode_servo_frame(int idx, int pwm, int move_ms, char *out, size_t n)
{
    if (!out || n == 0) return -1;
    return snprintf(out, n, "{#%03dP%04dT%04d!}", idx, armlink_clamp_pwm(pwm), move_ms);
}
