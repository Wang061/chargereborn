#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 纯 C 语音帧匹配器：逐字节喂入，识别完整的 "#Start!" / "#Stop!" 帧。
// 无 ESP 依赖，可 host gcc 单测(同 kinematics/armlink_frame/target_track 的约定)。
// KM1 端(steward_km1.ino)用同一套算法的 Arduino C++ 版本转发,两边独立实现,不共享代码
// (分属两个工具链, 见 docs/superpowers/specs/2026-07-08-voice-start-stop-control-design.md §5)。

typedef enum {
    VOICELINK_CMD_NONE = 0,
    VOICELINK_CMD_START,
    VOICELINK_CMD_STOP,
} voicelink_cmd_t;

#define VOICELINK_FRAME_MAX 15   // "#Start!"=7 "#Stop!"=6，留够余量

typedef struct {
    char buf[VOICELINK_FRAME_MAX + 1];
    int  len;   // 0 = 尚未见到帧起始符 '#'
} voicelink_frame_state_t;

// 清零状态，等待下一帧。
void voicelink_frame_reset(voicelink_frame_state_t *st);

// 喂入一个字符。收到完整 "#Start!"/"#Stop!" 时返回对应命令并自动 reset；
// 否则返回 VOICELINK_CMD_NONE。遇到新的 '#' 会无条件重开一帧(丢弃半截帧,
// 噪声容错优先于漏检)。超长垃圾会在越界前放弃当前帧,不会写出界。
voicelink_cmd_t voicelink_frame_feed(voicelink_frame_state_t *st, char c);

#ifdef __cplusplus
}
#endif
