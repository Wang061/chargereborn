#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

int armlink_clamp_pwm(int pwm);
int armlink_encode_arm_frame(const int pwm[4], int move_ms, char *out, size_t n);
int armlink_encode_servo_frame(int idx, int pwm, int move_ms, char *out, size_t n);

#ifdef __cplusplus
}
#endif
