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
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_set_grade(int g) { if (g < 0) g = 0; if (g > 4) g = 4; s_grade = g; ESP_LOGW(TAG, "grade=%d", g); }
int  armctrl_get_grade(void) { return s_grade; }
void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
bool armctrl_is_running(void) { return s_run; }
void armctrl_reload_cal(void) { s_cal_dirty = true; }   // 请求空闲时重载 NVS 标定(见 armctrl_task 空闲分支)

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
    if (s_grade == 0) {
        ESP_LOGW(TAG, "[G0-dry] 不发送(grade=0 干跑): %s", frame);
    } else {
        armlink_uart_send(frame, len);
        ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    }
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
    if (len > 0) {
        if (s_grade == 0) {
            ESP_LOGW(TAG, "[G0-dry] 不发送(grade=0 干跑): %s", frame);
        } else {
            armlink_uart_send(frame, len);
            ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}

#define POSE_FRAMES 5
#define POSE_INTERVAL_MS 60
#define POSE_FRESH_TIMEOUT_MS 1500
#define POSE_CENTER_RANGE_PX 4.0f
#define POSE_ANGLE_RANGE_DEG 12.0f

// 连续读 N 帧目标缓存, 抖动超门限判失败, 否则输出中心/角度均值。
static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    float cx[POSE_FRAMES], cy[POSE_FRAMES], ang[POSE_FRAMES];
    uint32_t prev_fid = 0;
    for (int i = 0; i < POSE_FRAMES; i++) {
        arm_target_t t;
        int waited = 0;
        // 第0帧只需有效; 后续帧必须是新检测(frame_id 变化)。每 POSE_INTERVAL_MS 轮询, 超 POSE_FRESH_TIMEOUT_MS 判失败。
        while (1) {
            armlink_get_last_target(&t);
            if (t.valid && (i == 0 || t.frame_id != prev_fid)) break;
            if (waited >= POSE_FRESH_TIMEOUT_MS) {
                ESP_LOGW(TAG, "pose: 第%d帧等新检测超时", i);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(POSE_INTERVAL_MS));
            waited += POSE_INTERVAL_MS;
        }
        prev_fid = t.frame_id;
        cx[i] = t.center_x_px; cy[i] = t.center_y_px; ang[i] = t.angle_deg;
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

// 世界长轴角 -> 腕#004 PWM。抓取时腕轴需对齐电池长轴(垂直于长轴夹取)。
// 注: spec §4.4 的"减去抓取点方位角(底座旋转)"耦合在此折进经验 wrist_zero_deg —
// 与 OpenMV 参考(get_grip_angle_deg 用经验偏移,不显式减方位角)一致,G1 标定 wrist_zero_deg 时一并吸收。
static int wrist_pwm_for_angle(float world_ang_deg)
{
    // 归一化到 [-90,90]
    float a = world_ang_deg + s_cal.wrist_zero_deg;
    while (a >= 90.0f) a -= 180.0f;
    while (a < -90.0f) a += 180.0f;
    int pwm = s_cal.wrist_center_pwm + (int)(a * s_cal.wrist_k);
    return armlink_clamp_pwm(pwm);
}

// 抓取序列: 正上方悬停(可急停) -> 预降 -> 腕对齐 -> 最终下降 -> 夹 -> 抬到carry。
static esp_err_t pick_sequence(float mm_x, float mm_y, float world_ang)
{
    float pre_z = s_cal.pick_z + 20.0f;   // 预抓高度(pick 上方 20mm)
    if (pre_z > s_cal.approach_z) pre_z = s_cal.approach_z;

    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 确保开爪

    // 1. 目标正上方安全高度悬停(G<=2 时此处人可急停确认)
    if (armctrl_move_arm(mm_x, mm_y, s_cal.approach_z, 1500) != ESP_OK) return ESP_FAIL;
    // 2. 腕对齐长轴
    armctrl_move_servo(4, wrist_pwm_for_angle(world_ang), 800);
    // 3. 预降
    if (armctrl_move_arm(mm_x, mm_y, pre_z, 1200) != ESP_OK) return ESP_FAIL;
    // 4. 最终下降到抓取高度
    if (armctrl_move_arm(mm_x, mm_y, s_cal.pick_z, 1400) != ESP_OK) return ESP_FAIL;
    // 5. 夹爪闭合
    armctrl_move_servo(5, s_cal.gripper_close_pwm, s_cal.gripper_time_ms);
    // 6. 抬起到搬运高度
    if (armctrl_move_arm(mm_x, mm_y, s_cal.carry_z, 1400) != ESP_OK) return ESP_FAIL;
    // 7. 腕回中位(搬运姿态)
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 800);
    ESP_LOGI(TAG, "抓取序列完成");
    return ESP_OK;
}

// 切割: 移到刀口安全位 -> 下探接触 -> 往复切 cut_times 次 -> 回安全位。
static esp_err_t cut_sequence(void)
{
    float sx = s_cal.blade_x - s_cal.cut_offset_x;
    float ex = s_cal.blade_x + s_cal.cut_offset_x;
    float by = s_cal.blade_y;
    if (armctrl_move_arm(s_cal.blade_x, by, s_cal.blade_safe_z, 1600) != ESP_OK) return ESP_FAIL;
    if (armctrl_move_arm(sx, by, s_cal.blade_contact_z, 1400) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < s_cal.cut_times; i++) {
        if (armctrl_move_arm(ex, by, s_cal.blade_contact_z, 1000) != ESP_OK) return ESP_FAIL;
        if (i != s_cal.cut_times - 1) {
            if (armctrl_move_arm(sx, by, s_cal.blade_contact_z, 1000) != ESP_OK) return ESP_FAIL;
        }
    }
    if (armctrl_move_arm(s_cal.blade_x, by, s_cal.blade_safe_z, 1600) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "切割完成");
    return ESP_OK;
}

// 放回: 移到放置点上方 -> 下降 -> 开爪 -> 抬起。
static esp_err_t place_back(float mm_x, float mm_y)
{
    if (armctrl_move_arm(mm_x, mm_y, s_cal.carry_z, 1600) != ESP_OK) return ESP_FAIL;
    if (armctrl_move_arm(mm_x, mm_y, s_cal.place_z, 1400) != ESP_OK) return ESP_FAIL;
    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);
    if (armctrl_move_arm(mm_x, mm_y, s_cal.approach_z, 1400) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "放回完成");
    return ESP_OK;
}

