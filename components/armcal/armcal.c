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
    // 连杆起点(真机 .ino 值; 卷尺实测后覆盖)
    c->link_mm[0] = 100.0f; c->link_mm[1] = 105.0f;
    c->link_mm[2] = 75.0f;  c->link_mm[3] = 180.0f;
    // 观察位(起点,须实测调; y 正前方, z 高处俯视)
    c->observe_x = 0.0f; c->observe_y = 130.0f; c->observe_z = 120.0f;
    // 腕#004(OpenMV 继承)
    c->wrist_center_pwm = 1500; c->wrist_k = 5.6f; c->wrist_zero_deg = 0;
    // 夹爪#005(OpenMV 继承, G1 用户验证)
    c->gripper_open_pwm = 800; c->gripper_close_pwm = 1700; c->gripper_time_ms = 800;
    // 高度(OpenMV 继承起点)
    c->pick_z = 0.0f; c->approach_z = 70.0f; c->carry_z = 120.0f; c->place_z = 3.0f;
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
