#include "voicelink_frame.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

static voicelink_cmd_t feed_str(voicelink_frame_state_t *st, const char *s) {
    voicelink_cmd_t last = VOICELINK_CMD_NONE;
    for (; *s; s++) {
        voicelink_cmd_t r = voicelink_frame_feed(st, *s);
        if (r != VOICELINK_CMD_NONE) last = r;
    }
    return last;
}

int main(void) {
    voicelink_frame_state_t st;

    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#Start!") == VOICELINK_CMD_START, "exact Start");

    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#Stop!") == VOICELINK_CMD_STOP, "exact Stop");

    // 语音模块每次识别固定发送同一帧两遍：验证第二遍照样能识别
    voicelink_frame_reset(&st);
    {
        voicelink_cmd_t r1 = feed_str(&st, "#Start!");
        voicelink_cmd_t r2 = feed_str(&st, "#Start!");
        CHECK(r1 == VOICELINK_CMD_START, "first of double-send");
        CHECK(r2 == VOICELINK_CMD_START, "second of double-send");
    }

    // 半截帧不能污染后续正常帧(新 '#' 必须无条件重开)
    voicelink_frame_reset(&st);
    {
        voicelink_cmd_t mid = feed_str(&st, "#Sto");
        CHECK(mid == VOICELINK_CMD_NONE, "partial frame yields nothing yet");
        voicelink_cmd_t r = feed_str(&st, "#Stop!");
        CHECK(r == VOICELINK_CMD_STOP, "restart after partial");
    }

    // 真实运动帧(#000P1500T1000!)不能被误判成语音命令 —— 关键回归防线
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#000P1500T1000!") == VOICELINK_CMD_NONE, "motion frame ignored");

    // 前导噪声字节被忽略，不影响后续正常帧
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "xyz#Start!") == VOICELINK_CMD_START, "leading noise ignored");

    // 超长垃圾不越界、不崩、不误判
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#ThisIsWayTooLongToBeAValidVoiceLinkFrame!") == VOICELINK_CMD_NONE,
          "oversized frame rejected safely");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
