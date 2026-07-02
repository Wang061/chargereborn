#include "armctrl.h"
#include "armcal.h"
#include "kinematics.h"
#include "armlink_frame.h"
#include "armlink_uart.h"
#include "armlink.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "armctrl";

static armcal_t s_cal;
static volatile int  s_grade = 0;       // 0..4, 默认最保守
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_set_grade(int g) { if (g < 0) g = 0; if (g > 4) g = 4; s_grade = g; ESP_LOGW(TAG, "grade=%d", g); }
int  armctrl_get_grade(void) { return s_grade; }
void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
bool armctrl_is_running(void) { return s_run; }

esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    int pwm[4];
    if (kin_move_best(s_cal.link_mm, x, y, z, pwm) != 0) {
        ESP_LOGE(TAG, "不可达 (%.0f,%.0f,%.0f) — 安全停", x, y, z);
        return ESP_ERR_INVALID_ARG;
    }
    // G1 慢速: 时长 x1.5
    int mt = (s_grade <= 1) ? (move_ms * 3 / 2) : move_ms;
    char frame[96];
    int len = armlink_encode_arm_frame(pwm, mt, frame, sizeof(frame));
    if (len <= 0) return ESP_FAIL;
    armlink_uart_send(frame, len);
    ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    vTaskDelay(pdMS_TO_TICKS(mt + SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "UART 未启用, 忽略 move_arm");
    return ESP_ERR_INVALID_STATE;
#endif
}

void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    char frame[32];
    int len = armlink_encode_servo_frame(idx, pwm, move_ms, frame, sizeof(frame));
    if (len > 0) armlink_uart_send(frame, len);
    ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}

// 回观察位(先中转点消回差, 再到观察位), 腕中位 + 开爪。
static void go_observe(void)
{
    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 开爪
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 600);                     // 腕中位
    armctrl_move_arm(0, s_cal.observe_y, s_cal.carry_z, 1200);             // 中转(正前高处)
    armctrl_move_arm(s_cal.observe_x, s_cal.observe_y, s_cal.observe_z, 1200);
}

// 状态机主体(T9-T11 逐步填); 本任务只做: 未就绪守卫 + 回观察位 + 占位。
static void armctrl_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_run || !s_ik_ok || !s_cal.valid) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        // T9-T11 在此填: 观察→定位→抓→切→放→回观察; 本步先回观察位并停。
        go_observe();
        ESP_LOGI(TAG, "(骨架)已回观察位; 抓取序列待 T9-T11 填");
        s_run = false;   // 骨架跑一次即停
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t armctrl_init(void)
{
    esp_err_t e = armcal_load(&s_cal);   // 无标定则 valid=false
    if (e != ESP_OK) ESP_LOGW(TAG, "标定未就绪(valid=false), 自动模式将被拒绝");
    s_ik_ok = (kin_selftest() == 0);
    if (!s_ik_ok) ESP_LOGE(TAG, "IK 自检失败, 自动模式禁用");
    xTaskCreate(armctrl_task, "armctrl", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "init ok (grade=0,run=off,ik=%d,cal=%d)", s_ik_ok, s_cal.valid);
    return ESP_OK;
}
