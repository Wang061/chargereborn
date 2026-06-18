#include "espdet_detect.hpp"
#include "esp_log.h"
#include <filesystem>

#if CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t battery_detect_espdl[] asm("_binary_battery_detect_espdl_start");
static const char *path = (const char *)battery_detect_espdl;
#elif CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_PARTITION
static const char *path = "espdet_det";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif
namespace espdet_detect {
ESPDet::ESPDet(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_ESPDET_DETECT_MODEL_IN_SDCARD
    m_model =
        new dl::Model(path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_ESPDET_DETECT_MODEL_LOCATION));
#else
    auto sd_path = std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_ESPDET_DETECT_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
    // esp-dl 3.3.5 dropped the RGB565 capability arg from ImagePreprocessor; input is RGB888, 3-arg ctor is fine
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
    m_image_preprocessor->enable_letterbox({114, 114, 114});
    m_postprocessor = new dl::detect::ESPDetPostProcessor(
        m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}

} // namespace espdet_detect

ESPDetDetect::ESPDetDetect(model_type_t model_type, bool lazy_load) : m_model_type(model_type)
{
    switch (model_type) {
    case model_type_t::ESPDET_PICO_224_224_BATTERY:
        m_score_thr[0] = espdet_detect::ESPDet::default_score_thr;
        m_nms_thr[0] = espdet_detect::ESPDet::default_nms_thr;
        break;
    }
    if (lazy_load) {
        m_model = nullptr;
    } else {
        load_model();
    }
}

void ESPDetDetect::load_model()
{
    switch (m_model_type) {
    case model_type_t::ESPDET_PICO_224_224_BATTERY:
#if CONFIG_FLASH_ESPDET_PICO_224_224_BATTERY || CONFIG_BATTERY_DETECT_MODEL_IN_SDCARD
        m_model = new espdet_detect::ESPDet("espdet_pico_224_224_battery.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("battery_detect", "espdet_pico_224_224_battery is not selected in menuconfig.");
#endif
        break;
    }
}
