#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "ai.h"

#ifdef __cplusplus
extern "C" {
#endif

// 机械臂目标：从检测结果流里,经内建目标跟踪器(target_track)滤波后的"最佳电池"位姿。
typedef struct {
    bool     valid;            // 跟踪器已 confirmed(连续命中数达标)
    bool     stable;           // 已过稳定判据(含滞回); acquire_pose 据此判断可以动臂
    bool     coasting;         // 本帧靠滑行(丢检期间),不可用于开始新动作
    float    center_x_px;      // 滤波后中心 x(640x480 源像素)
    float    center_y_px;      // 滤波后中心 y
    float    angle_deg;        // 滤波后电池长轴角 [0,180)，图像 y 向下
    float    score;            // 最近一次真实关联(非滑行)帧的分数
    float    wrist_deg;        // 机械域占位(NAN,未使用;armctrl 侧经 homography 独立换算腕角)
    uint32_t src_w, src_h;     // 参考帧尺寸
    uint32_t frame_id;         // 单调递增，区分新旧目标
} arm_target_t;

// 初始化 armlink(建缓存锁 + 内建跟踪器; 若 CONFIG_ARMLINK_UART_ENABLE 则起 UART)。重复调用安全。
esp_err_t armlink_init(void);

// 从一次检测结果更新机械臂目标: 逐框喂入内建跟踪器(target_track), 派生 arm_target_t。
// 由 detect_task 每帧调用。线程安全。
void armlink_update_from_ai(const ai_result_t *r);

// 读取最近一次机械臂目标缓存。线程安全。供 /arm_target 与 armctrl::acquire_pose。
void armlink_get_last_target(arm_target_t *out);

// —— 跟踪器生命周期控制(armctrl 状态机调用；均线程安全，内部持同一把锁)——
// 挂起: 抓取序列第一个运动原语起调用,期间不关联/不预测。
void armlink_track_suspend(void);
// 恢复=硬重置: 回观察位停稳后调用(go_observe_ex 内部已自动调用)。
void armlink_track_resume(void);
// 设置连续模式防重抓排除区(px 域,最多 TRACK_MAX_EXCLUSIONS 点)。n=0 清空。
void armlink_set_exclusions(const float pts_px[][2], int n);

#ifdef __cplusplus
}
#endif
