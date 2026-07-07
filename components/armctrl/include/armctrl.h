#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t armctrl_init(void);
void armctrl_request_run(bool on, bool cont);
bool armctrl_is_running(void);
bool armctrl_is_continuous(void);
void armctrl_reload_cal(void);        // 请求重载 NVS 标定(armctrl 空闲时生效, 免重启; 供 /arm_calib POST 后调用)

// 急停: 立即发送 $DST:0!(唯一真机验证过的急停串,见 CRASH_SIGNATURES.md 2026-07-02)并停止循环,
// 锁存直到 armctrl_clear_estop() 被调用——期间任何 run 请求都会被拒绝。
void armctrl_estop(void);
bool armctrl_is_estopped(void);
void armctrl_clear_estop(void);

// 低层运动原语(供状态机与联调)。move_ms 运动时间。
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms);   // 解IK+发捆绑帧+等到位
void armctrl_move_servo(int idx, int pwm, int move_ms);               // 单舵机帧+等到位

// —— dashboard 事件钩子(队友 WebSocket 集成用,见 docs/ai/DASHBOARD_INTEGRATION.md) ——
#include <stdint.h>

typedef struct {
    uint32_t seq_id;             // 单调递增,每轮+1
    int64_t  t_identified_us;    // acquire_pose 成功(STABLE)时刻
    int64_t  t_picked_us;        // pick_sequence 成功时刻(失败为0)
    int64_t  t_cut_us;           // cut_sequence 成功时刻(未到达/失败为0)
    int64_t  t_placed_us;        // place_back 完成时刻(失败也记录尝试完成的时刻;为0表示未到达)
    bool     ok;                 // 本轮是否完整成功(pick+cut成功且完成place尝试;place_back失败不翻false,与"放回失败非致命"流程一致)
} armctrl_cycle_log_t;

typedef void (*armctrl_event_cb_t)(const armctrl_cycle_log_t *log, void *arg);

// 注册每轮终止(成功或失败提前结束)时的回调; cb=NULL 取消注册。
// 线程安全提示: 回调在 armctrl_task 内直接同步调用,实现必须快速返回、不可阻塞/不可长时间持锁。
void armctrl_set_event_cb(armctrl_event_cb_t cb, void *arg);

// 读取处理计数: total=累计(NVS "stats"命名空间持久化,与 armcal 命名空间完全独立),
// session=本次上电内计数(不持久化)。
void armctrl_get_stats(uint32_t *out_total, uint32_t *out_session);

#ifdef __cplusplus
}
#endif
