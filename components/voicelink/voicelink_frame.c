#include "voicelink_frame.h"
#include <string.h>

void voicelink_frame_reset(voicelink_frame_state_t *st)
{
    st->len = 0;
}

voicelink_cmd_t voicelink_frame_feed(voicelink_frame_state_t *st, char c)
{
    if (c == '#') {
        st->len = 1;
        st->buf[0] = '#';
        return VOICELINK_CMD_NONE;
    }
    if (st->len == 0) {
        return VOICELINK_CMD_NONE;   // 还没见到帧起始符，忽略
    }
    if (c == '!') {
        if (st->len < VOICELINK_FRAME_MAX) {
            st->buf[st->len++] = '!';
            st->buf[st->len] = '\0';
        } else {
            st->len = 0;
            return VOICELINK_CMD_NONE;   // 超长，判定不是我们要的帧
        }
        voicelink_cmd_t cmd = VOICELINK_CMD_NONE;
        if (strcmp(st->buf, "#Start!") == 0) cmd = VOICELINK_CMD_START;
        else if (strcmp(st->buf, "#Stop!") == 0) cmd = VOICELINK_CMD_STOP;
        st->len = 0;   // 一帧结束，重置等下一帧
        return cmd;
    }
    if (st->len < VOICELINK_FRAME_MAX) {
        st->buf[st->len++] = c;
    } else {
        st->len = 0;   // 超界放弃这一帧，等下一个 '#' 重来
    }
    return VOICELINK_CMD_NONE;
}
