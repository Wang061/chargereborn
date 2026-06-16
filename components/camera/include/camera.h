#pragma once
#include "esp_err.h"
#include "esp_camera.h"   // camera_fb_t / pixformat_t（托管组件 espressif/esp32-camera）
#ifdef __cplusplus
extern "C" {
#endif

// ===== 相机接线表（排针 → DevKitC GPIO）。物理杜邦线必须与此表一一对应。=====
// 安全性：均落在 GPIO4..18，避开 Octal PSRAM 专用 35/36/37、USB-JTAG 19/20、
//         SPI flash/PSRAM 段 26..37、strapping 0/3/45/46。（已经官方 datasheet 核对）
// PWDN/RESET 不接（-1，用 SCCB 软复位）。飞线不稳：先降 CAM_XCLK_HZ→10MHz，再降分辨率。
#define CAM_PIN_PWDN    (-1)
#define CAM_PIN_RESET   (-1)
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD     4   // SCCB SDA
#define CAM_PIN_SIOC     5   // SCCB SCL
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK    13
#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      11
#define CAM_PIN_D2      10
#define CAM_PIN_D1       9
#define CAM_PIN_D0       8

#define CAM_XCLK_HZ     16000000   // 16MHz：启用 ESP32-S3 EDMA 模式、对飞线更友好；不稳降 10000000
#define CAM_JPEG_QUALITY 12        // JPEG 质量 0-63，越小越清
#define CAM_FB_COUNT     2         // 帧缓冲数；JPEG 模式 >1 → 连续取流更顺

// 注意：函数名用 camera_ 前缀。托管组件 esp32-camera 的私有 HAL(cam_hal.c)已导出
//       全局符号 cam_init/cam_deinit，裸用 cam_ 前缀会在链接期符号冲突(multiple definition)。
// 初始化相机（DVP+SCCB，自动探测传感器）。成功 ESP_OK；失败返回错误码并打印诊断，不 abort。
esp_err_t camera_init(void);
// 取一帧（JPEG），失败返回 NULL。用完必须 camera_return（配对，杜绝帧缓冲泄漏）。
camera_fb_t *camera_capture(void);
// 归还帧缓冲。
void camera_return(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif
