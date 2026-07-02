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

#define POSE_FRAMES 5
#define POSE_INTERVAL_MS 60
#define POSE_CENTER_RANGE_PX 4.0f
#define POSE_ANGLE_RANGE_DEG 12.0f

// 连续读 N 帧目标缓存, 抖动超门限判失败, 否则输出中心/角度均值。
static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    float cx[POSE_FRAMES], cy[POSE_FRAMES], ang[POSE_FRAMES];
    for (int i = 0; i < POSE_FRAMES; i++) {
        arm_target_t t;
        armlink_get_last_target(&t);
        if (!t.valid) { ESP_LOGW(TAG, "pose: 第%d帧无目标", i); return false; }
        cx[i] = t.center_x_px; cy[i] = t.center_y_px; ang[i] = t.angle_deg;
        vTaskDelay(pdMS_TO_TICKS(POSE_INTERVAL_MS));
    }
    float minx = cx[0], maxx = cx[0], miny = cy[0], maxy = cy[0];
    float mina = ang[0], maxa = ang[0], sx = 0, sy = 0, sa = 0;
    for (int i = 0; i < POSE_FRAMES; i++) {
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
        if (ang[i] < mina) mina = ang[i];
        if (ang[i] > maxa) maxa = ang[i];
        sx += cx[i]; sy += cy[i]; sa += ang[i];
    }
    if ((maxx - minx) > POSE_CENTER_RANGE_PX || (maxy - miny) > POSE_CENTER_RANGE_PX) {
        ESP_LOGW(TAG, "pose: 中心抖动 %.1f,%.1f", maxx - minx, maxy - miny); return false;
    }
    if ((maxa - mina) > POSE_ANGLE_RANGE_DEG) {
        ESP_LOGW(TAG, "pose: 角度抖动 %.1f", maxa - mina); return false;
    }
    *out_px = sx / POSE_FRAMES; *out_py = sy / POSE_FRAMES; *out_ang = sa / POSE_FRAMES;
    ESP_LOGI(TAG, "pose ok px=%.1f py=%.1f ang=%.1f", *out_px, *out_py, *out_ang);
    return true;
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
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        // T10 在此填抓取序列; 本步先打印定位并停。
        s_run = false;
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
