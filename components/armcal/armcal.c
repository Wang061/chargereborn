#include "armcal.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "armcal";
#define NS  "armcal"
#define KEY "cfg"

void armcal_defaults(armcal_t *c)
{
    memset(c, 0, sizeof(*c));
    // 单位阵占位(未标定)
    c->H[0] = 1.0f; c->H[4] = 1.0f; c->H[8] = 1.0f;
    // 连杆(卷尺实测 2026-07-02, 销心到销心; L3=腕#003轴心到18650中轴闭合位)
    c->link_mm[0] = 107.0f; c->link_mm[1] = 107.0f;
    c->link_mm[2] = 86.5f;  c->link_mm[3] = 165.0f;
    // 观察位(G1 定稿 2026-07-04: 采用 OpenMV 原厂 Home 位 (0,100,70), alpha=-77 俯视,
    // 用户实测画面覆盖优于更垂直的(0,100,50); 标定8点带 y=150..210 在视野内)
    c->observe_x = 0.0f; c->observe_y = 100.0f; c->observe_z = 70.0f;
    // 腕#004(G1 实测 2026-07-03: +504us 转 60° => k=504/60=8.4 pwm/deg, 约240°行程档;
    // 旧 OpenMV 继承 5.6 会把腕角打 2/3 折扣)
    c->wrist_center_pwm = 1500; c->wrist_k = 8.4f; c->wrist_zero_deg = 0;
    // 夹爪#005(G1 实测 2026-07-03: 小PWM=开; 1400 夹住φ14, +80 余量=1480 牢固且舵机安静;
    // 旧继承值 1700 会越过夹持点 ~300us 硬堵转, 禁用)
    c->gripper_open_pwm = 800; c->gripper_close_pwm = 1480; c->gripper_time_ms = 800;
    // 高度(G1 实测 2026-07-03: IK 绝对 z 系统偏高 ~20mm(低位), 下列为"命令值"经验定标:
    // 命令 z=0 时爪心恰在平躺φ14电池轴心(~7mm), 完整抓取-抬起验证通过; place=pick 同高释放)
    c->pick_z = 0.0f; c->approach_z = 70.0f; c->carry_z = 120.0f; c->place_z = 0.0f;
    // 刀口(OpenMV 继承起点)
    c->blade_x = 145.0f; c->blade_y = 75.0f; c->blade_safe_z = 100.0f;
    c->blade_contact_z = 40.0f; c->cut_offset_x = 12.0f; c->cut_times = 2;
    c->valid = false;
    c->magic = ARMCAL_MAGIC;
}

esp_err_t armcal_load(armcal_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    armcal_defaults(out);
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e != ESP_OK) return ESP_ERR_NVS_NOT_FOUND;
    armcal_t tmp;
    size_t sz = sizeof(tmp);
    e = nvs_get_blob(h, KEY, &tmp, &sz);
    nvs_close(h);
    if (e != ESP_OK || sz != sizeof(tmp) || tmp.magic != ARMCAL_MAGIC) {
        ESP_LOGW(TAG, "无有效标定,用默认(valid=false)");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = tmp;
    ESP_LOGI(TAG, "标定已加载 valid=%d", out->valid);
    return ESP_OK;
}

esp_err_t armcal_save(const armcal_t *c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "标定已保存 e=%d valid=%d", e, c->valid);
    return e;
}
