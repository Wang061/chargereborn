#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// 语音开始/停止桩：监听 UART，收到 KM1 转发的 #Start!/#Stop! 帧后
// 调用 armctrl_request_run()。默认关闭，见 Kconfig CONFIG_VOICELINK_ENABLE。
esp_err_t voicelink_init(void);

#ifdef __cplusplus
}
#endif
