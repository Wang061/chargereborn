#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t armctrl_init(void);
void armctrl_request_run(bool on);
bool armctrl_is_running(void);
void armctrl_reload_cal(void);        // 请求重载 NVS 标定(armctrl 空闲时生效, 免重启; 供 /arm_calib POST 后调用)

// 急停: 立即发送 $DST:0!(唯一真机验证过的急停串,见 CRASH_SIGNATURES.md 2026-07-02)并停止循环,
// 锁存直到 armctrl_clear_estop() 被调用——期间任何 run 请求都会被拒绝。
void armctrl_estop(void);
bool armctrl_is_estopped(void);
void armctrl_clear_estop(void);

// 低层运动原语(供状态机与联调)。move_ms 运动时间。
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms);   // 解IK+发捆绑帧+等到位
void armctrl_move_servo(int idx, int pwm, int move_ms);               // 单舵机帧+等到位

#ifdef __cplusplus
}
#endif
