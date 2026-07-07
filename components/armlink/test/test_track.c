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

// ---- 测试2: 类别翻转框跳(AA<->9V, 2026-07-06晚日志真实框) ----
static void test_class_flip_no_jump(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    float last_cx = 0; bool have_last = false;
    for (int i = 0; i < 10; i++) {
        track_box_t b;
        if (i % 2 == 0) { b = (track_box_t){ .cx=367.5f, .cy=182.0f, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 }; }   // AA [285,152,450,212]
        else            { b = (track_box_t){ .cx=336.5f, .cy=176.5f, .w=109, .h=63, .angle_deg=-1, .score=0.73f, .anisotropy=0 }; }   // 9V [282,145,391,208]
        ts += 250000;
        track_update(&tr, &b, 1, ts);
        track_output_t out; track_get(&tr, &out);
        if (have_last) {
            CHECK(fabsf(out.cx - last_cx) <= 3.0f, "class-flip frame must not jump filtered center >3px");
        }
        last_cx = out.cx; have_last = true;
    }
}

// ---- 测试3: 置信度闪烁(0.73<->0.12) ----
static void test_score_flicker_no_break(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    float scores[] = {0.73f, 0.73f, 0.73f, 0.12f, 0.73f, 0.73f};
    for (int i = 0; i < 6; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=scores[i], .anisotropy=0 };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
    }
    track_output_t out; track_get(&tr, &out);
    CHECK(out.confirmed, "low-score dip(below 0.25 update-threshold, but track already confirmed) must not break confirmed track");
    // 注: score=0.12 < UPDATE门限(0.25),该帧按丢检处理(滑行),不应导致轨迹丢失
}

// ---- 测试4: 底部持续误检不得劫持轨迹(2026-07-06晚日志真实误检框) ----
static void test_bottom_false_positive_rejected(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    for (int i = 0; i < 8; i++) {
        track_box_t boxes[2];
        boxes[0] = (track_box_t){ .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };   // 真目标
        boxes[1] = (track_box_t){ .cx=142, .cy=444, .w=269, .h=54, .angle_deg=-1, .score=0.27f, .anisotropy=0 };   // 底部误检 [8,417,277,471]
        ts += 250000;
        track_update(&tr, boxes, 2, ts);
    }
    track_output_t out; track_get(&tr, &out);
    CHECK(out.confirmed, "tracker must confirm on the real target");
    CHECK(fabsf(out.cy - 182.0f) < 5.0f, "persistent low-score false positive must not hijack the track");
}

// ---- 测试5: 稳态收敛(有界噪声下必须收敛且尾段抖动收紧) ----
static float pseudo_noise(uint32_t *seed) {
    *seed = (*seed) * 1103515245u + 12345u;
    float u = (float)((*seed >> 8) & 0xFFFF) / 65536.0f;   // 0..1 近似均匀
    return (u - 0.5f) * 2.0f;   // -1..1
}
static void test_steady_state_convergence(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    uint32_t seed = 42;
    int64_t ts = 0;
    int stable_frame = -1;
    float tail_cx[10];
    for (int i = 0; i < 40; i++) {
        float noise_x = pseudo_noise(&seed) * 3.0f;   // 量级~1.7px std,够用的冒烟噪声
        float noise_y = pseudo_noise(&seed) * 3.0f;
        track_box_t b = { .cx = 330.0f + noise_x, .cy = 182.0f + noise_y, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
        track_output_t out; track_get(&tr, &out);
        if (stable_frame < 0 && out.stable) stable_frame = i;
        if (i >= 30) tail_cx[i - 30] = out.cx;
    }
    CHECK(stable_frame >= 0, "STABLE must eventually be reached under bounded noise");
    float minv = tail_cx[0], maxv = tail_cx[0];
    for (int i = 1; i < 10; i++) { if (tail_cx[i] < minv) minv = tail_cx[i]; if (tail_cx[i] > maxv) maxv = tail_cx[i]; }
    CHECK((maxv - minv) <= 3.0f, "steady-state filtered center must be tight (<=3px range over last 10 frames)");
}

// ---- 测试6: 滑行->丢锁->重捕获; 目标挪动100px->聚集重锁 ----
static void test_coast_lost_recapture(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    for (int i = 0; i < 5; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
    }
    track_output_t out; track_get(&tr, &out);
    CHECK(out.confirmed, "must be confirmed before coast test");

    // 丢检 1.5s(超过 max_age=1.2s) -> LOST
    ts += 1500000;
    bool accepted = track_update(&tr, NULL, 0, ts);
    CHECK(accepted, "empty-detection frame is still a valid update call(not gated)");
    track_get(&tr, &out);
    CHECK(!out.confirmed, "must go LOST after max_age with no detections");

    // 重捕获: 下一帧出现目标,应重新从 hits=1 起步(尚不confirmed)
    ts += 250000;
    track_box_t b2 = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
    track_update(&tr, &b2, 1, ts);
    track_get(&tr, &out);
    CHECK(!out.confirmed, "single hit right after LOST must not be confirmed yet(min_hits=3)");

    // ---- 目标挪动100px: 连续3帧聚集拒绝后应重置,随后在新位置重新确认 ----
    track_state_t tr2;
    track_init(&tr2);
    track_resume(&tr2, 0);
    ts = 0;
    for (int i = 0; i < 5; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr2, &b, 1, ts);
    }
    track_get(&tr2, &out);
    CHECK(out.confirmed, "must be confirmed before move test");
    for (int i = 0; i < 3; i++) {
        track_box_t moved = { .cx=430, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr2, &moved, 1, ts);
    }
    track_get(&tr2, &out);
    CHECK(!out.confirmed, "after 3 clustered off-gate rejections the stale track must reset");
    for (int i = 0; i < 3; i++) {
        track_box_t moved = { .cx=430, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr2, &moved, 1, ts);
    }
    track_get(&tr2, &out);
    CHECK(out.confirmed, "tracker must recapture at the new location after reset");
    CHECK(fabsf(out.cx - 430.0f) < 5.0f, "recaptured center must match the moved location");
}

