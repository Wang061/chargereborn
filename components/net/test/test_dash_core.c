#include "net_dash_core.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

static dash_log_entry_t make_entry(uint32_t seq, bool ok) {
    dash_log_entry_t e = {0};
    e.seq_id = seq;
    e.ok = ok;
    e.t_identified_us = (int64_t)seq * 1000;
    strncpy(e.cls_name, "18650", sizeof(e.cls_name) - 1);
    e.cls_score = 0.9f;
    return e;
}

// ---- 测试1: 基本 push/snapshot 顺序(倒序,最新在前) ----
static void test_ring_order(void) {
    dash_ring_t r;
    dash_ring_init(&r);
    for (uint32_t i = 1; i <= 3; i++) {
        dash_log_entry_t e = make_entry(i, true);
        dash_ring_push(&r, &e);
    }
    dash_log_entry_t out[3];
    int n = dash_ring_snapshot(&r, out, 3);
    CHECK(n == 3, "3 pushes -> snapshot returns 3");
    CHECK(out[0].seq_id == 3, "newest(seq=3) is out[0]");
    CHECK(out[1].seq_id == 2, "middle(seq=2) is out[1]");
    CHECK(out[2].seq_id == 1, "oldest(seq=1) is out[2]");
}

// ---- 测试2: 超过容量后覆盖最旧条目,count 封顶在 CAP ----
static void test_ring_overflow_evicts_oldest(void) {
    dash_ring_t r;
    dash_ring_init(&r);
    for (uint32_t i = 1; i <= DASH_LOG_RING_CAP + 3; i++) {
        dash_log_entry_t e = make_entry(i, true);
        dash_ring_push(&r, &e);
    }
    CHECK(r.count == DASH_LOG_RING_CAP, "count caps at DASH_LOG_RING_CAP after overflow");
    dash_log_entry_t out[DASH_LOG_RING_CAP];
    int n = dash_ring_snapshot(&r, out, DASH_LOG_RING_CAP);
    CHECK(n == DASH_LOG_RING_CAP, "snapshot returns full CAP after overflow");
    CHECK(out[0].seq_id == DASH_LOG_RING_CAP + 3, "newest entry is still the most recent push");
    CHECK(out[DASH_LOG_RING_CAP - 1].seq_id == 4, "oldest surviving entry is push #4 (1..3 evicted)");
}

// ---- 测试3: snapshot 的 out_cap 小于实际条数时按 out_cap 截断,不越界写 ----
static void test_ring_snapshot_respects_out_cap(void) {
    dash_ring_t r;
    dash_ring_init(&r);
    for (uint32_t i = 1; i <= 5; i++) {
        dash_log_entry_t e = make_entry(i, true);
        dash_ring_push(&r, &e);
    }
    dash_log_entry_t out[2];
    int n = dash_ring_snapshot(&r, out, 2);
    CHECK(n == 2, "out_cap=2 with 5 entries -> snapshot returns exactly 2");
    CHECK(out[0].seq_id == 5, "still newest-first with truncated out_cap");
}

// ---- 测试4: 空环形缓冲 snapshot 返回 0,不崩 ----
static void test_ring_empty_snapshot(void) {
    dash_ring_t r;
    dash_ring_init(&r);
    dash_log_entry_t out[4];
    int n = dash_ring_snapshot(&r, out, 4);
    CHECK(n == 0, "empty ring -> snapshot returns 0");
}

// ---- 测试5: ok=false 的条目原样保留(失败轮次也要能在溯源列表里区分) ----
static void test_ring_preserves_ok_field(void) {
    dash_ring_t r;
    dash_ring_init(&r);
    dash_log_entry_t fail_e = make_entry(1, false);
    dash_ring_push(&r, &fail_e);
    dash_log_entry_t out[1];
    dash_ring_snapshot(&r, out, 1);
    CHECK(out[0].ok == false, "failed cycle entry keeps ok=false through ring round-trip");
}

// ---- 测试6: 类别采样门限——低于 0.40 不更新,保留旧值 ----
static void test_class_sample_threshold(void) {
    dash_class_sample_t s;
    dash_class_sample_init(&s);
    CHECK(strcmp(s.cls_name, "?") == 0, "init default cls_name is \"?\"");
    CHECK(s.score == 0.0f, "init default score is 0");

    dash_class_sample_update(&s, "9V", 0.39f);
    CHECK(strcmp(s.cls_name, "?") == 0, "score below 0.40 threshold does not update cls_name");

    dash_class_sample_update(&s, "9V", 0.40f);
    CHECK(strcmp(s.cls_name, "9V") == 0, "score at exactly 0.40 threshold does update");
    CHECK(s.score == 0.40f, "score field updated together with cls_name");

    dash_class_sample_update(&s, "18650", 0.10f);
    CHECK(strcmp(s.cls_name, "9V") == 0, "subsequent low-score update does not overwrite previous good sample");
}

// ---- 测试7: 类别名截断,不溢出/不缺 NUL 终止符 ----
static void test_class_sample_name_truncation(void) {
    dash_class_sample_t s;
    dash_class_sample_init(&s);
    dash_class_sample_update(&s, "a_name_that_is_way_too_long_for_the_buffer", 0.99f);
    CHECK(strlen(s.cls_name) == DASH_CLS_NAME_MAX - 1, "long cls_name truncated to buffer-1 chars");
    CHECK(s.cls_name[DASH_CLS_NAME_MAX - 1] == '\0', "truncated cls_name still NUL-terminated");
}

int main(void) {
    test_ring_order();
    test_ring_overflow_evicts_oldest();
    test_ring_snapshot_respects_out_cap();
    test_ring_empty_snapshot();
    test_ring_preserves_ok_field();
    test_class_sample_threshold();
    test_class_sample_name_truncation();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
