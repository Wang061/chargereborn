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
#include "esp_timer.h"
#include "nvs.h"
#include <math.h>
#include <inttypes.h>

static const char *TAG = "armctrl";

static armcal_t s_cal;
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)
static volatile bool s_estop = false;   // 急停锁存; 置位后拒绝任何新 run,且原语层直接短路不发字节

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

static volatile bool s_continuous = false;
static float s_processed_px[8][2];   // 连续模式防重抓: 本次运行会话已处理目标的px中心
static int   s_processed_count = 0;
static int   s_acquire_fail_streak = 0;
static volatile bool s_holding = false;  // 爪中疑似有物(pick成功置位,place尝试完成清零);急停/切割失败恢复后的run入口据此决定不开爪

// —— dashboard 统计: 独立 NVS 命名空间"stats",与 armcal 完全隔离,不影响标定数据 ——
#define STATS_NS  "stats"
#define STATS_KEY "cnt"
static uint32_t s_stats_total = 0;
static uint32_t s_stats_session = 0;
static armctrl_event_cb_t s_event_cb = NULL;
static void *s_event_cb_arg = NULL;
static uint32_t s_cycle_seq = 0;

static void stats_load(void)
{
    nvs_handle_t h;
    if (nvs_open(STATS_NS, NVS_READONLY, &h) != ESP_OK) { s_stats_total = 0; return; }
    uint32_t v = 0;
    size_t sz = sizeof(v);
    if (nvs_get_blob(h, STATS_KEY, &v, &sz) == ESP_OK && sz == sizeof(v)) s_stats_total = v;
    else s_stats_total = 0;
    nvs_close(h);
}

static void stats_save(void)
{
    nvs_handle_t h;
    if (nvs_open(STATS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, STATS_KEY, &s_stats_total, sizeof(s_stats_total));
    nvs_commit(h);
    nvs_close(h);
}

void armctrl_set_event_cb(armctrl_event_cb_t cb, void *arg) { s_event_cb = cb; s_event_cb_arg = arg; }

void armctrl_get_stats(uint32_t *out_total, uint32_t *out_session)
{
    if (out_total) *out_total = s_stats_total;
    if (out_session) *out_session = s_stats_session;
}

static void emit_cycle_log(int64_t t_id, int64_t t_pick, int64_t t_cut, int64_t t_place, bool ok)
{
    if (!s_event_cb) return;
    armctrl_cycle_log_t log = {
        .seq_id = ++s_cycle_seq,
        .t_identified_us = t_id,
        .t_picked_us = t_pick,
        .t_cut_us = t_cut,
        .t_placed_us = t_place,
        .ok = ok,
    };
    s_event_cb(&log, s_event_cb_arg);
}

void armctrl_request_run(bool on, bool cont) {
    if (on && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中,先 /arm_estop?on=0 清除"); return; }
    s_run = on;
    s_continuous = cont;
    if (on) {
        s_processed_count = 0;
        s_acquire_fail_streak = 0;
        armlink_set_exclusions(NULL, 0);   // 新会话: 清空上一次运行留下的排除点
    }
    ESP_LOGW(TAG, "run=%d cont=%d", on, cont);
}

bool armctrl_is_continuous(void) { return s_continuous; }

void armctrl_estop(void)
{
    s_estop = true;
    s_run = false;
#if CONFIG_ARMLINK_UART_ENABLE
    armlink_uart_send("$DST:0!", 7);
#endif
    ESP_LOGW(TAG, "急停! 已发送 $DST:0! 并停止循环");
}

bool armctrl_is_estopped(void) { return s_estop; }

void armctrl_clear_estop(void)
{
    s_estop = false;
    ESP_LOGW(TAG, "急停已清除");
}
bool armctrl_is_running(void) { return s_run; }
void armctrl_reload_cal(void) { s_cal_dirty = true; }   // 请求空闲时重载 NVS 标定(见 armctrl_task 空闲分支)

esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    if (s_estop) { ESP_LOGW(TAG, "[estop] 忽略 move_arm"); return ESP_ERR_INVALID_STATE; }
    int pwm[4];
    if (kin_move_best(s_cal.link_mm, x, y, z, pwm) != 0) {
        ESP_LOGE(TAG, "不可达 (%.0f,%.0f,%.0f) — 安全停", x, y, z);
        return ESP_ERR_INVALID_ARG;
    }
    char frame[96];
    int len = armlink_encode_arm_frame(pwm, move_ms, frame, sizeof(frame));
    if (len <= 0) return ESP_FAIL;
    // 未标定时绝不发字节(安全联锁最后一道): "mm"是假坐标,不许驱动真舵机。
    if (!s_cal.valid) {
        ESP_LOGW(TAG, "[dry] 未标定,不发送: %s", frame);
    } else {
        armlink_uart_send(frame, len);
        ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "UART 未启用, 忽略 move_arm");
    return ESP_ERR_INVALID_STATE;
#endif
}

void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    if (s_estop) { ESP_LOGW(TAG, "[estop] 忽略 move_servo"); return; }
    char frame[32];
    int len = armlink_encode_servo_frame(idx, pwm, move_ms, frame, sizeof(frame));
    if (len > 0) {
        if (!s_cal.valid) {
            ESP_LOGW(TAG, "[dry] 未标定,不发送: %s", frame);
        } else {
            armlink_uart_send(frame, len);
            ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}

// 等待 armlink 内建跟踪器(target_track)进入 STABLE(见 armlink/target_track.h),超时判失败。
// 滑行帧(coasting)不作为下爪依据——同时满足 spec §4.6 STABLE 判据的"当前帧必须命中"。
// 旧的"5帧中心/角度抖动窗口"逻辑已整体退役——抖动判定、时间门限、丢检滑行现全部
// 在 target_track 内部完成(见 docs/superpowers/specs/2026-07-06-final-submission-cleanup-design.md §4.6)。
#define ACQUIRE_TIMEOUT_MS 8000
#define ACQUIRE_POLL_MS    60

static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    int waited = 0;
    while (1) {
        if (!s_run || s_estop) {
            ESP_LOGW(TAG, "pose: 运行已取消/急停, 中止等待");
            return false;
        }
        arm_target_t t;
        armlink_get_last_target(&t);
        if (t.valid && t.stable && !t.coasting) {
            *out_px = t.center_x_px; *out_py = t.center_y_px; *out_ang = t.angle_deg;
            ESP_LOGI(TAG, "pose ok(stable) px=%.1f py=%.1f ang=%.1f", *out_px, *out_py, *out_ang);
            return true;
        }
        if (waited >= ACQUIRE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "pose: 等待STABLE超时(%dms)", ACQUIRE_TIMEOUT_MS);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(ACQUIRE_POLL_MS));
        waited += ACQUIRE_POLL_MS;
    }
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

    // 1. 目标正上方安全高度悬停(此处人可经 /arm_estop 或断电随时急停)
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
    armlink_track_resume();   // 臂已停稳在观察位: 跟踪器硬重置+记新门限时间戳(根治运动帧污染)
}

