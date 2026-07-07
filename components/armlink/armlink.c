#include "armlink.h"
#include "armlink_uart.h"
#include "target_track.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "armlink";

// 目标选框分数下限在 target_track 内部处理(TRACK_MIN_SCORE_NEW=0.40/TRACK_MIN_SCORE_UPDATE=0.25)；
// 角度置信门限同理(TRACK_MIN_ANISOTROPY=0.10)。此处不再重复定义。

static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;     // 保护 s_last 与 s_tracker 的唯一互斥量(两个任务都会访问,见 Task 6 说明)
static track_state_t     s_tracker;

esp_err_t armlink_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_last, 0, sizeof(s_last));
    s_last.wrist_deg = NAN;
    track_init(&s_tracker);
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

    // 纯本地转换(不碰共享状态): ai_box_t -> track_box_t
    track_box_t boxes[AI_MAX_BOXES];
    int n = 0;
    for (int i = 0; i < r->count && n < AI_MAX_BOXES; i++) {
        const ai_box_t *b = &r->boxes[i];
        track_box_t *tb = &boxes[n++];
        tb->cx = 0.5f * (float)(b->x1 + b->x2);
        tb->cy = 0.5f * (float)(b->y1 + b->y2);
        tb->w  = (float)(b->x2 - b->x1);
        tb->h  = (float)(b->y2 - b->y1);
        tb->angle_deg  = b->angle_deg;
        tb->score      = b->score;
        tb->anisotropy = b->anisotropy;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_update(&s_tracker, boxes, n, r->capture_ts_us);
    track_output_t out;
    track_get(&s_tracker, &out);

    arm_target_t t;
    memset(&t, 0, sizeof(t));
    t.wrist_deg   = NAN;
    t.src_w       = (uint32_t)r->src_w;
    t.src_h       = (uint32_t)r->src_h;
    t.valid       = out.confirmed;
    t.stable      = out.stable;
    t.coasting    = out.coasting;
    t.center_x_px = out.cx;
    t.center_y_px = out.cy;
    t.angle_deg   = out.angle_deg;
    t.score       = out.score;
    t.frame_id    = s_last.frame_id + 1;   // 每次更新递增，消费端可判新旧
    s_last = t;
    xSemaphoreGive(s_lock);
}

void armlink_get_last_target(arm_target_t *out)
{
    if (!out) return;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_last;
    if (s_lock) xSemaphoreGive(s_lock);
}

void armlink_track_suspend(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_suspend(&s_tracker);
    xSemaphoreGive(s_lock);
}

void armlink_track_resume(void)
{
    if (!s_lock) return;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_resume(&s_tracker, now);
    // 同锁内立即失效已发布快照: resume(硬重置)到下一帧检测发布之间有最长一帧(~300ms)窗口,
    // 不清 s_last 的话 acquire_pose 首轮轮询(60ms)会吃到复位前遗留的 stable 快照
    // (2026-07-07 上板实测: pose ok 与 resume 同 tick 出现,物理上不可能——STABLE 需≥10帧)。
    s_last.valid = false;
    s_last.stable = false;
    s_last.coasting = false;
    s_last.frame_id++;
    xSemaphoreGive(s_lock);
}

void armlink_set_exclusions(const float pts_px[][2], int n)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_set_exclusions(&s_tracker, pts_px, n);
    xSemaphoreGive(s_lock);
}
