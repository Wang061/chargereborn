#include "armlink.h"
#include "armlink_uart.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "armlink";

// —— 选框 / 编码 常量（命名，注单位）——
#define ARMLINK_CLS_BATTERY       0       // 18650 类别 id（与 ai_class_name 一致）
#define ARMLINK_MIN_ANISOTROPY    0.10f   // 角度置信门限，低于此不取该框角度
// 腕角 -> PWM 占位映射（参考旧 OpenMV pwm=1500+deg*K；K 与零位需实测标定）
#define ARMLINK_WRIST_PWM_MID     1500
#define ARMLINK_WRIST_PWM_PER_DEG 5.6f
#define ARMLINK_WRIST_PWM_MIN     500
#define ARMLINK_WRIST_PWM_MAX     2500
#define ARMLINK_MOVE_TIME_MS      800     // 指令运动时间占位(ms)

static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;

esp_err_t armlink_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_last, 0, sizeof(s_last));
    s_last.wrist_deg = NAN;
    esp_err_t e = armlink_uart_init();   // 禁用时为空桩返回 ESP_OK
#if CONFIG_ARMLINK_UART_ENABLE
    ESP_LOGW(TAG, "init ok — UART sink ENABLED (port=%d tx=%d), 会驱动机械臂!",
             CONFIG_ARMLINK_UART_PORT_NUM, CONFIG_ARMLINK_UART_TX_GPIO);
#else
    ESP_LOGI(TAG, "init ok — UART sink 禁用(仅产出目标, 不驱动机械臂)");
#endif
    return e;
}

void armlink_update_from_ai(const ai_result_t *r)
{
    if (!r || !s_lock) return;

    arm_target_t t;
    memset(&t, 0, sizeof(t));
    t.wrist_deg = NAN;                 // 机械域占位（待标定）
    t.src_w = (uint32_t)r->src_w;
    t.src_h = (uint32_t)r->src_h;

    // 选最佳电池框：cls==battery 且角度可靠，取 score 最高者
    int best = -1;
    float best_score = -1.0f;
    for (int i = 0; i < r->count; i++) {
        const ai_box_t *b = &r->boxes[i];
        if (b->cls != ARMLINK_CLS_BATTERY) continue;
        if (b->angle_deg < 0.0f || b->anisotropy < ARMLINK_MIN_ANISOTROPY) continue;
        if (b->score > best_score) { best_score = b->score; best = i; }
    }
    if (best >= 0) {
        const ai_box_t *b = &r->boxes[best];
        t.valid       = true;
        t.center_x_px = 0.5f * (float)(b->x1 + b->x2);
        t.center_y_px = 0.5f * (float)(b->y1 + b->y2);
        t.angle_deg   = b->angle_deg;
        t.score       = b->score;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    t.frame_id = s_last.frame_id + 1;   // 每次更新递增，消费端可判新旧
    s_last = t;
    xSemaphoreGive(s_lock);

#if CONFIG_ARMLINK_UART_ENABLE
    if (t.valid) {
        char cmd[64];
        int len =
#if CONFIG_ARMLINK_PROTO_WRIST_SERVO
            armlink_encode_wrist_servo(&t, cmd, sizeof(cmd));
#else
            armlink_encode_kms(&t, cmd, sizeof(cmd));
#endif
        if (len > 0) armlink_uart_send(cmd, len);
    }
#endif
}

void armlink_get_last_target(arm_target_t *out)
{
    if (!out) return;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_last;
    if (s_lock) xSemaphoreGive(s_lock);
}

int armlink_encode_kms(const arm_target_t *t, char *out, size_t n)
{
    if (!t || !out || n == 0) return -1;
    // 占位：本步无 px->mm 标定，用像素中心填 x,y（单位错误，仅脚手架）。标定后改真实 mm。
    int x = (int)t->center_x_px;
    int y = (int)t->center_y_px;
    int z = 0;
    return snprintf(out, n, "$KMS:%d,%d,%d,%d!", x, y, z, ARMLINK_MOVE_TIME_MS);
}

int armlink_encode_wrist_servo(const arm_target_t *t, char *out, size_t n)
{
    if (!t || !out || n == 0) return -1;
    // 优先用已标定 wrist_deg；未标定(NAN)则用像素角占位（pwm=1500+deg*K，K 待实测）。
    float deg = isnan(t->wrist_deg) ? t->angle_deg : t->wrist_deg;
    int pwm = (int)(ARMLINK_WRIST_PWM_MID + deg * ARMLINK_WRIST_PWM_PER_DEG);
    if (pwm < ARMLINK_WRIST_PWM_MIN) pwm = ARMLINK_WRIST_PWM_MIN;
    if (pwm > ARMLINK_WRIST_PWM_MAX) pwm = ARMLINK_WRIST_PWM_MAX;
    return snprintf(out, n, "{#004P%04dT%04d!}", pwm, ARMLINK_MOVE_TIME_MS);
}
