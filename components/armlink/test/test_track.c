#include "target_track.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

// ---- 测试1: 运动帧拒收(2026-07-06晚失败场景根因复现) ----
// 臂运动中检出的 y=317 假帧必须被时间门限拒收,不得进滤波器。
static void test_motion_frame_rejected(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 1000000);   // gate_ts=1.000s(臂刚停稳的时刻)

    // 运动中(gate之前)拍到的旧帧: capture_ts=0.900s < gate_ts,必须整帧丢弃
    track_box_t moving[] = {{ .cx=330, .cy=317, .w=100, .h=57, .angle_deg=-1, .score=0.73f, .anisotropy=0 }};
    bool accepted = track_update(&tr, moving, 1, 900000);
    CHECK(!accepted, "motion frame(ts<gate) must be rejected");

    track_output_t out; track_get(&tr, &out);
    CHECK(!out.confirmed, "rejected motion frame must not initialize tracker");

    // 停稳后的真帧: capture_ts=1.050s >= gate_ts,必须接受
    track_box_t settled[] = {{ .cx=330, .cy=182, .w=100, .h=57, .angle_deg=-1, .score=0.73f, .anisotropy=0 }};
    accepted = track_update(&tr, settled, 1, 1050000);
    CHECK(accepted, "settled frame(ts>=gate) must be accepted");
    track_get(&tr, &out);
    CHECK(fabsf(out.cy - 182.0f) < 1.0f, "first accepted frame initializes estimate to detection");
}

int main(void) {
    test_motion_frame_rejected();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
