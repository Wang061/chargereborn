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
// 检测器只输出电池类别(4类模型:21700/18650/9V/AA；单类espdet只有18650)，没有"非电池"类，
// 故选框不按具体类别过滤——任一电池类别都可作为抓取目标。
// (2026-07-06 G1 实测踩坑：曾硬过滤只认 cls==18650，但同一颗测试电池在4类int8模型下经常
//  被判成 AA 而非 18650，导致 armlink_update_from_ai 长期选不出目标、armctrl.acquire_pose
//  反复等新检测超时("反复抬起来又降下去"却抓不到)。用户确认4类本就都要能抓，遂放开；
//  若后续需要按类别区别处理(如只切锂电池类)，需先把 cls 带进 arm_target_t 再加判断，
//  而不是走这个硬过滤。)
#define ARMLINK_MIN_ANISOTROPY    0.10f   // 角度置信门限，低于此不取该框角度
// 目标选框分数下限(区别于 ai/Kconfig 的 score_thr=0.08——那个是"要不要显示这个框"，
// 这个是"够不够格当抓取目标")。2026-07-06 放开类别过滤后实测：真实电池稳定检出 0.50~0.88，
// 量化噪声产生的幻视框集中在 0.12~0.27 且位置与真实框相距甚远(可达上百像素)；两簇之间有
// 明显空档，取 0.40 卡在空档中间滤掉噪声，不影响真实检出。
#define ARMLINK_MIN_SCORE         0.40f
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

    // 选最佳电池框：不限具体电池类别，分数够格、角度可靠，取 score 最高者
    int best = -1;
    float best_score = -1.0f;
    for (int i = 0; i < r->count; i++) {
        const ai_box_t *b = &r->boxes[i];
        if (b->score < ARMLINK_MIN_SCORE) continue;
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
