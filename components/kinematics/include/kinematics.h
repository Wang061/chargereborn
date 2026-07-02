#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 纯逆运动学。无 ESP 依赖，可 host 单测。单位: mm / us / deg。
// 连杆: links[0..3] = L0(底座高) L1(大臂) L2(小臂) L3(腕到爪尖)，mm。

// 存连杆到 out_links（简单拷贝，保留接口对称性）。
void kin_setup(const float link_mm[4], float out_links[4]);

// 单 Alpha(爪-平面夹角,deg)解算 4 舵机 PWM。
// 返回: 0=成功; 1=z过低; 2=超伸展; 3=bbb域外; 4=theta5域外; 5=aaa域外; 6=theta4域外; 7=theta3域外。
int kin_solve(const float links[4], float x, float y, float z, float alpha_deg, int out_pwm[4]);

// 扫 Alpha 0→-135° 取最负可达解(3号舵机与水平最大夹角)。
// 返回: 0=成功(out_pwm 有效); -1=y<0 或全程无解。
int kin_move_best(const float links[4], float x, float y, float z, int out_pwm[4]);

#ifdef __cplusplus
}
#endif
