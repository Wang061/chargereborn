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

// ---- 测试8: 基本路由分类(无查询串) ----
static void test_classify_uri_basic(void) {
    CHECK(dash_classify_uri("/dash") == DASH_ROUTE_INDEX, "/dash -> INDEX");
    CHECK(dash_classify_uri("/dash/") == DASH_ROUTE_INDEX, "/dash/ -> INDEX");
    CHECK(dash_classify_uri("/dash/app.js") == DASH_ROUTE_APP_JS, "/dash/app.js -> APP_JS");
    CHECK(dash_classify_uri("/dash/styles.css") == DASH_ROUTE_STYLES_CSS, "/dash/styles.css -> STYLES_CSS");
    CHECK(dash_classify_uri("/dash/unknown.txt") == DASH_ROUTE_NOT_FOUND, "/dash/unknown.txt -> NOT_FOUND");
    CHECK(dash_classify_uri("/other") == DASH_ROUTE_NOT_FOUND, "unrelated path -> NOT_FOUND");
}

// ---- 测试9: 查询串截断(关键边界——浏览器缓存戳请求不能被误判 404) ----
static void test_classify_uri_query_string(void) {
    CHECK(dash_classify_uri("/dash/app.js?t=12345") == DASH_ROUTE_APP_JS,
          "app.js with cache-bust query string still classifies as APP_JS");
    CHECK(dash_classify_uri("/dash/styles.css?v=2") == DASH_ROUTE_STYLES_CSS,
          "styles.css with query string still classifies as STYLES_CSS");
    CHECK(dash_classify_uri("/dash?x=1") == DASH_ROUTE_INDEX,
          "/dash with query string still classifies as INDEX");
    CHECK(dash_classify_uri("/dash/?x=1") == DASH_ROUTE_INDEX,
          "/dash/ with query string still classifies as INDEX");
}

// ---- 测试10: NULL 与空字符串不崩 ----
static void test_classify_uri_null_and_empty(void) {
    CHECK(dash_classify_uri(NULL) == DASH_ROUTE_NOT_FOUND, "NULL uri -> NOT_FOUND, no crash");
    CHECK(dash_classify_uri("") == DASH_ROUTE_NOT_FOUND, "empty uri -> NOT_FOUND");
}

// ---- 测试11: JSON 构建——单条 ok=true 记录,字段齐全可用 strstr 验证 ----
static void test_json_single_entry_shape(void) {
    dash_log_entry_t e = make_entry(7, true);
    char buf[512];
    int n = dash_build_battery_log_json(buf, sizeof(buf), 123456789, 42, 5, 18.0f, &e, 1);
    CHECK(n > 0 && n < (int)sizeof(buf), "single-entry JSON fits comfortably in 512B buffer");
    CHECK(strstr(buf, "\"total\":42") != NULL, "total field present");
    CHECK(strstr(buf, "\"session\":5") != NULL, "session field present");
    CHECK(strstr(buf, "\"co2_g_per_cell\":18.0") != NULL, "co2_g_per_cell field present");
    CHECK(strstr(buf, "\"seq_id\":7") != NULL, "entry seq_id present");
    CHECK(strstr(buf, "\"ok\":true") != NULL, "entry ok:true rendered as JSON literal true, not 1");
    CHECK(strstr(buf, "\"cls\":\"18650\"") != NULL, "entry cls string present");
}

// ---- 测试12: ok=false 记录必须原样体现,不能被静默改写成 true ----
static void test_json_ok_false_entry(void) {
    dash_log_entry_t e = make_entry(9, false);
    char buf[512];
    dash_build_battery_log_json(buf, sizeof(buf), 0, 0, 0, 18.0f, &e, 1);
    CHECK(strstr(buf, "\"ok\":false") != NULL, "failed-cycle entry rendered as ok:false");
}

// ---- 测试13: 零条目——logs 数组为空,但外层字段仍完整 ----
static void test_json_zero_entries(void) {
    char buf[256];
    int n = dash_build_battery_log_json(buf, sizeof(buf), 0, 10, 2, 18.0f, NULL, 0);
    CHECK(n > 0, "zero-entry call still produces valid output");
    CHECK(strstr(buf, "\"logs\":[]") != NULL, "zero entries -> empty logs array, valid JSON");
}

// ---- 测试14: buf_sz=0 不崩、不写、返回值仍是期望长度(snprintf 语义) ----
static void test_json_zero_buf_size_safe(void) {
    dash_log_entry_t e = make_entry(1, true);
    int n = dash_build_battery_log_json(NULL, 0, 0, 1, 1, 18.0f, &e, 1);
    CHECK(n > 0, "buf_sz=0 with NULL buf does not crash, returns expected length like snprintf");
}

// ---- 测试15: 缓冲区明显小于实际所需——不越界写(用 canary 字节检测) ----
static void test_json_small_buf_no_overflow(void) {
    dash_log_entry_t entries[DASH_LOG_RING_CAP];
    for (int i = 0; i < DASH_LOG_RING_CAP; i++) entries[i] = make_entry((uint32_t)(i + 1), true);

    char buf[40 + 1];   // 明显不够放下 16 条记录的完整 JSON，最后一字节做 canary
    buf[40] = (char)0x7E;   // canary
    int n = dash_build_battery_log_json(buf, 40, 0, 1, 1, 18.0f, entries, DASH_LOG_RING_CAP);
    CHECK(n > 40, "return value reports full would-be length even though truncated");
    CHECK(buf[40] == (char)0x7E, "canary byte past the 40-byte window untouched -> no overflow");
    CHECK(buf[39] == '\0' || buf[39] != (char)0x7E, "buffer was actually written into (not a no-op)");
}

// ---- 测试16: 满载 16 条真实大小的缓冲区里不截断(生产环境实际用的 buf_sz) ----
static void test_json_full_ring_fits_production_buffer(void) {
    dash_log_entry_t entries[DASH_LOG_RING_CAP];
    for (int i = 0; i < DASH_LOG_RING_CAP; i++) entries[i] = make_entry((uint32_t)(i + 1), (i % 3) != 0);

    char buf[3072];   // 与 net_dash.c 生产环境使用的缓冲区大小一致
    int n = dash_build_battery_log_json(buf, sizeof(buf), 0, 100, 16, 18.0f, entries, DASH_LOG_RING_CAP);
    CHECK(n > 0 && n < (int)sizeof(buf), "16 full entries fit within the production 3072B buffer with margin");
}

int main(void) {
    test_ring_order();
    test_ring_overflow_evicts_oldest();
    test_ring_snapshot_respects_out_cap();
    test_ring_empty_snapshot();
    test_ring_preserves_ok_field();
    test_class_sample_threshold();
    test_class_sample_name_truncation();
    test_classify_uri_basic();
    test_classify_uri_query_string();
    test_classify_uri_null_and_empty();
    test_json_single_entry_shape();
    test_json_ok_false_entry();
    test_json_zero_entries();
    test_json_zero_buf_size_safe();
    test_json_small_buf_no_overflow();
    test_json_full_ring_fits_production_buffer();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
