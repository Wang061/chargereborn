#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// net_dash 核心逻辑：环形缓冲/URI分类/JSON构建/类别采样门限。
// 无 ESP 依赖，可 host gcc 单测(同 target_track.c 的约定)。
// ESP 侧并发保护(临界区)由调用方(net_dash.c)负责，此处所有函数均非线程安全。

#define DASH_LOG_RING_CAP 16
#define DASH_CLS_NAME_MAX 16
#define DASH_CLASS_SCORE_MIN 0.40f   // 与 http_srv.c 现有 DETECT_OVERLAY_MIN_SCORE 一致

// 单轮处理日志：armctrl_cycle_log_t 的字段原样复制 + 近似关联的检出类别，
// 不直接 #include "armctrl.h"(避免核心逻辑沾 ESP 依赖)，字段名/类型必须与
// armctrl.h 的 armctrl_cycle_log_t 保持一致，由 net_dash.c 负责转换。
typedef struct {
    uint32_t seq_id;
    int64_t  t_identified_us;
    int64_t  t_picked_us;
    int64_t  t_cut_us;
    int64_t  t_placed_us;
    bool     ok;
    char     cls_name[DASH_CLS_NAME_MAX];   // "?" = 无可用采样
    float    cls_score;                      // 0 = 无可用采样
} dash_log_entry_t;

// —— 环形缓冲：定长数组，满后覆盖最旧条目 ——
typedef struct {
    dash_log_entry_t entries[DASH_LOG_RING_CAP];
    uint8_t head;    // 下一个写入位置
    uint8_t count;   // 有效条目数(<=DASH_LOG_RING_CAP)
} dash_ring_t;

void dash_ring_init(dash_ring_t *ring);
void dash_ring_push(dash_ring_t *ring, const dash_log_entry_t *entry);
// 倒序(最新写入的在 out[0])拷贝最多 out_cap 条到 out，返回实际拷贝条数。
int  dash_ring_snapshot(const dash_ring_t *ring, dash_log_entry_t *out, int out_cap);

// —— 类别近似采样缓存 ——
typedef struct {
    char  cls_name[DASH_CLS_NAME_MAX];
    float score;
} dash_class_sample_t;

void dash_class_sample_init(dash_class_sample_t *s);
// score < DASH_CLASS_SCORE_MIN 时忽略、保留旧值；否则更新(cls_name 截断到 DASH_CLS_NAME_MAX-1)。
void dash_class_sample_update(dash_class_sample_t *s, const char *cls_name, float score);

// —— /dash 路由分类：处理通配符路由 "/dash/?*" 命中后的资产分发 ——
typedef enum {
    DASH_ROUTE_INDEX,       // 请求路径(去掉查询串后)是 /dash 或 /dash/
    DASH_ROUTE_APP_JS,      // /dash/app.js
    DASH_ROUTE_STYLES_CSS,  // /dash/styles.css
    DASH_ROUTE_NOT_FOUND,   // 其余子路径
} dash_route_kind_t;

// uri 可能带查询串(如 "/dash/app.js?t=123")，本函数内部按 '?' 截断后再比较，
// 调用方(httpd handler)不需要预处理。uri 为 NULL 时返回 DASH_ROUTE_NOT_FOUND。
dash_route_kind_t dash_classify_uri(const char *uri);

// 构建 /battery_log 响应 JSON。返回值语义同 snprintf：期望写入的总长度(不含结尾NUL)，
// 调用方可用返回值 >= buf_sz 判断是否发生截断。buf_sz==0 时安全返回期望长度、不写任何字节。
int dash_build_battery_log_json(char *buf, size_t buf_sz,
                                 int64_t now_us, uint32_t total, uint32_t session,
                                 float co2_g_per_cell,
                                 const dash_log_entry_t *entries, int entry_count);

#ifdef __cplusplus
}
#endif
