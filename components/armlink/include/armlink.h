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

/* —— 协议编码（纯字符串，不发送；供 UART sink 或上位机复用）—— */
// $KMS:x,y,z,t! —— Steward(KM1) 自做 IK 的坐标指令。
// 注意：本步无 px->mm 标定，机械域是占位，编码值不可直接驱动真臂。
int armlink_encode_kms(const arm_target_t *t, char *out, size_t n);
// {#004P<pwm>T<ms>!} —— 裸腕舵机角指令；pwm 由角度经占位公式（K 待标定）。
int armlink_encode_wrist_servo(const arm_target_t *t, char *out, size_t n);

#ifdef __cplusplus
}
#endif

/*
 * 启用真实 UART 驱动机械臂的步骤（本步默认全关，安全）：
 *  1. 物理装置就绪 + 填 docs/ai/SAFETY.md（舵机限位/电流/上电序/急停）。
 *  2. menuconfig: armlink → 开 ARMLINK_UART_ENABLE，填 ARMLINK_UART_TX_GPIO
 *     （避开 35/36/37 PSRAM、0/3/45/46 strapping、19/20 USB-JTAG）。
 *  3. 不接动力电/抬空，先用逻辑分析仪或 Steward 回显核对 UART 串正确。
 *  4. 标定 px->mm 与 px角->腕角(K/零位)，填 armlink.c 占位公式，低速接电验证。
 */
