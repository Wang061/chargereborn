#include "camera.h"
#include "esp_log.h"

static const char *TAG = "camera";

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,   // 旧版字段名 pin_sscb_sda；build 报错就改这两行
        .pin_sccb_scl = CAM_PIN_SIOC,   // 旧版字段名 pin_sscb_scl
        .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5, .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_pclk  = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_HZ,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,    // 直出 JPEG，省 RAM、可直接传
        .frame_size   = FRAMESIZE_VGA,     // 640x480 采集；OV5640 可上更高
        .jpeg_quality = 12,                // 0-63，越小越清
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM, // 帧缓冲住 PSRAM（已实测 8MB 可用）
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x (%s) — 核对接线表/供电/共地",
                 err, esp_err_to_name(err));
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // 用官方枚举宏判别(sensor.h，经 esp_camera.h 引入):OV2640_PID=0x26、OV5640_PID=0x5640
        const char *name = (s->id.PID == OV2640_PID) ? "OV2640"
                         : (s->id.PID == OV5640_PID) ? "OV5640" : "other";
        ESP_LOGI(TAG, "camera up: sensor PID=0x%04x (%s), JPEG VGA", s->id.PID, name);
    }
    return ESP_OK;
}

camera_fb_t *camera_capture(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) ESP_LOGW(TAG, "esp_camera_fb_get returned NULL");
    return fb;
}

void camera_return(camera_fb_t *fb)
{
    if (fb) esp_camera_fb_return(fb);
}
