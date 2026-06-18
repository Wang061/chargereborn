#pragma once
#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"

namespace espdet_detect {
class ESPDet : public dl::detect::DetectImpl {
public:
    static inline constexpr float default_score_thr = 0.20;  // 本模型置信度偏低(0.20 平衡漏检/误报);可按实测再调
    static inline constexpr float default_nms_thr = 0.7;
    ESPDet(const char *model_name, float score_thr, float nms_thr);
};
} // namespace espdet_detect

class ESPDetDetect : public dl::detect::DetectWrapper {
public:
    typedef enum {
        ESPDET_PICO_224_224_BATTERY,
    } model_type_t;
    ESPDetDetect(model_type_t model_type = static_cast<model_type_t>(CONFIG_DEFAULT_ESPDET_DETECT_MODEL),
                 bool lazy_load = true);

private:
    void load_model() override;
    model_type_t m_model_type;
};
