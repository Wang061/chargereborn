#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "ai.h"

#ifdef __cplusplus
extern "C" {
#endif

// 机械臂目标：从一次检测结果里选出的"最佳电池"位姿。
// 像素域字段已可直接用；机械域(wrist_deg / mm)是占位，需物理标定后填（见文件末启用步骤）。
typedef struct {
    bool     valid;            // 是否有可用目标（检测到电池且角度可靠）
    /* —— 像素域（已可用）—— */
    float    center_x_px;      // 最佳框中心 x（640x480 源像素）
    float    center_y_px;      // 最佳框中心 y
    float    angle_deg;        // 电池长轴角 [0,180)，图像 y 向下
    float    score;            // 该框置信度
    /* —— 机械域（本步占位，NAN；需 px->mm / px角->腕角 标定）—— */
    float    wrist_deg;        // 腕舵机角，占位 NAN（待标定）
    uint32_t src_w, src_h;     // 参考帧尺寸（消费端换算/校验）
    uint32_t frame_id;         // 单调递增，区分新旧目标
} arm_target_t;

// 初始化 armlink（建缓存锁；若 CONFIG_ARMLINK_UART_ENABLE 则起 UART）。重复调用安全。
esp_err_t armlink_init(void);

// 从一次检测结果更新机械臂目标（选最佳电池框 + 填像素域 + 占位机械域）。
// 由 detect_task 每帧调用。线程安全。UART 启用时会编码并发送（默认不启用）。
void armlink_update_from_ai(const ai_result_t *r);

// 读取最近一次机械臂目标缓存。线程安全。供 /arm_target。
void armlink_get_last_target(arm_target_t *out);

#ifdef __cplusplus
}
#endif