static void go_observe(void)
{
    go_observe_ex(true);
}

// 状态机主体: 未就绪守卫 -> 观察 -> 定位 -> 抓取 -> 切割 -> 放回 -> 回观察(完整流程, G分级已去除)。
static void armctrl_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_run || !s_ik_ok || !s_cal.valid || s_estop) {
            if (s_cal_dirty) {
                s_cal_dirty = false;
                armcal_load(&s_cal);
                ESP_LOGI(TAG, "标定已重载 valid=%d", s_cal.valid);
            }
            if (s_run && !s_ik_ok) { ESP_LOGW(TAG, "run 被拒: IK 自检未过"); s_run = false; }
            else if (s_run && !s_cal.valid) { ESP_LOGW(TAG, "run 被拒: 未标定"); s_run = false; }
            else if (s_run && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中"); s_run = false; }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        go_observe_ex(!s_holding);   // 爪中疑似有物(急停恢复/切割失败重启)则不开爪回观察位,等人工取出
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试");
            s_acquire_fail_streak++;
            if (s_acquire_fail_streak >= 3) {
                ESP_LOGW(TAG, "连续3次未能稳定获取目标,自动停止");
                s_run = false;
                s_acquire_fail_streak = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        s_acquire_fail_streak = 0;
        int64_t t_identified = esp_timer_get_time();
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            emit_cycle_log(t_identified, 0, 0, 0, false);
            go_observe(); s_run = false; continue;
        }
        int64_t t_picked = esp_timer_get_time();
        s_holding = true;   // 爪已闭合夹住电池(直到place尝试完成)
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            emit_cycle_log(t_identified, t_picked, 0, 0, false);
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        int64_t t_cut = esp_timer_get_time();
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        s_holding = s_estop;   // 正常流已过开爪点→false;若place期间被急停中断(开爪可能没执行)→保持true错到安全侧,恢复时不开爪(SAFETY.md流程:先断电人工取出)
        int64_t t_placed = esp_timer_get_time();
        emit_cycle_log(t_identified, t_picked, t_cut, t_placed, true);
        // 记录本次目标 px 中心到防重抓排除表(px域: 放回原位后,同一观察位下一轮会在同一像素
        // 位置再次被检出;记 px 而非 mm 是零坐标转换的最简方案)。
        if (s_processed_count < 8) {
            s_processed_px[s_processed_count][0] = px;
            s_processed_px[s_processed_count][1] = py;
            s_processed_count++;
            armlink_set_exclusions(s_processed_px, s_processed_count);
        }
        s_stats_total++;
        s_stats_session++;
        stats_save();
        go_observe();
        ESP_LOGI(TAG, "完整循环完成(累计%" PRIu32 "/本次%" PRIu32 ")", s_stats_total, s_stats_session);
        if (!s_continuous) {
            s_run = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t armctrl_init(void)
{
    esp_err_t e = armcal_load(&s_cal);   // 无标定则 valid=false
    if (e != ESP_OK) ESP_LOGW(TAG, "标定未就绪(valid=false), 自动模式将被拒绝");
    s_ik_ok = (kin_selftest() == 0);
    if (!s_ik_ok) ESP_LOGE(TAG, "IK 自检失败, 自动模式禁用");
    stats_load();
    xTaskCreate(armctrl_task, "armctrl", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "init ok (run=off,ik=%d,cal=%d,stats_total=%" PRIu32 ")", s_ik_ok, s_cal.valid, s_stats_total);
    return ESP_OK;
}