// ---- 测试7: 角度回绕(178<->2度交替噪声,滤波不得穿越90度假跳变) ----
static void test_angle_wraparound(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    float angs[] = {178.0f, 2.0f, 179.0f, 1.0f, 178.5f, 1.5f};
    for (int i = 0; i < 6; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=angs[i], .score=0.73f, .anisotropy=0.5f };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
        track_output_t out; track_get(&tr, &out);
        CHECK(out.angle_deg < 30.0f || out.angle_deg > 150.0f,
              "filtered angle must stay near the true value's short-arc side, not drift to ~90");
    }
}

// ---- 测试8: 双阈值(0.25/0.40)边界值——分数介于两阈值之间时,是否真的按confirmed状态分流 ----
static void test_dual_threshold_boundary(void) {
    // Part A: 已confirmed轨迹,分数0.30(>=UPDATE门限0.25)必须算命中
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    for (int i = 0; i < 3; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
    }
    track_output_t out; track_get(&tr, &out);
    CHECK(out.confirmed && out.hits == 3, "must be confirmed with hits=3 before boundary probe");

    track_box_t mid = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.30f, .anisotropy=0 };  // 0.25<=0.30<0.40
    ts += 250000;
    track_update(&tr, &mid, 1, ts);
    track_get(&tr, &out);
    CHECK(out.hits == 4, "score=0.30 must count as a hit once confirmed(>=UPDATE threshold 0.25) — proves UPDATE threshold, not NEW threshold, governs post-confirm frames");

    // Part B: 尚未confirmed轨迹,分数0.30(<NEW门限0.40)必须被拒绝,不得计入hits/confirmed
    track_state_t tr2;
    track_init(&tr2);
    track_resume(&tr2, 0);
    ts = 0;
    for (int i = 0; i < 2; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr2, &b, 1, ts);
    }
    track_get(&tr2, &out);
    CHECK(!out.confirmed && out.hits == 2, "must have hits=2, not yet confirmed(min_hits=3), before boundary probe");

    track_box_t mid2 = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.30f, .anisotropy=0 };  // 0.25<=0.30<0.40
    ts += 250000;
    track_update(&tr2, &mid2, 1, ts);
    track_get(&tr2, &out);
    CHECK(!out.confirmed && out.hits == 2, "score=0.30 must be rejected before confirm(<NEW threshold 0.40) — hits must stay at 2, proving NEW threshold(not UPDATE) governs pre-confirm frames");
}

// ---- 测试9: 关联时"先门限后选分数",而非"先选分数后门限"——高分但门外的候选不得被选中 ----
static void test_gate_before_score_pick(void) {
    track_state_t tr;
    track_init(&tr);
    track_resume(&tr, 0);
    int64_t ts = 0;
    for (int i = 0; i < 3; i++) {
        track_box_t b = { .cx=330, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.73f, .anisotropy=0 };
        ts += 250000;
        track_update(&tr, &b, 1, ts);
    }
    track_output_t out; track_get(&tr, &out);
    CHECK(out.confirmed, "must be confirmed before gate-order probe");

    // 同帧两个候选: 门内低分(332,182,score=0.50) vs 门外高分(500,182,score=0.90,远超门限半径~50px)
    track_box_t boxes[2];
    boxes[0] = (track_box_t){ .cx=332, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.50f, .anisotropy=0 };
    boxes[1] = (track_box_t){ .cx=500, .cy=182, .w=165, .h=60, .angle_deg=-1, .score=0.90f, .anisotropy=0 };
    ts += 250000;
    track_update(&tr, boxes, 2, ts);
    track_get(&tr, &out);

    CHECK(!out.coasting, "in-gate candidate must be selected, not treated as a miss");
    CHECK(fabsf(out.cx - 500.0f) > 50.0f, "must NOT jump toward the out-of-gate higher-score candidate(500,182)");
    CHECK(fabsf(out.cx - 330.5f) < 5.0f, "must update toward the in-gate lower-score candidate(332,182), not the out-of-gate one");
}

int main(void) {
    test_motion_frame_rejected();
    test_class_flip_no_jump();
    test_score_flicker_no_break();
    test_bottom_false_positive_rejected();
    test_steady_state_convergence();
    test_coast_lost_recapture();
    test_angle_wraparound();
    test_dual_threshold_boundary();
    test_gate_before_score_pick();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
