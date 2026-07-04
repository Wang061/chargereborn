#pragma once
#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"

namespace espdet4_detect {
class ESPDet4 : public dl::detect::DetectImpl {
public:
    static inline constexpr float default_score_thr = 0.20;  // 与单类模型一致的起点阈值,上板后按实测再调
    static inline constexpr float default_nms_thr = 0.7;
    ESPDet4(const char *model_name, float score_thr, float nms_thr);
};
} // namespace espdet4_detect

class ESPDet4Detect : public dl::detect::DetectWrapper {
public:
    typedef enum {
        ESPDET_PICO_224_224_BATTERY4,
    } model_type_t;
    ESPDet4Detect(model_type_t model_type = static_cast<model_type_t>(CONFIG_DEFAULT_ESPDET4_DETECT_MODEL),
                  bool lazy_load = true);

private:
    void load_model() override;
    model_type_t m_model_type;
};
