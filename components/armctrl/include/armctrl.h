#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t armctrl_init(void);
void armctrl_set_grade(int g);       // 0..4
int  armctrl_get_grade(void);
void armctrl_request_run(bool on);
bool armctrl_is_running(void);

// 低层运动原语(供状态机与联调)。move_ms 运动时间。
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms);   // 解IK+发捆绑帧+等到位
void armctrl_move_servo(int idx, int pwm, int move_ms);               // 单舵机帧+等到位

#ifdef __cplusplus
}
#endif
