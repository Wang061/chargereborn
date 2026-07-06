#pragma once
#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"

namespace espdet4_detect {
class ESPDet4 : public dl::detect::DetectImpl {
public:
    // 2026-07-06 iter_2(mse+TQT)量化后板上实测: 真实检出分数已从早期int8基线的
    // 0.05~0.18 大幅回升(离线map50 0.73->0.93,18650/21700置信度塌陷基本解决)。
    // 0.08 是塌陷模型时代的补偿值,当前模型下会放过大量低分噪声框;上调到 0.40。
    static inline constexpr float default_score_thr = 0.40;
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
