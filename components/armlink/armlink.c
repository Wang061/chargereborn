#include "armlink.h"
#include "armlink_uart.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

static const char *TAG = "armlink";

// —— 选框 / 编码 常量（命名，注单位）——
// 18650 类别 id：与 ai_class_name 一致。4 类模型(battery_yolo/battery_detect4)顺序
// 0:21700 1:18650 2:9V 3:AA；单类模型(espdet)只有 cls=0=18650。
// 两套配置下 id 不同，用 Kconfig 分支避免抓错电池种类。
#if CONFIG_AI_DETECTOR_BATTERY_YOLO || CONFIG_AI_DETECTOR_BATTERY_DETECT4
#define ARMLINK_CLS_BATTERY       1
#else
#define ARMLINK_CLS_BATTERY       0
#endif
#define ARMLINK_MIN_ANISOTROPY    0.10f   // 角度置信门限，低于此不取该框角度
// 腕角 -> PWM 占位映射（参考旧 OpenMV pwm=1500+deg*K；K 与零位需实测标定）
#define ARMLINK_WRIST_PWM_MID     1500
#define ARMLINK_WRIST_PWM_PER_DEG 5.6f
#define ARMLINK_WRIST_PWM_MIN     500
#define ARMLINK_WRIST_PWM_MAX     2500
#define ARMLINK_MOVE_TIME_MS      800     // 指令运动时间占位(ms)

// 测试帧改走裸协议：真机固件的 $KMS: 自解算未实现（sscanf 不匹配，COM4 直连实测确认，
// 见 docs/ai/CRASH_SIGNATURES.md）；裸腕舵机(#004) 帧经同一实测验证可动。
#define ARMLINK_TEST_SERVO_IDX    4      // 腕舵机(#004)，单路低扭矩，测试最安全
#define ARMLINK_TEST_PWM_MID      1500   // 中位 (us)
#define ARMLINK_TEST_PWM_SWING    150    // 摆幅 (us, ~20°)，远在 [500,2500] 限位内
#define ARMLINK_TEST_STEP_MS      600    // 单步运动时间 (ms)
#define ARMLINK_TEST_SETTLE_MS    700    // 步间等待 (ms)，>= STEP_MS 保证到位，且避开 KM1 uart_get_ok 未清时的丢帧窗口

static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;
static volatile bool     s_auto_send = false;   // 运行时自动发送开关（默认关，防首次上电乱驱动臂）

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

    // 驱动权移交 armctrl 状态机(Phase2): 此处只更新目标缓存, 不再直接发帧。
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

void armlink_set_auto_send(bool on)
{
    s_auto_send = on;
    ESP_LOGW(TAG, "auto_send -> %s", on ? "ON(会自动驱动机械臂!)" : "OFF");
}

bool armlink_get_auto_send(void)
{
    return s_auto_send;
}

#if CONFIG_ARMLINK_UART_ENABLE
// 发一帧裸腕舵机指令 {#idxPppppTtttt!}；armlink_send_test_frame 内部小工具。
static esp_err_t armlink_send_wrist_pwm(int pwm_us)
{
    char cmd[32];
    int len = snprintf(cmd, sizeof(cmd), "{#%03dP%04dT%04d!}",
                        ARMLINK_TEST_SERVO_IDX, pwm_us, ARMLINK_TEST_STEP_MS);
    if (len <= 0) return ESP_FAIL;
    int w = armlink_uart_send(cmd, len);
    ESP_LOGI(TAG, "test frame sent (%d B): %s", w, cmd);
    return (w == len) ? ESP_OK : ESP_FAIL;
}
#endif

esp_err_t armlink_send_test_frame(void)
{
#if CONFIG_ARMLINK_UART_ENABLE
    // 真机固件的 $KMS: 自解算未实现（COM4 直连实测：sscanf 不匹配，两端都无回应）；
    // 改发裸协议已验证可动的序列：腕舵机(#004) 中位 -> 小幅摆 -> 回中位。
    // 阻塞说明：本函数由 /arm_test HTTP handler 同步调用，含 ~1.4s vTaskDelay，
    // 会占用该 httpd worker 线程 ~2s；仅供手动点按联调，不适合高频/自动路径。
    esp_err_t e;

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ARMLINK_TEST_SETTLE_MS));

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID - ARMLINK_TEST_PWM_SWING);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ARMLINK_TEST_SETTLE_MS));

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID);
    if (e != ESP_OK) return e;

    return ESP_OK;
#else
    ESP_LOGW(TAG, "test frame 请求被忽略: CONFIG_ARMLINK_UART_ENABLE 未开");
    return ESP_ERR_INVALID_STATE;
#endif
}