// 回观察位(先中转点消回差, 再到观察位), 腕中位。
// open_grip=false 供夹持着电池的错误回退(如切割失败): 不开爪, 保持夹持等人工取回。
static void go_observe_ex(bool open_grip)
{
    if (open_grip) {
        armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 开爪
    }
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 600);                     // 腕中位
    armctrl_move_arm(0, s_cal.observe_y, s_cal.carry_z, 1200);             // 中转(正前高处)
    armctrl_move_arm(s_cal.observe_x, s_cal.observe_y, s_cal.observe_z, 1200);
}

static void go_observe(void)
{
    go_observe_ex(true);
}

// 状态机主体(T9-T11 逐步填); 本任务只做: 未就绪守卫 + 回观察位 + 占位。
static void armctrl_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_run || !s_ik_ok || !s_cal.valid) {
            if (s_cal_dirty) {
                s_cal_dirty = false;
                armcal_load(&s_cal);
                ESP_LOGI(TAG, "标定已重载 valid=%d", s_cal.valid);
            }
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
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        if (s_grade < 4) {
            // G3: 抓起后直接放回原位验证抓取(不切割)
            armctrl_move_arm(mm_x, mm_y, s_cal.place_z, 1400);
            armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);
            go_observe();
            s_run = false;
            continue;
        }
        // G4: 抓起 -> 移刀口切割 -> 放回 -> 回观察位
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            // 先尽力撤到刀口安全高度(失败也继续撤, 不能停在刀口): 返回值有意忽略
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        s_run = false;   // 单轮; 连续分拣是 bonus, 改为 continue 即连续
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
