#pragma once
#include "dl_detect_base.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"

// 队友自训 YOLOv8n 4 类电池检测器(21700/18650/9V/AA), 6 分离输出导出 + int8。
// 后处理复用 esp-dl yolo11PostProcessor(YOLOv8/11 检测头解码一致: DFL reg_max=16 + sigmoid)。
namespace battery_yolo {
class Yolo8n : public dl::detect::DetectImpl {
public:
    static inline constexpr float default_score_thr = 0.40;  // PC 冒烟测板域帧 conf 0.8+,阈值可高走;上板按实测再调
    static inline constexpr float default_nms_thr = 0.7;
    Yolo8n(const char *model_name, float score_thr, float nms_thr);
};
} // namespace battery_yolo

class BatteryYoloDetect : public dl::detect::DetectWrapper {
public:
    typedef enum {
        BATTERY_YOLOV8N_224_S8,
    } model_type_t;
    BatteryYoloDetect(model_type_t model_type = static_cast<model_type_t>(CONFIG_DEFAULT_BATTERY_YOLO_MODEL),
                      bool lazy_load = true);

private:
    void load_model() override;
    model_type_t m_model_type;
};
