#include "battery_yolo_detect.hpp"
#include "esp_log.h"
#include <filesystem>

#if CONFIG_BATTERY_YOLO_MODEL_IN_FLASH_RODATA
extern const uint8_t battery_yolo_espdl[] asm("_binary_battery_yolo_espdl_start");
static const char *path = (const char *)battery_yolo_espdl;
#elif CONFIG_BATTERY_YOLO_MODEL_IN_FLASH_PARTITION
static const char *path = "espdet_det";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif

namespace battery_yolo {
Yolo8n::Yolo8n(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_BATTERY_YOLO_MODEL_IN_SDCARD
    m_model =
        new dl::Model(path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_BATTERY_YOLO_MODEL_LOCATION));
#else
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_BATTERY_YOLO_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
    // esp-dl 3.3.5: RGB888 输入用 3 参构造(无 RGB565 cap 枚举), 与 battery_detect 同款
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
    m_image_preprocessor->enable_letterbox({114, 114, 114});
    // yolo11PostProcessor: 读 box0/score0/box1/score1/box2/score2, DFL(reg_max=16)+sigmoid+NMS
    // stage 表 {stride_y, stride_x, offset_y, offset_x} 对应 stride 8/16/32, anchor 中心偏移 = stride/2
    m_postprocessor = new dl::detect::yolo11PostProcessor(
        m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}
} // namespace battery_yolo

BatteryYoloDetect::BatteryYoloDetect(model_type_t model_type, bool lazy_load) : m_model_type(model_type)
{
    switch (model_type) {
    case model_type_t::BATTERY_YOLOV8N_224_S8:
        m_score_thr[0] = battery_yolo::Yolo8n::default_score_thr;
        m_nms_thr[0] = battery_yolo::Yolo8n::default_nms_thr;
        break;
    }
    if (lazy_load) {
        m_model = nullptr;
    } else {
        load_model();
    }
}

void BatteryYoloDetect::load_model()
{
    switch (m_model_type) {
    case model_type_t::BATTERY_YOLOV8N_224_S8:
#if CONFIG_FLASH_BATTERY_YOLOV8N_224_S8 || CONFIG_BATTERY_YOLO_MODEL_IN_SDCARD
        m_model = new battery_yolo::Yolo8n("battery_yolov8n_224_s8.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("battery_yolo", "battery_yolov8n_224_s8 is not selected in menuconfig.");
#endif
        break;
    }
}
