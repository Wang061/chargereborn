# ChargeReborn Dashboard 固件 HTTP 直连联调 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `chargereborn-dashboard` 网页作为固件内嵌页（`/dash/`）真正联通固件——急停按钮从死路（只走不存在的 WebSocket）接到已有的 `/arm_estop`，电池溯源/碳减排面板从假数据切到 armctrl 事件钩子驱动的真数据，全程不做 WebSocket 层、不改 sdkconfig。

**Architecture:** `components/net` 新增一对文件——`net_dash_core.{c,h}`（纯 C、无 ESP 依赖、host 可单测：环形缓冲、URI 分类、JSON 构建、类别采样门限）+ `net_dash.{c,h}`（ESP 胶水层：httpd handler、`EMBED_TXTFILES` 内嵌资产、`esp_timer` 周期采样、`armctrl_set_event_cb` 注册）。`http_srv.c` 只加两行挂接。网页侧改 3 个文件（`app.js`/`index.html`/`mock-server.js`），核心是把 `sendEstop()` 从只会 `sendMessage`（WebSocket）改成 HTTP 直连模式下真打 `/arm_estop`。

**Tech Stack:** ESP-IDF 5.5.4 / esp32s3、`esp_http_server`（复用现有 80 口 httpd 实例）、`esp_timer`、原生 JS（无框架）、Node.js 内置 `http` 模块（`mock-server.js`，无 npm 依赖）。

## Global Constraints

- `IDF_VERSION=5.5.4`、`IDF_TARGET=esp32s3`（项目版本锁，不得违反）。
- 不改 `sdkconfig`（本设计不需要 `CONFIG_HTTPD_WS_SUPPORT`）。
- 不碰 `components/armctrl`/`components/armlink`/`components/ai` 内部实现——只用已暴露的公开 API（`armctrl_set_event_cb`/`armctrl_get_stats`/`armctrl_estop`/`ai_get_last`/`ai_class_name`）。
- `armctrl_set_event_cb` 只在系统启动时注册一次，不重复注册/注销。
- 急停路径只能经 `armctrl_estop()`（已由现有 `/arm_estop` 端点调用），网页侧不得自造协议帧。
- 碳系数唯一数值来源：`components/net/net_dash.c` 的 `#define CARBON_G_PER_CELL 18.0f`；其余代码（含前端）一律从 `/battery_log` 响应的 `co2_g_per_cell` 字段读取，不硬编码。
- 环形缓冲容量 `DASH_LOG_RING_CAP = 16`；类别采样门限 `0.40f`（与 `http_srv.c` 现有 `DETECT_OVERLAY_MIN_SCORE` 一致）。
- flash 操作必须当场向用户确认（本项目铁律），本计划里每处 flash 步骤都会明确标出。
- host 单测统一用项目既有约定：`env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 <test.c> <impl.c> -I<component>/include -lm -o /tmp/<name> && /tmp/<name>`（与 `components/armlink/test/test_track.c` 的验证方式一致；**-D__USE_MINGW_ANSI_STDIO=1 必需**——此 mingw 工具链默认链接的 snprintf 截断时返回 -1 而非 C99 规定的"期望写入长度"，加这个宏切到 mingw 自带的 C99 兼容实现；ESP32 目标端 newlib 的 snprintf 本身就是标准行为，不受此坑影响，只有 host 单测需要这个 flag，2026-07-08 实测发现）。
- 日志 TAG 统一用 `"net_dash"`；不用裸 `printf` 做正式固件日志。
- 前置设计文档：`docs/superpowers/specs/2026-07-08-dashboard-firmware-integration-design.md`（已用户审阅通过）。

---

## Task 1: Git 卫生——先提交现有 WIP（与本功能无关，仅按设计 §7 顺序要求执行）

当前工作区有 3 个文件的未提交改动，是这次 dashboard 任务开始前就存在的、与本功能完全无关的机械臂抓取位姿调优工作（`docs/ai/GRASP_ACCURACY_PLAN.md` 相关）。必须先把这批 WIP 单独提交，本计划的改动才能干净叠加，互不污染 diff。

**Files:**
- Commit only: `components/armctrl/armctrl.c`、`components/net/http_srv.c`、`docs/ai/ARM_PIPELINE.md`

**⚠️ 重要边界**：工作区还有其他未跟踪内容（`.agents/`、`.codex/`、`AGENTS.md`、`docs/workbench/`、`projects/`、`docs/ai/GRASP_ACCURACY_PLAN.md`、`reference/` 下的截图/照片）——这些不属于本次提交范围，**严禁**用 `git add -A` 或 `git add .`，只能显式点名这 3 个文件。

**Interfaces:**
- Produces: 一个干净的 baseline commit，后续 Task 2-11 的 diff 都基于此提交之上。

- [ ] **Step 1: 核对当前状态与目标提交内容**

```bash
git status --short
git diff --stat components/armctrl/armctrl.c components/net/http_srv.c docs/ai/ARM_PIPELINE.md
```

Expected: 看到这 3 个文件的 `M` 状态和非零的 insertions/deletions；其余未跟踪路径（`.agents/` 等）保持 `??`，本步骤不动它们。

- [ ] **Step 2: 精确提交这 3 个文件**

```bash
git add components/armctrl/armctrl.c components/net/http_srv.c docs/ai/ARM_PIPELINE.md
git commit -m "refactor(armctrl,net): 抓取位姿计算重构为grasp_pose_t(底座朝向/腕相对角解耦,补偿项归零待标定);控制页收敛移除旧采集UI"
```

Expected: `git commit` 成功；`git status --short` 中这 3 个文件消失，其余未跟踪路径不受影响。

- [ ] **Step 3: 确认工作区只剩预期的未跟踪内容**

```bash
git status --short
```

Expected: 只剩 `??` 开头的未跟踪路径（`.agents/`、`.codex/`、`AGENTS.md`、`chargereborn-dashboard/`、`docs/ai/GRASP_ACCURACY_PLAN.md`、`docs/workbench/`、`projects/`、`reference/source/*`、截图 png），没有任何 `M`（已修改未提交）文件。这就是 Task 2 开始前的干净基线。

---

## Task 2: `net_dash_core` — 环形缓冲 + 类别采样缓存（host TDD）

纯 C 数据结构，无 ESP-IDF 依赖，遵循 `components/armlink/target_track.c` 的既有 host-测试惯例。这是 net_dash 的第一批核心逻辑：把 `armctrl_cycle_log_t` 包一层（加近似类别字段）存进定长环形缓冲，以及"最近一次高置信度检出"的采样缓存。

**Files:**
- Create: `components/net/include/net_dash_core.h`
- Create: `components/net/net_dash_core.c`
- Create: `components/net/test/test_dash_core.c`

**Interfaces:**
- Produces（供 Task 3/4/5/6 使用的精确签名）：
  - `#define DASH_LOG_RING_CAP 16`
  - `#define DASH_CLS_NAME_MAX 16`
  - `typedef struct { uint32_t seq_id; int64_t t_identified_us, t_picked_us, t_cut_us, t_placed_us; bool ok; char cls_name[DASH_CLS_NAME_MAX]; float cls_score; } dash_log_entry_t;`
  - `typedef struct { dash_log_entry_t entries[DASH_LOG_RING_CAP]; uint8_t head; uint8_t count; } dash_ring_t;`
  - `void dash_ring_init(dash_ring_t *ring);`
  - `void dash_ring_push(dash_ring_t *ring, const dash_log_entry_t *entry);`
  - `int dash_ring_snapshot(const dash_ring_t *ring, dash_log_entry_t *out, int out_cap);`（倒序：最新在前；返回实际拷贝条数）
  - `typedef struct { char cls_name[DASH_CLS_NAME_MAX]; float score; } dash_class_sample_t;`
  - `void dash_class_sample_init(dash_class_sample_t *s);`（初始化为 `"?"`/0）
  - `void dash_class_sample_update(dash_class_sample_t *s, const char *cls_name, float score);`（`score < 0.40f` 时不更新）

- [ ] **Step 1: 写头文件（数据结构与函数原型，先于实现和测试）**

```c
// components/net/include/net_dash_core.h
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

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写失败测试（覆盖：push/snapshot 顺序、满后覆盖、采样门限）**

```c
// components/net/test/test_dash_core.c
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
```

- [ ] **Step 3: 运行测试，确认因缺少实现而失败**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test
```

Expected: 编译失败（`net_dash_core.c` 尚不存在/为空）。若此步骤因为你已经手滑写了实现而编译通过，先清空 `net_dash_core.c` 内容重来——这一步的目的是确认测试确实在检验真实逻辑，不是空转。

- [ ] **Step 4: 写最小实现**

```c
// components/net/net_dash_core.c
#include "net_dash_core.h"
#include <string.h>

void dash_ring_init(dash_ring_t *ring)
{
    ring->head = 0;
    ring->count = 0;
}

void dash_ring_push(dash_ring_t *ring, const dash_log_entry_t *entry)
{
    ring->entries[ring->head] = *entry;
    ring->head = (uint8_t)((ring->head + 1) % DASH_LOG_RING_CAP);
    if (ring->count < DASH_LOG_RING_CAP) ring->count++;
}

int dash_ring_snapshot(const dash_ring_t *ring, dash_log_entry_t *out, int out_cap)
{
    int n = ring->count;
    if (n > out_cap) n = out_cap;
    for (int i = 0; i < n; i++) {
        int idx = (ring->head - 1 - i + DASH_LOG_RING_CAP) % DASH_LOG_RING_CAP;
        out[i] = ring->entries[idx];
    }
    return n;
}

void dash_class_sample_init(dash_class_sample_t *s)
{
    strncpy(s->cls_name, "?", DASH_CLS_NAME_MAX - 1);
    s->cls_name[DASH_CLS_NAME_MAX - 1] = '\0';
    s->score = 0.0f;
}

void dash_class_sample_update(dash_class_sample_t *s, const char *cls_name, float score)
{
    if (score < DASH_CLASS_SCORE_MIN) return;
    strncpy(s->cls_name, cls_name, DASH_CLS_NAME_MAX - 1);
    s->cls_name[DASH_CLS_NAME_MAX - 1] = '\0';
    s->score = score;
}
```

- [ ] **Step 5: 运行测试，确认全部通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test && /tmp/dash_core_test
```

Expected: 输出 `ALL PASS`，退出码 0。

- [ ] **Step 6: Commit**

```bash
git add components/net/include/net_dash_core.h components/net/net_dash_core.c components/net/test/test_dash_core.c
git commit -m "feat(net): net_dash_core环形缓冲+类别采样缓存(host单测7条)"
```

---

## Task 3: `net_dash_core` — URI 分类器（host TDD，含查询串截断边界）

`/dash/?*` 通配符路由命中后，需要一个纯逻辑函数判断这个请求到底要哪个资产。**关键边界**：`httpd_req_t.uri` 含查询串（如 `?t=123` 的缓存戳），朴素字符串比较会在这种请求上误判成 404——这个函数必须先按 `?` 截断再比较。

**Files:**
- Modify: `components/net/include/net_dash_core.h`（追加枚举+函数原型）
- Modify: `components/net/net_dash_core.c`（追加实现）
- Modify: `components/net/test/test_dash_core.c`（追加测试函数 + `main()` 里的调用）

**Interfaces:**
- Consumes: 无（独立于 Task 2 的环形缓冲/采样逻辑）
- Produces（供 Task 5 使用）：
  - `typedef enum { DASH_ROUTE_INDEX, DASH_ROUTE_APP_JS, DASH_ROUTE_STYLES_CSS, DASH_ROUTE_NOT_FOUND } dash_route_kind_t;`
  - `dash_route_kind_t dash_classify_uri(const char *uri);`

- [ ] **Step 1: 头文件追加枚举与函数原型**

在 `components/net/include/net_dash_core.h` 的 `dash_class_sample_update` 原型声明之后、`#ifdef __cplusplus\n}\n#endif` 之前插入：

```c
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
```

- [ ] **Step 2: 测试追加（含查询串截断这个关键边界用例）**

在 `components/net/test/test_dash_core.c` 的 `test_class_sample_name_truncation` 函数之后插入：

```c
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
```

并把 `main()` 更新为：

```c
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
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: 运行测试，确认新增的 3 个测试函数因缺少实现而失败**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test
```

Expected: 编译失败（`dash_classify_uri` 未定义，链接或编译报错）。

- [ ] **Step 4: 实现 `dash_classify_uri`**

在 `components/net/net_dash_core.c` 末尾追加：

```c
dash_route_kind_t dash_classify_uri(const char *uri)
{
    if (uri == NULL) return DASH_ROUTE_NOT_FOUND;

    size_t len = 0;
    while (uri[len] != '\0' && uri[len] != '?') len++;   // 截断查询串

    if (len == 5 && strncmp(uri, "/dash", 5) == 0)          return DASH_ROUTE_INDEX;
    if (len == 6 && strncmp(uri, "/dash/", 6) == 0)         return DASH_ROUTE_INDEX;
    if (len == 12 && strncmp(uri, "/dash/app.js", 12) == 0) return DASH_ROUTE_APP_JS;
    if (len == 16 && strncmp(uri, "/dash/styles.css", 16) == 0) return DASH_ROUTE_STYLES_CSS;
    return DASH_ROUTE_NOT_FOUND;
}
```

- [ ] **Step 5: 运行测试，确认全部通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test && /tmp/dash_core_test
```

Expected: 输出 `ALL PASS`（10 个测试函数全部通过），退出码 0。

- [ ] **Step 6: Commit**

```bash
git add components/net/include/net_dash_core.h components/net/net_dash_core.c components/net/test/test_dash_core.c
git commit -m "feat(net): net_dash_core URI分类器,含查询串截断边界(host单测+3条)"
```

---

## Task 4: `net_dash_core` — `/battery_log` JSON 构建（host TDD，含截断安全）

把环形缓冲快照 + 统计计数 + 碳系数序列化成 `/battery_log` 的响应 JSON。用项目既有的链式 `snprintf` 累加惯例（同 `http_srv.c` 的 `detect_get`），但额外保证对任意 `buf_sz`（包括 0、包括小于实际所需的值）都不会指针越界或无符号下溢——这是新写的函数，值得比 `detect_get` 现有代码更严格一档。

**Files:**
- Modify: `components/net/include/net_dash_core.h`
- Modify: `components/net/net_dash_core.c`
- Modify: `components/net/test/test_dash_core.c`

**Interfaces:**
- Consumes: `dash_log_entry_t`（Task 2）
- Produces（供 Task 6 使用）：
  - `int dash_build_battery_log_json(char *buf, size_t buf_sz, int64_t now_us, uint32_t total, uint32_t session, float co2_g_per_cell, const dash_log_entry_t *entries, int entry_count);`（返回值语义同 `snprintf`：期望写入的长度，可能 `>= buf_sz` 表示发生了截断）

- [ ] **Step 1: 头文件追加函数原型**

在 `components/net/include/net_dash_core.h` 的 `dash_classify_uri` 原型之后插入：

```c
// 构建 /battery_log 响应 JSON。返回值语义同 snprintf：期望写入的总长度(不含结尾NUL)，
// 调用方可用返回值 >= buf_sz 判断是否发生截断。buf_sz==0 时安全返回期望长度、不写任何字节。
int dash_build_battery_log_json(char *buf, size_t buf_sz,
                                 int64_t now_us, uint32_t total, uint32_t session,
                                 float co2_g_per_cell,
                                 const dash_log_entry_t *entries, int entry_count);
```

- [ ] **Step 2: 测试追加（含 0 字节缓冲、过小缓冲两个截断边界）**

在 `components/net/test/test_dash_core.c` 追加（`test_classify_uri_null_and_empty` 之后），并在文件顶部追加 `#include <stdlib.h>`（本任务不需要，跳过——只需保留已有 include）：

```c
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
```

并把 `main()` 更新为：

```c
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
```

- [ ] **Step 3: 运行测试，确认因缺少实现而失败**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test
```

Expected: 编译失败（`dash_build_battery_log_json` 未定义）。

- [ ] **Step 4: 实现 `dash_build_battery_log_json`**

在 `components/net/net_dash_core.c` 末尾追加（需要文件顶部已有 `#include <string.h>`；追加 `#include <stdio.h>`）：

```c
#include <stdio.h>

int dash_build_battery_log_json(char *buf, size_t buf_sz,
                                 int64_t now_us, uint32_t total, uint32_t session,
                                 float co2_g_per_cell,
                                 const dash_log_entry_t *entries, int entry_count)
{
    int n = snprintf(buf, buf_sz,
        "{\"now_us\":%lld,\"total\":%u,\"session\":%u,\"co2_g_per_cell\":%.1f,\"logs\":[",
        (long long)now_us, (unsigned)total, (unsigned)session, (double)co2_g_per_cell);

    for (int i = 0; i < entry_count; i++) {
        const dash_log_entry_t *e = &entries[i];
        char *p = (n < 0 || (size_t)n >= buf_sz) ? buf + buf_sz : buf + n;
        size_t room = (n < 0 || (size_t)n >= buf_sz) ? 0 : buf_sz - (size_t)n;
        n += snprintf(p, room,
            "%s{\"seq_id\":%u,\"ok\":%s,\"cls\":\"%s\",\"score\":%.2f,"
            "\"t_identified_us\":%lld,\"t_picked_us\":%lld,\"t_cut_us\":%lld,\"t_placed_us\":%lld}",
            i ? "," : "",
            (unsigned)e->seq_id, e->ok ? "true" : "false", e->cls_name, (double)e->cls_score,
            (long long)e->t_identified_us, (long long)e->t_picked_us,
            (long long)e->t_cut_us, (long long)e->t_placed_us);
    }

    char *p = (n < 0 || (size_t)n >= buf_sz) ? buf + buf_sz : buf + n;
    size_t room = (n < 0 || (size_t)n >= buf_sz) ? 0 : buf_sz - (size_t)n;
    n += snprintf(p, room, "]}");
    return n;
}
```

- [ ] **Step 5: 运行测试，确认全部通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc -D__USE_MINGW_ANSI_STDIO=1 components/net/test/test_dash_core.c components/net/net_dash_core.c -Icomponents/net/include -lm -o /tmp/dash_core_test && /tmp/dash_core_test
```

Expected: 输出 `ALL PASS`（16 个测试函数全部通过），退出码 0。这是 `net_dash_core` 的最终形态，之后不再修改，只被 `net_dash.c` 调用。

- [ ] **Step 6: Commit**

```bash
git add components/net/include/net_dash_core.h components/net/net_dash_core.c components/net/test/test_dash_core.c
git commit -m "feat(net): net_dash_core /battery_log JSON构建,含截断安全边界(host单测+6条,累计16条)"
```

---

## Task 5: 固件接线 A——内嵌静态资产 + `/dash` 路由 + `http_srv.c` 挂接

第一次把 `net_dash_core` 接进真实 ESP-IDF 构建。目标：浏览器打开 `http://192.168.4.1/dash/` 能看到 dashboard 页面，且**现有 8 个端点（`/detect`、`/arm_target` 等）行为不受影响**——这是本任务最大的回归风险点，因为 `uri_match_fn` 是整个 httpd 实例级别的全局设置。

**Files:**
- Create: `components/net/include/net_dash.h`
- Create: `components/net/net_dash.c`
- Modify: `components/net/CMakeLists.txt`
- Modify: `components/net/http_srv.c`

**Interfaces:**
- Consumes: `dash_classify_uri`（Task 3）
- Produces（供 Task 6 使用）：`void net_dash_register(httpd_handle_t server);`（Task 6 会在这个函数体内追加 `/battery_log` 的注册逻辑）

- [ ] **Step 1: 头文件**

```c
// components/net/include/net_dash.h
#pragma once
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// 注册 dashboard 相关 URI handler(/dash/*、/battery_log)到已有的 httpd 实例。
// 调用前 server 必须已 httpd_start() 成功，且 server 的 config.uri_match_fn
// 必须已设为 httpd_uri_match_wildcard(由调用方在 httpd_config_t 里配置，
// 见 http_srv.c 的 net_http_start)。只应在系统启动时调用一次。
void net_dash_register(httpd_handle_t server);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 实现（本任务只做静态资产路由，不含 `/battery_log`——留给 Task 6）**

```c
// components/net/net_dash.c
#include "net_dash.h"
#include "net_dash_core.h"
#include "esp_log.h"

static const char *TAG = "net_dash";

// EMBED_TXTFILES 生成的符号(见 CMakeLists.txt)：文件名中的 '.' 变成 '_'。
// EMBED_TXTFILES 会自动追加 NUL 终止符，故用 HTTPD_RESP_USE_STRLEN 发送即可，
// 不需要手动算 (_end - _start)。
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");

static esp_err_t dash_get(httpd_req_t *req)
{
    switch (dash_classify_uri(req->uri)) {
    case DASH_ROUTE_INDEX:
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
    case DASH_ROUTE_APP_JS:
        httpd_resp_set_type(req, "application/javascript; charset=utf-8");
        return httpd_resp_send(req, (const char *)app_js_start, HTTPD_RESP_USE_STRLEN);
    case DASH_ROUTE_STYLES_CSS:
        httpd_resp_set_type(req, "text/css; charset=utf-8");
        return httpd_resp_send(req, (const char *)styles_css_start, HTTPD_RESP_USE_STRLEN);
    default:
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "unknown /dash asset");
        return ESP_FAIL;
    }
}

void net_dash_register(httpd_handle_t server)
{
    httpd_uri_t dash = { .uri = "/dash/?*", .method = HTTP_GET, .handler = dash_get };
    httpd_register_uri_handler(server, &dash);
    ESP_LOGI(TAG, "dash registered: /dash/");
}
```

- [ ] **Step 3: `CMakeLists.txt` 加 `net_dash_core.c`/`net_dash.c` 到 SRCS + `EMBED_TXTFILES` 内嵌三个 dashboard 资产**

```cmake
# components/net/CMakeLists.txt
idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c" "net_dash_core.c" "net_dash.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "../../chargereborn-dashboard/dashboard/index.html"
                                      "../../chargereborn-dashboard/dashboard/app.js"
                                      "../../chargereborn-dashboard/dashboard/styles.css"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera ai armlink armcal armctrl)
```

路径用 `../../` 是因为 `chargereborn-dashboard/` 是仓库根下的独立顶层目录（不在 `components/net/` 下），单一数据源（不复制文件），队友改 dashboard 源文件后下次固件构建自动带上最新版本。

- [ ] **Step 4: `http_srv.c` 两行改动——`uri_match_fn` + `net_dash_register` 调用**

用 Edit 工具在 `components/net/http_srv.c` 里做以下两处精确替换：

第一处（`net_http_start` 函数开头的 config 初始化）：

```c
// old_string:
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;   // 默认 4096 不够: detect_get 的 buf[1536]+ai_result_t 会撑爆 httpd 任务栈 → 卡死/崩溃
    config.max_uri_handlers = 16;   // 当前 8 个 URI,保留余量给调试/后续 dashboard

// new_string:
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;   // 默认 4096 不够: detect_get 的 buf[1536]+ai_result_t 会撑爆 httpd 任务栈 → 卡死/崩溃
    config.max_uri_handlers = 16;   // 现有 8 个 + /dash 通配 1 个 + /battery_log 1 个(见net_dash.c) = 10
    // 通配符匹配器：对不含 */? 的模板要求长度完全相等才命中(ESP-IDF httpd_uri.c 已核实)，
    // 即以下现有 8 个精确路径 handler 语义不变，只有 net_dash_register 注册的带通配符模板才启用模糊匹配。
    config.uri_match_fn = httpd_uri_match_wildcard;
```

第二处（`net_http_start` 函数末尾）：

```c
// old_string:
    httpd_uri_t estop = { .uri = "/arm_estop", .method = HTTP_GET, .handler = arm_estop_get };
    httpd_register_uri_handler(server, &estop);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}

// new_string:
    httpd_uri_t estop = { .uri = "/arm_estop", .method = HTTP_GET, .handler = arm_estop_get };
    httpd_register_uri_handler(server, &estop);
    net_dash_register(server);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}
```

并在文件顶部 `#include` 区加一行：

```c
// old_string:
#include "armctrl.h"

// new_string:
#include "armctrl.h"
#include "net_dash.h"
```

- [ ] **Step 5: 编译**

```
mcp__idf-bridge__build
```

Expected: 绿。若报 `_binary_index_html_start` 未定义/链接错误，检查 `EMBED_TXTFILES` 的相对路径是否正确解析到 `chargereborn-dashboard/dashboard/` 下的三个文件。

- [ ] **Step 6: 烧录（⚠️ 当场向用户确认后才能执行）**

```
mcp__idf-bridge__flash
```

- [ ] **Step 7: 浏览器验证——新路由可用，且现有端点零回归**

连接板子 SoftAP 后，依次验证：

1. 打开 `http://192.168.4.1/dash/` → 看到 dashboard 页面（HTML 渲染出来，不是 404/空白）。
2. 打开 `http://192.168.4.1/dash`（无斜杠）→ 同样渲染出 dashboard 页面。
3. 打开 `http://192.168.4.1/dash/app.js` → 看到 JS 源码文本，不是 404。
4. 打开 `http://192.168.4.1/dash/styles.css` → 看到 CSS 源码文本，不是 404。
5. 打开 `http://192.168.4.1/detect` → 仍返回原有的检测 JSON（回归检查：`uri_match_fn` 改动没有破坏精确路径匹配）。
6. 打开 `http://192.168.4.1/` → 仍是原有的识别控制页（根路径 handler 未受影响）。
7. 打开 `http://192.168.4.1/dash/nonexistent.txt` → 返回 404（未知子路径正确落空，不会误命中 index）。

Expected: 全部 7 项符合预期。

- [ ] **Step 8: Commit**

```bash
git add components/net/include/net_dash.h components/net/net_dash.c components/net/CMakeLists.txt components/net/http_srv.c
git commit -m "feat(net): 固件内嵌chargereborn-dashboard静态页,/dash路由(uri_match_fn全局改通配符,现有端点零回归)"
```

---

## Task 6: 固件接线 B——`/battery_log` 端点 + `armctrl` 事件钩子 + `esp_timer` 类别采样

在 Task 5 的基础上追加真正的数据面：环形缓冲的读写、`armctrl_set_event_cb` 的一次性注册、`esp_timer` 周期采样检出类别。这是本功能里**唯一引入新并发模式**的任务（`armctrl_task` 写、httpd 任务读、`esp_timer` Timer Task 写，三者共享环形缓冲/采样缓存），按项目规则需要 build+flash+monitor 验证。

**Files:**
- Modify: `components/net/net_dash.c`（在 Task 5 基础上追加，非重写）
- Modify: `components/net/CMakeLists.txt`（`PRIV_REQUIRES` 加 `esp_timer`）

**Interfaces:**
- Consumes: `dash_ring_t`/`dash_ring_push`/`dash_ring_snapshot`（Task 2）、`dash_class_sample_t`/`dash_class_sample_update`（Task 2）、`dash_build_battery_log_json`（Task 4）、`armctrl_set_event_cb`/`armctrl_cycle_log_t`/`armctrl_get_stats`（`armctrl.h`，已存在）、`ai_get_last`/`ai_class_name`/`ai_result_t`（`ai.h`，已存在）
- Produces: `net_dash_register()` 现在额外注册 `/battery_log` GET 端点，返回 JSON 结构见设计文档 §4.4。

- [ ] **Step 1: 在 `net_dash.c` 顶部追加 include 与静态状态**

用 Edit 工具在 `components/net/net_dash.c` 做以下替换：

```c
// old_string:
#include "net_dash.h"
#include "net_dash_core.h"
#include "esp_log.h"

static const char *TAG = "net_dash";

// new_string:
#include "net_dash.h"
#include "net_dash_core.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "armctrl.h"
#include "ai.h"
#include <string.h>   // strncpy(on_armctrl_cycle 拷贝类别名用)

static const char *TAG = "net_dash";

// 反推自 dashboard demo 假数据(总量126g/一周7颗≈18g/颗)，答辩口径与队友原型保持一致；
// 如有实测/官方系数，改这一处即可——固件端唯一数值来源，前端从 /battery_log 响应读取。
#define CARBON_G_PER_CELL 18.0f

#define DASH_SAMPLE_PERIOD_US 500000   // 类别采样周期 500ms，见设计 §4.3
#define DASH_BATTERY_LOG_BUF_SZ 3072   // 16条×~150B/条+头部≈2.5KB留余量；

static dash_ring_t s_ring;
static dash_class_sample_t s_class_sample;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;   // 保护 s_ring 与 s_class_sample
static esp_timer_handle_t s_sample_timer;
```

- [ ] **Step 2: 追加 `armctrl` 事件回调与 `esp_timer` 采样回调（在 `dash_get` 函数之前插入）**

```c
// old_string:
static esp_err_t dash_get(httpd_req_t *req)

// new_string:
// armctrl_task 内同步调用：必须快速返回、不可阻塞(armctrl.h 的硬约束)。
// 只做 memcpy 到环形缓冲，不做 JSON 格式化。
static void on_armctrl_cycle(const armctrl_cycle_log_t *log, void *arg)
{
    (void)arg;
    dash_log_entry_t entry = {
        .seq_id = log->seq_id,
        .t_identified_us = log->t_identified_us,
        .t_picked_us = log->t_picked_us,
        .t_cut_us = log->t_cut_us,
        .t_placed_us = log->t_placed_us,
        .ok = log->ok,
    };
    portENTER_CRITICAL(&s_ring_lock);
    strncpy(entry.cls_name, s_class_sample.cls_name, DASH_CLS_NAME_MAX - 1);
    entry.cls_name[DASH_CLS_NAME_MAX - 1] = '\0';
    entry.cls_score = s_class_sample.score;
    dash_ring_push(&s_ring, &entry);
    portEXIT_CRITICAL(&s_ring_lock);
}

// esp_timer 周期回调(Timer Task 上下文)：ai_get_last() 是线程安全的只读缓存读取(ai.h 注释)，
// 系统刚启动、ai_init() 还没跑完时 r.count==0，本函数直接空转返回，不会读到脏数据。
static void sample_timer_cb(void *arg)
{
    (void)arg;
    ai_result_t r;
    ai_get_last(&r);
    if (r.count <= 0) return;
    portENTER_CRITICAL(&s_ring_lock);
    dash_class_sample_update(&s_class_sample, ai_class_name(r.boxes[0].cls), r.boxes[0].score);
    portEXIT_CRITICAL(&s_ring_lock);
}

static esp_err_t battery_log_get(httpd_req_t *req)
{
    dash_log_entry_t local[DASH_LOG_RING_CAP];
    int n_entries;
    portENTER_CRITICAL(&s_ring_lock);
    n_entries = dash_ring_snapshot(&s_ring, local, DASH_LOG_RING_CAP);
    portEXIT_CRITICAL(&s_ring_lock);

    uint32_t total, session;
    armctrl_get_stats(&total, &session);

    char buf[DASH_BATTERY_LOG_BUF_SZ];   // 栈缓冲,同 detect_get 既有惯例(httpd单工作任务不并发);
                                          // 3072B 远小于 detect_get 的 1536B+ai_result_t 量级,httpd任务栈8192B充裕
    int n = dash_build_battery_log_json(buf, sizeof(buf), esp_timer_get_time(),
                                         total, session, CARBON_G_PER_CELL,
                                         local, n_entries);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf));
}

static esp_err_t dash_get(httpd_req_t *req)
```

- [ ] **Step 3: 扩展 `net_dash_register`——注册 `/battery_log`、初始化环形缓冲/采样缓存、注册 armctrl 钩子、启动 esp_timer**

```c
// old_string:
void net_dash_register(httpd_handle_t server)
{
    httpd_uri_t dash = { .uri = "/dash/?*", .method = HTTP_GET, .handler = dash_get };
    httpd_register_uri_handler(server, &dash);
    ESP_LOGI(TAG, "dash registered: /dash/");
}

// new_string:
void net_dash_register(httpd_handle_t server)
{
    dash_ring_init(&s_ring);
    dash_class_sample_init(&s_class_sample);

    httpd_uri_t dash = { .uri = "/dash/?*", .method = HTTP_GET, .handler = dash_get };
    httpd_register_uri_handler(server, &dash);
    httpd_uri_t blog = { .uri = "/battery_log", .method = HTTP_GET, .handler = battery_log_get };
    httpd_register_uri_handler(server, &blog);

    // 系统启动时注册一次，终身持有——不重复注册/注销(armctrl.h 边界要求)。
    // net_http_start() 在 main.c 里跑在 armctrl_init() 之前也没关系：这里只是存函数指针，
    // 真正被调用是在 armctrl_task 处理完第一轮循环之后，那时 armctrl_init() 早已跑完。
    armctrl_set_event_cb(on_armctrl_cycle, NULL);

    const esp_timer_create_args_t targs = {
        .callback = &sample_timer_cb,
        .arg = NULL,
        .name = "dash_sample",
    };
    esp_err_t err = esp_timer_create(&targs, &s_sample_timer);
    if (err == ESP_OK) err = esp_timer_start_periodic(s_sample_timer, DASH_SAMPLE_PERIOD_US);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sample timer start failed: %s (类别近似关联将不可用,不影响核心功能)", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "dash registered: /dash/, /battery_log");
}
```

- [ ] **Step 4: `CMakeLists.txt` 加 `esp_timer` 依赖**

```cmake
# components/net/CMakeLists.txt
idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c" "net_dash_core.c" "net_dash.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "../../chargereborn-dashboard/dashboard/index.html"
                                      "../../chargereborn-dashboard/dashboard/app.js"
                                      "../../chargereborn-dashboard/dashboard/styles.css"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera ai armlink armcal armctrl esp_timer)
```

- [ ] **Step 5: 编译**

```
mcp__idf-bridge__build
```

Expected: 绿。

- [ ] **Step 6: 烧录（⚠️ 当场向用户确认后才能执行）**

```
mcp__idf-bridge__flash
```

- [ ] **Step 7: monitor 验证——并发新引入的路径不产生 WDT/heap 异常**

```
mcp__idf-bridge__monitor_start
```

观察至少 1 分钟的日志，然后：

```
mcp__idf-bridge__monitor_read
```

Expected: 看到 `dash registered: /dash/, /battery_log`；**没有**出现 `Guru Meditation`、`CORRUPT HEAP`、`task_wdt`、`abort()` 字样；`alive Ns heap=...` 心跳日志持续正常滚动。

- [ ] **Step 8: curl/浏览器验证 `/battery_log` 数据形状**

打开 `http://192.168.4.1/battery_log`，此时还没跑过任何抓取循环。

Expected: 返回 `{"now_us":...,"total":0,"session":0,"co2_g_per_cell":18.0,"logs":[]}`（或 `total` 是之前已有 NVS 累计值，非本次新增，`logs` 为空数组，因为还没有新的 armctrl 循环触发过回调）。JSON 格式合法（可以直接被浏览器渲染成可读结构，不是乱码/截断）。

- [ ] **Step 9: 触发一次真实抓取循环，验证事件钩子真正写入环形缓冲**

按现有 `/` 页面或 `/arm_run?on=1` 跑一轮完整抓取（**这一步涉及机械臂真实运动，需要用户在场确认安全**）。完成后刷新 `http://192.168.4.1/battery_log`。

Expected: `logs` 数组出现一条新记录，`ok:true`（或如果这轮失败则 `ok:false`），`total`/`session` 计数比 Step 8 时 +1（仅当 `ok:true` 时），时间戳字段非零。

- [ ] **Step 10: Commit**

```bash
git add components/net/net_dash.c components/net/CMakeLists.txt
git commit -m "feat(net): /battery_log端点,armctrl事件钩子接环形缓冲,esp_timer类别近似采样"
```

---

## Task 7: 前端——急停真实化 + 解除急停（含 `buildBrainUrl` 查询串 bug 修复）

这是整个功能里最关键的一步：把"看着能用、实际不通"的急停按钮接上真实固件。过程中发现并修复一个**既有** bug——`buildBrainUrl()` 在直连模式下用 `url.pathname = "/xxx?query"` 赋值会被 WHATWG URL 解析器提前吃掉查询串，随后紧跟的 `url.search = ""` 会把它清空，导致任何带查询参数的直连请求（比如 `/arm_estop?on=1`）实际发出去时**丢失查询参数**。这个 bug 至今没暴露是因为现有代码从未通过 `buildBrainUrl` 传过带 `?` 的路径。

全程用 `mock-server.js` + Playwright 验证，不需要真实硬件（"本地代理"模式下 `app.js` 走的是与真实"连接固件"完全相同的代码路径，只是 base URL 换成 `/brain`）。

**Files:**
- Modify: `chargereborn-dashboard/dashboard/app.js`
- Modify: `chargereborn-dashboard/dashboard/index.html`
- Modify: `chargereborn-dashboard/dashboard/mock-server.js`

**Interfaces:**
- Consumes: 无新增固件端点（复用已有的 `/arm_estop`，Task 5/6 未改动此端点）
- Produces: `clearEstop()` 函数、`#clearEstopBtn` DOM 元素——供人工验收，无后续任务消费

- [ ] **Step 1: 修复 `buildBrainUrl` 的查询串丢失 bug**

```js
// old_string (chargereborn-dashboard/dashboard/app.js):
function buildBrainUrl(pathname, options = {}) {
  const base = state.httpBaseUrl;
  if (base.startsWith("/")) return `${base}${pathname}`;

  const url = new URL(base);
  url.pathname = pathname;
  url.search = "";
  url.hash = "";
  if (options.streamPort) url.port = "81";
  return url.toString();
}

// new_string:
function buildBrainUrl(pathname, options = {}) {
  const base = state.httpBaseUrl;
  if (base.startsWith("/")) return `${base}${pathname}`;

  // 注意：url.pathname 的 setter 遇到 '?' 会把它当成查询串一并解析掉，
  // 所以必须先手动拆分，否则紧接着的 url.search="" 会把刚被 setter 顺带塞进去的
  // 查询串清空——之前没人发现是因为此前所有调用点都没传过带 '?' 的 pathname。
  const [path, search] = pathname.split("?");
  const url = new URL(base);
  url.pathname = path;
  url.search = search ? `?${search}` : "";
  url.hash = "";
  if (options.streamPort) url.port = "81";
  return url.toString();
}
```

- [ ] **Step 2: `sendEstop` 改为 HTTP 直连模式下真打 `/arm_estop`，新增 `clearEstop`**

```js
// old_string:
function sendEstop() {
  els.lastCommand.textContent = "ESTOP";
  els.commandState.textContent = "发送中";

  const sent = sendMessage({
    type: "control",
    command: "ESTOP",
    level: "critical",
    source: "web_dashboard",
    ts: Date.now(),
  });

  if (sent) {
    els.commandState.textContent = "已下发";
    setMessage("远程急停指令已通过 WebSocket 下发。");
  } else {
    els.commandState.textContent = "演示确认";
  }
}

// new_string:
async function sendEstop() {
  els.lastCommand.textContent = "ESTOP";
  els.commandState.textContent = "发送中";

  if (state.httpConnected) {
    try {
      const response = await fetch(buildBrainUrl("/arm_estop?on=1"), { cache: "no-store" });
      const data = await response.json();
      els.commandState.textContent = data.estopped ? "已急停" : "请求失败";
      setMessage(
        data.estopped
          ? "急停指令已通过固件 HTTP 接口下发并生效。"
          : "急停请求已发送但固件未确认锁存，请立即检查现场。",
      );
    } catch (error) {
      els.commandState.textContent = "请求失败";
      setMessage(`急停请求失败：${error.message}，请立即手动断电！`);
    }
    return;
  }

  const sent = sendMessage({
    type: "control",
    command: "ESTOP",
    level: "critical",
    source: "web_dashboard",
    ts: Date.now(),
  });

  if (sent) {
    els.commandState.textContent = "已下发";
    setMessage("远程急停指令已通过 WebSocket 下发。");
  } else {
    els.commandState.textContent = "演示确认";
  }
}

// 解除急停锁存。不做二次确认弹窗——本身不引发任何运动，真正的抓取动作
// 仍要求用户另外点击"抓取启动"（既有的独立确认点）。
async function clearEstop() {
  if (!state.httpConnected) {
    setMessage("解除急停仅在固件 HTTP 直连模式下可用，请先连接固件。");
    return;
  }
  try {
    const response = await fetch(buildBrainUrl("/arm_estop?on=0"), { cache: "no-store" });
    const data = await response.json();
    els.commandState.textContent = data.estopped ? "仍锁存" : "已解除";
    setMessage(
      data.estopped
        ? "解除请求已发送，但固件仍处于急停锁存状态。"
        : "急停锁存已解除，可重新下发抓取指令。",
    );
  } catch (error) {
    setMessage(`解除急停请求失败：${error.message}`);
  }
}
```

- [ ] **Step 3: `els` 对象加 `clearEstopBtn` 引用，末尾加事件监听**

```js
// old_string:
  estopBtn: document.querySelector("#estopBtn"),
  confirmDialog: document.querySelector("#confirmDialog"),

// new_string:
  estopBtn: document.querySelector("#estopBtn"),
  clearEstopBtn: document.querySelector("#clearEstopBtn"),
  confirmDialog: document.querySelector("#confirmDialog"),
```

```js
// old_string:
els.estopBtn.addEventListener("click", () => els.confirmDialog.showModal());
els.confirmEstopBtn.addEventListener("click", sendEstop);

// new_string:
els.estopBtn.addEventListener("click", () => els.confirmDialog.showModal());
els.confirmEstopBtn.addEventListener("click", sendEstop);
els.clearEstopBtn.addEventListener("click", clearEstop);
```

- [ ] **Step 4: `index.html` 加"解除急停"按钮，更新说明文案**

```html
<!-- old_string: -->
          <button class="estop-button" id="estopBtn" type="button">
            远程急停
          </button>
          <p class="control-copy">
            指令经 WebSocket 下发至 Brain，再由 UART 通知 Steward 执行安全停机。
          </p>

<!-- new_string: -->
          <button class="estop-button" id="estopBtn" type="button">
            远程急停
          </button>
          <button class="btn ghost" id="clearEstopBtn" type="button">
            解除急停
          </button>
          <p class="control-copy">
            固件 HTTP 直连模式下，指令经板载 HTTP 接口直达急停锁存，同步通过 UART 通知 Steward 执行安全停机。
          </p>
```

- [ ] **Step 5: `mock-server.js` 加 `/brain/arm_estop` mock（内存态开关，模拟固件锁存行为）**

```js
// old_string:
function mockStream(res) {

// new_string:
let mockEstopped = false;

function mockArmEstop(req, res) {
  const url = new URL(req.url, `http://localhost:${port}`);
  const on = url.searchParams.get("on");
  if (on === "1") mockEstopped = true;
  else if (on === "0") mockEstopped = false;
  send(res, 200, JSON.stringify({ estopped: mockEstopped }), "application/json; charset=utf-8");
}

function mockStream(res) {
```

```js
// old_string:
    if (url.pathname === "/brain/detect") return mockDetect(res);
    if (url.pathname === "/brain/arm_target") return mockArmTarget(res);
    if (url.pathname === "/brain/stream") return mockStream(res);
    send(res, 404, "Unknown mock Brain endpoint");

// new_string:
    if (url.pathname === "/brain/detect") return mockDetect(res);
    if (url.pathname === "/brain/arm_target") return mockArmTarget(res);
    if (url.pathname === "/brain/arm_estop") return mockArmEstop(req, res);
    if (url.pathname === "/brain/stream") return mockStream(res);
    send(res, 404, "Unknown mock Brain endpoint");
```

- [ ] **Step 6: 启动 mock-server，验证 `buildBrainUrl` 修复（纯 Node，不需要浏览器）**

```bash
cd chargereborn-dashboard/dashboard
node -e "
const assert = require('assert');
// 复刻 buildBrainUrl 的核心逻辑做单元验证(app.js 是浏览器脚本,不便 require)
function buildBrainUrl(base, pathname) {
  const [path, search] = pathname.split('?');
  const url = new URL(base);
  url.pathname = path;
  url.search = search ? '?' + search : '';
  return url.toString();
}
const got = buildBrainUrl('http://192.168.4.1', '/arm_estop?on=1');
assert.strictEqual(got, 'http://192.168.4.1/arm_estop?on=1', 'query string must survive');
console.log('PASS: buildBrainUrl preserves query string ->', got);
"
```

Expected: 输出 `PASS: buildBrainUrl preserves query string -> http://192.168.4.1/arm_estop?on=1`。

- [ ] **Step 7: Playwright 验证——完整点击流程，走真实 `app.js` 代码**

```bash
cd chargereborn-dashboard/dashboard
node mock-server.js &
MOCK_PID=$!
until curl -s http://localhost:8088/ >/dev/null 2>&1; do sleep 0.2; done
echo "mock server ready, pid=$MOCK_PID"
```

然后依次执行：

1. `mcp__tools__playwright_browser_navigate` → `http://localhost:8088/`
2. `mcp__tools__playwright_browser_click` → 点击"本地代理"按钮（`#proxyBtn`）
3. `mcp__tools__playwright_browser_click` → 点击"远程急停"按钮（`#estopBtn`）→ 弹出确认对话框
4. `mcp__tools__playwright_browser_click` → 点击"确认急停"按钮（`#confirmEstopBtn`）
5. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#commandState').textContent`

   Expected: `"已急停"`

6. `mcp__tools__playwright_browser_click` → 点击"解除急停"按钮（`#clearEstopBtn`）
7. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#commandState').textContent`

   Expected: `"已解除"`

8. `mcp__tools__playwright_browser_network_requests` → 确认列表中出现 `GET /brain/arm_estop?on=1` 和 `GET /brain/arm_estop?on=0` 两条真实网络请求（不是只有 UI 状态变了但请求根本没发出去——这正是当前 bug 的失败模式）。

最后清理：

```bash
kill $MOCK_PID
```

- [ ] **Step 8: Commit**

```bash
git add chargereborn-dashboard/dashboard/app.js chargereborn-dashboard/dashboard/index.html chargereborn-dashboard/dashboard/mock-server.js
git commit -m "fix(dashboard): 急停接入真实/arm_estop(HTTP直连模式),修buildBrainUrl查询串丢失bug,加解除急停按钮"
```

---

## Task 8: 前端——板上自动直连

页面从固件内嵌路径 `/dash/` 加载时自动进入 HTTP 直连模式，手机连热点开网址即用，不需要任何点击。

**Files:**
- Modify: `chargereborn-dashboard/dashboard/app.js`
- Modify: `chargereborn-dashboard/dashboard/mock-server.js`（加 `/dash/` 前缀静态文件别名，纯测试便利，不影响生产行为——生产环境的 `/dash/` 由固件 `net_dash.c` 提供，与本文件无关）

**Interfaces:**
- Consumes: 无
- Produces: `autoConnectIfEmbedded()` 函数——无后续任务消费

- [ ] **Step 1: `app.js` 加自动直连逻辑**

```js
// old_string:
els.connectBtn.addEventListener("click", connectWebSocket);

// new_string:
function autoConnectIfEmbedded() {
  if (!location.pathname.startsWith("/dash/")) return;
  els.httpBaseUrl.value = location.origin;
  connectHttpFirmware();
}

els.connectBtn.addEventListener("click", connectWebSocket);
```

```js
// old_string:
setConnectionStatus(false);
renderCarbon();
renderTrace(demoTrace);
startDemoUpdates();

// new_string:
setConnectionStatus(false);
renderCarbon();
renderTrace(demoTrace);
startDemoUpdates();
autoConnectIfEmbedded();
```

- [ ] **Step 2: `mock-server.js` 加 `/dash/` 前缀静态文件别名（本地验证用）**

```js
// old_string:
function serveStatic(req, res) {
  const url = new URL(req.url, `http://localhost:${port}`);
  const pathname = url.pathname === "/" ? "/index.html" : url.pathname;
  const file = path.normalize(path.join(root, pathname));

// new_string:
function serveStatic(req, res) {
  const url = new URL(req.url, `http://localhost:${port}`);
  let pathname = url.pathname;
  if (pathname === "/dash" || pathname.startsWith("/dash/")) {
    // 模拟固件 net_dash.c 的 /dash 前缀路由,便于本地验证"板上自动直连"逻辑,不需要真实硬件。
    pathname = pathname.slice("/dash".length) || "/";
  }
  if (pathname === "/") pathname = "/index.html";
  const file = path.normalize(path.join(root, pathname));
```

- [ ] **Step 3: Playwright 验证**

```bash
cd chargereborn-dashboard/dashboard
node mock-server.js &
MOCK_PID=$!
until curl -s http://localhost:8088/ >/dev/null 2>&1; do sleep 0.2; done
```

1. `mcp__tools__playwright_browser_navigate` → `http://localhost:8088/dash/`
2. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#httpBaseUrl').value`

   Expected: `"http://localhost:8088"`

3. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#httpConnectBtn').textContent`

   Expected: `"断开固件"`（证明 `connectHttpFirmware()` 真的跑了，不是只填了地址没连接）

4. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#statusText').textContent`

   Expected: `"在线"`

   注：此时后台仍会去轮询 `/detect`/`/arm_target`/`/battery_log`（都在裸 origin 下，非 `/brain/` 前缀），mock-server 对这些路径只有静态文件路由、没有 JSON mock，会 404——这是预期的，本任务只验证"自动直连触发&状态正确"，不验证轮询数据本身（那是 Task 7 已验证过的 `/brain/` 代理路径的职责）。

5. 对照组：`mcp__tools__playwright_browser_navigate` → `http://localhost:8088/`（无 `/dash/` 前缀）
6. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#httpConnectBtn').textContent`

   Expected: `"连接固件"`（未自动直连，证明判断条件只对 `/dash/` 路径生效，不是无差别触发）

```bash
kill $MOCK_PID
```

- [ ] **Step 4: Commit**

```bash
git add chargereborn-dashboard/dashboard/app.js chargereborn-dashboard/dashboard/mock-server.js
git commit -m "feat(dashboard): 从/dash/路径加载时自动进入HTTP直连模式"
```

---

## Task 9: 前端——溯源 + 碳减排面板真数据

新增 `pollBatteryLog()`，独立 2 秒轮询周期（比 `/detect`/`/arm_target` 的 600ms 慢，溯源数据变化频率低）。碳面板从"今日/本周"两张假卡片收敛成"本次会话"一张真卡片；柱状图改为本次会话逐颗累计阶梯图。

**Files:**
- Modify: `chargereborn-dashboard/dashboard/app.js`
- Modify: `chargereborn-dashboard/dashboard/index.html`
- Modify: `chargereborn-dashboard/dashboard/mock-server.js`

**Interfaces:**
- Consumes: `/battery_log` 端点（Task 6），响应形状 `{now_us, total, session, co2_g_per_cell, logs:[{seq_id,ok,cls,score,t_identified_us,t_picked_us,t_cut_us,t_placed_us}]}`
- Produces: 无后续任务消费

- [ ] **Step 1: `state.carbon` 初始形状去掉 `today`/`week`，改 `session`；加轮询定时器句柄**

```js
// old_string:
  carbon: {
    total: 1260,
    today: 126,
    week: 684,
    series: [72, 90, 108, 126, 90, 108, 126],
  },
  traces: new Map(),
  demoTimer: null,
};

// new_string:
  carbon: {
    total: 1260,
    session: 126,
    series: [72, 90, 108, 126, 90, 108, 126],
  },
  traces: new Map(),
  demoTimer: null,
  httpLogPollTimer: null,
  lastBatteryLogSeqId: null,
};
```

- [ ] **Step 2: `els` 对象——`todayCo2`/`weekCo2` 换成 `sessionCo2`**

```js
// old_string:
  totalCo2: document.querySelector("#totalCo2"),
  todayCo2: document.querySelector("#todayCo2"),
  weekCo2: document.querySelector("#weekCo2"),
  carbonChart: document.querySelector("#carbonChart"),

// new_string:
  totalCo2: document.querySelector("#totalCo2"),
  sessionCo2: document.querySelector("#sessionCo2"),
  carbonChart: document.querySelector("#carbonChart"),
```

- [ ] **Step 3: `index.html` 碳面板——两张卡片收敛成一张**

```html
<!-- old_string: -->
          <div class="carbon-stats">
            <div>
              <span>今日</span>
              <strong><span id="todayCo2">0</span> g</strong>
            </div>
            <div>
              <span>本周</span>
              <strong><span id="weekCo2">0</span> g</strong>
            </div>
          </div>

<!-- new_string: -->
          <div class="carbon-stats">
            <div>
              <span>本次会话</span>
              <strong><span id="sessionCo2">0</span> g</strong>
            </div>
          </div>
```

- [ ] **Step 4: `connectHttpFirmware`/`disconnectHttpFirmware` 加独立的 2 秒 `/battery_log` 轮询定时器**

```js
// old_string:
  setMessage("已切换到固件 HTTP/MJPEG 模式：正在轮询 /detect 和 /arm_target。");
  pollFirmwareOnce();
  state.httpPollTimer = window.setInterval(pollFirmwareOnce, 600);
}

function disconnectHttpFirmware() {
  state.httpConnected = false;
  window.clearInterval(state.httpPollTimer);
  state.httpPollTimer = null;
  els.mjpegStream.removeAttribute("src");

// new_string:
  setMessage("已切换到固件 HTTP/MJPEG 模式：正在轮询 /detect、/arm_target 与 /battery_log。");
  pollFirmwareOnce();
  pollBatteryLog().catch(() => {});
  state.httpPollTimer = window.setInterval(pollFirmwareOnce, 600);
  // /battery_log 变化频率远低于 /detect(视觉帧率)，独立开慢周期,不占用检测轮询带宽。
  state.httpLogPollTimer = window.setInterval(() => pollBatteryLog().catch(() => {}), 2000);
}

function disconnectHttpFirmware() {
  state.httpConnected = false;
  window.clearInterval(state.httpPollTimer);
  state.httpPollTimer = null;
  window.clearInterval(state.httpLogPollTimer);
  state.httpLogPollTimer = null;
  els.mjpegStream.removeAttribute("src");
```

- [ ] **Step 5: 新增 `pollBatteryLog`/`applyBatteryLog`，插入到 `pollArmTarget` 之后**

```js
// old_string:
async function pollArmTarget() {
  const response = await fetch(buildBrainUrl("/arm_target"), { cache: "no-store" });
  if (!response.ok) throw new Error(`arm_target ${response.status}`);
  const target = await response.json();
  if (!target.valid) {
    els.armTarget.textContent = "未锁定";
    return;
  }

  els.armTarget.textContent = `(${Number(target.cx).toFixed(0)}, ${Number(target.cy).toFixed(0)}) ${Number(target.angle_deg).toFixed(0)}°`;
}

// new_string:
async function pollArmTarget() {
  const response = await fetch(buildBrainUrl("/arm_target"), { cache: "no-store" });
  if (!response.ok) throw new Error(`arm_target ${response.status}`);
  const target = await response.json();
  if (!target.valid) {
    els.armTarget.textContent = "未锁定";
    return;
  }

  els.armTarget.textContent = `(${Number(target.cx).toFixed(0)}, ${Number(target.cy).toFixed(0)}) ${Number(target.angle_deg).toFixed(0)}°`;
}

async function pollBatteryLog() {
  const response = await fetch(buildBrainUrl("/battery_log"), { cache: "no-store" });
  if (!response.ok) throw new Error(`battery_log ${response.status}`);
  const data = await response.json();
  applyBatteryLog(data);
}

function applyBatteryLog(data) {
  const nowMs = Date.now();
  const nowUs = Number(data.now_us || 0);
  const toLocalString = (tUs) => {
    if (!tUs) return "-";
    const deltaMs = (nowUs - Number(tUs)) / 1000;
    return new Date(nowMs - deltaMs).toLocaleString("zh-CN");
  };

  state.traces.clear();
  const logs = data.logs || [];
  const successGrams = [];
  for (const entry of logs) {
    const id = `BAT-${entry.seq_id}`;
    const hasCls = entry.cls && entry.cls !== "?";
    state.traces.set(id, {
      id,
      model: hasCls ? entry.cls : "未知（近似值缺失）",
      confidence: Number(entry.score || 0),
      abnormal: entry.ok === false,
      timestamps: {
        identifiedAt: toLocalString(entry.t_identified_us),
        pickedAt: toLocalString(entry.t_picked_us),
        cutAt: toLocalString(entry.t_cut_us),
        archivedAt: toLocalString(entry.t_placed_us),
      },
    });
    if (entry.ok) successGrams.push(Number(data.co2_g_per_cell || 0));
  }

  const perCell = Number(data.co2_g_per_cell || 0);
  // 阶梯图：环形缓冲是倒序(最新在前)，按时间正序累计才是"越跑越高"的阶梯线。
  const series = [];
  let running = 0;
  for (let i = successGrams.length - 1; i >= 0; i -= 1) {
    running += successGrams[i];
    series.push(running);
  }
  state.carbon = {
    total: Number(data.total || 0) * perCell,
    session: Number(data.session || 0) * perCell,
    series: series.length ? series : [0],
  };
  renderCarbon();

  const latest = logs[0];
  if (latest && latest.seq_id !== state.lastBatteryLogSeqId) {
    state.lastBatteryLogSeqId = latest.seq_id;
    traceBattery(`BAT-${latest.seq_id}`);
  }
}
```

- [ ] **Step 6: `updateCarbon`/`startDemoUpdates` 同步去掉 `today`/`week`，改用 `session`（保持离线演示模式一致）**

```js
// old_string:
function updateCarbon(payload = {}) {
  state.carbon = {
    total: Number(payload.total ?? state.carbon.total),
    today: Number(payload.today ?? state.carbon.today),
    week: Number(payload.week ?? state.carbon.week),
    series: Array.isArray(payload.series) ? payload.series.map(Number) : state.carbon.series,
  };
  renderCarbon();
}

// new_string:
function updateCarbon(payload = {}) {
  state.carbon = {
    total: Number(payload.total ?? state.carbon.total),
    session: Number(payload.session ?? state.carbon.session),
    series: Array.isArray(payload.series) ? payload.series.map(Number) : state.carbon.series,
  };
  renderCarbon();
}
```

```js
// old_string:
    updateCarbon({
      total: state.carbon.total + 18,
      today: state.carbon.today + 18,
      week: state.carbon.week + 18,
      series: [...state.carbon.series.slice(1), 90 + Math.round(Math.random() * 72)],
    });

// new_string:
    updateCarbon({
      total: state.carbon.total + 18,
      session: state.carbon.session + 18,
      series: [...state.carbon.series.slice(1), 90 + Math.round(Math.random() * 72)],
    });
```

- [ ] **Step 7: `renderCarbon` 改用 `sessionCo2`；柱状图 X 轴标签从"D+序号"改成"#+序号"（不再是"天"的语义）**

```js
// old_string:
function renderCarbon() {
  animateNumber(els.totalCo2, state.carbon.total);
  animateNumber(els.todayCo2, state.carbon.today);
  animateNumber(els.weekCo2, state.carbon.week);
  drawCarbonChart();
}

// new_string:
function renderCarbon() {
  animateNumber(els.totalCo2, state.carbon.total);
  animateNumber(els.sessionCo2, state.carbon.session);
  drawCarbonChart();
}
```

```js
// old_string:
    ctx.fillStyle = "#607086";
    ctx.fillText(`D${index + 1}`, x + barWidth / 2, cssHeight - 12);

// new_string:
    ctx.fillStyle = "#607086";
    ctx.fillText(`#${index + 1}`, x + barWidth / 2, cssHeight - 12);
```

- [ ] **Step 8: `pollFirmwareOnce` 保持不变（不要把 `pollBatteryLog` 塞进去）——确认性检查**

打开 `chargereborn-dashboard/dashboard/app.js`，确认 `pollFirmwareOnce` 函数体仍然只有：

```js
async function pollFirmwareOnce() {
  await Promise.allSettled([pollDetect(), pollArmTarget()]);
}
```

`pollBatteryLog` **不**在这个函数里调用——它只应该出现在 Step 4 新增的独立 2 秒 `setInterval` 里。如果不小心把它加进了 `pollFirmwareOnce`，会导致溯源数据以 600ms 而不是 2s 轮询，浪费带宽（违反设计 §5.3），删掉即可。

- [ ] **Step 9: `mock-server.js` 加 `/brain/battery_log` mock**

```js
// old_string:
function proxyToBrain(req, res) {

// new_string:
function mockBatteryLog(res) {
  const now = Date.now() * 1000;
  const logs = [];
  for (let i = 0; i < 5; i += 1) {
    const seq = 40 - i;
    const tIdentified = now - i * 8_000_000 - 12_000_000;
    const ok = i !== 2;
    logs.push({
      seq_id: seq,
      ok,
      cls: i === 4 ? "?" : "18650",
      score: i === 4 ? 0 : 0.9 - i * 0.03,
      t_identified_us: tIdentified,
      t_picked_us: ok ? tIdentified + 3_000_000 : 0,
      t_cut_us: ok ? tIdentified + 7_000_000 : 0,
      t_placed_us: ok ? tIdentified + 8_000_000 : 0,
    });
  }
  send(
    res,
    200,
    JSON.stringify({ now_us: now, total: 42, session: 5, co2_g_per_cell: 18.0, logs }),
    "application/json; charset=utf-8",
  );
}

function proxyToBrain(req, res) {
```

```js
// old_string:
    if (url.pathname === "/brain/arm_estop") return mockArmEstop(req, res);
    if (url.pathname === "/brain/stream") return mockStream(res);

// new_string:
    if (url.pathname === "/brain/arm_estop") return mockArmEstop(req, res);
    if (url.pathname === "/brain/battery_log") return mockBatteryLog(res);
    if (url.pathname === "/brain/stream") return mockStream(res);
```

- [ ] **Step 10: Playwright 验证**

```bash
cd chargereborn-dashboard/dashboard
node mock-server.js &
MOCK_PID=$!
until curl -s http://localhost:8088/ >/dev/null 2>&1; do sleep 0.2; done
```

1. `mcp__tools__playwright_browser_navigate` → `http://localhost:8088/`
2. `mcp__tools__playwright_browser_click` → 点击"本地代理"（`#proxyBtn`）
3. `mcp__tools__playwright_browser_wait_for` → `{time: 2.5}`（等一个 2s 轮询周期跑完）
4. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#totalCo2').textContent`

   Expected: 非 `"0"`（42×18=756，因动画渐变可能还在过渡中，但不应停在初始值 `"1,260"` 或 `"0"`）

5. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#sessionCo2').textContent`

   Expected: 非初始值 `"126"`（5×18=90，同样考虑动画过渡）

6. `mcp__tools__playwright_browser_evaluate` → `() => document.querySelector('#traceResult').textContent`

   Expected: 包含 `"BAT-40"`（最新一条 mock 日志的 seq_id=40）

7. `mcp__tools__playwright_browser_evaluate` → `() => document.body.innerHTML.includes('todayCo2') || document.body.innerHTML.includes('weekCo2')`

   Expected: `false`（确认"今日/本周"字样已从 DOM 里彻底消失，不是残留了个隐藏元素）

```bash
kill $MOCK_PID
```

- [ ] **Step 11: Commit**

```bash
git add chargereborn-dashboard/dashboard/app.js chargereborn-dashboard/dashboard/index.html chargereborn-dashboard/dashboard/mock-server.js
git commit -m "feat(dashboard): 溯源/碳减排面板接入/battery_log真数据,今日本周收敛为本次会话"
```

---

## Task 10: 文档更新

修正话术错误（ESP-NOW→UART）、更新部署步骤为 HTTP 直连优先、给 `DASHBOARD_INTEGRATION.md` 加状态注记、同步 `PROTOCOL.md` 的 `carbon_update` 字段命名。纯文档改动，无需 build/flash。

**Files:**
- Modify: `chargereborn-dashboard/dashboard/DEFENSE_GUIDE.md`
- Modify: `chargereborn-dashboard/dashboard/PROTOCOL.md`
- Modify: `docs/ai/DASHBOARD_INTEGRATION.md`

**Interfaces:** 无（文档任务不产生代码接口）

- [ ] **Step 1: `DEFENSE_GUIDE.md`——部署步骤改为 HTTP 直连优先**

```markdown
<!-- old_string: -->
## 4. 现场部署步骤

### 方案 A：直接本地打开

1. 打开 `chargereborn-master/app/dashboard/index.html`。
2. 在顶部 `Brain HTTP` 输入框填入 `http://192.168.4.1`。
3. 点击“连接固件”。
4. 页面会显示 `http://192.168.4.1:81/stream` 的 MJPEG 视频，并轮询 `/detect` 与 `/arm_target`。

### 方案 B：本地代理 / Mock 服务（推荐）

<!-- new_string: -->
## 4. 现场部署步骤

### 方案 A：固件内嵌页（推荐，无需笔记本）

1. 手机或电脑连接机器人 SoftAP 热点。
2. 浏览器打开 `http://192.168.4.1/dash/`。
3. 页面自动进入 HTTP 直连模式（无需点击任何按钮），显示视频流并轮询 `/detect`、`/arm_target`、`/battery_log`。

### 方案 B：本地代理 / Mock 服务（无硬件开发/备用演示用）

```

- [ ] **Step 2: `DEFENSE_GUIDE.md`——修正 ESP-NOW 话术错误**

```markdown
<!-- old_string: -->
3. “右侧红色按钮是远程急停。点击后需要二次确认，确认后网页通过 WebSocket 发送 ESTOP 指令，Brain 再通过 UART 通知机械臂控制端停机。”

<!-- new_string: -->
3. “右侧红色按钮是远程急停。点击后需要二次确认，确认后网页经固件板载 HTTP 接口直达急停锁存，Brain 再通过 UART 直接向机械臂控制板发送急停指令。”

```

- [ ] **Step 3: `PROTOCOL.md`——修正同一处 ESP-NOW 错误表述（第 46 行附近）**

先用 Grep 定位当前行号（文件在 Task 9 之前的改动中行号可能已偏移）：

```bash
grep -n "ESP-NOW\|UART" chargereborn-dashboard/dashboard/PROTOCOL.md
```

找到含 "ESP-NOW" 字样的那一行，用 Edit 工具把它改成与 `DASHBOARD_INTEGRATION.md` 已指出的纠正一致的表述（"再通过 UART 直接向机械臂控制板发送急停指令"，不提 ESP-NOW）。

- [ ] **Step 4: `PROTOCOL.md`——`carbon_update` 示例字段名与实际前端契约同步（`today`/`week` → `session`）**

```markdown
<!-- old_string: -->
### 3.4 碳减排数据

```json
{
  "type": "carbon_update",
  "payload": {
    "total": 1260,
    "today": 126,
    "week": 684,
    "series": [72, 90, 108, 126, 90, 108, 126]
  },
  "ts": 1783310400000
}
```

<!-- new_string: -->
### 3.4 碳减排数据

> 2026-07-08 更新：字段名与实际 dashboard 前端契约同步为 `session`（本次上电会话累计），
> 不再是 `today`/`week`——固件侧没有按天分桶持久化，`today`/`week` 会被前端忽略。

```json
{
  "type": "carbon_update",
  "payload": {
    "total": 1260,
    "session": 126,
    "series": [72, 90, 108, 126, 90, 108, 126]
  },
  "ts": 1783310400000
}
```

```

- [ ] **Step 5: `docs/ai/DASHBOARD_INTEGRATION.md`——顶部加状态注记**

在文件第 1 行标题之后插入：

```markdown
<!-- old_string: -->
# DASHBOARD_INTEGRATION.md — chargereborn-dashboard 固件对接计划书

> 面向实现 `components/net/net_ws.c`（新文件，你来写）的队友。固件侧的埋点已经就位

<!-- new_string: -->
# DASHBOARD_INTEGRATION.md — chargereborn-dashboard 固件对接计划书

> **2026-07-08 更新**：WS 层未实现，明日演示改走 HTTP 轮询方案（见
> `docs/superpowers/specs/2026-07-08-dashboard-firmware-integration-design.md` 与同名 plans/
> 文档）——固件新增 `components/net/net_dash.c` 提供 `/dash/*` 内嵌静态页与 `/battery_log`
> 端点，前端 `app.js` 的 HTTP 直连模式直接消费。本文档的 armctrl 钩子映射关系与实现边界
> 仍是赛后补做 WS 层时的有效参考，不删除、仅在此处加注记。

> 面向实现 `components/net/net_ws.c`（新文件，你来写）的队友。固件侧的埋点已经就位
```

- [ ] **Step 6: 确认无遗留错误表述**

```bash
grep -rn "ESP-NOW" chargereborn-dashboard/ docs/ai/DASHBOARD_INTEGRATION.md
```

Expected: 无匹配（或只剩 `DASHBOARD_INTEGRATION.md` 里"已知的表述纠正"章节本身对这个历史错误的描述性引用，那是有意保留的记录，不是新的错误）。

- [ ] **Step 7: Commit**

```bash
git add chargereborn-dashboard/dashboard/DEFENSE_GUIDE.md chargereborn-dashboard/dashboard/PROTOCOL.md docs/ai/DASHBOARD_INTEGRATION.md
git commit -m "docs(dashboard): 部署步骤改HTTP直连优先,修正ESP-NOW话术错误,carbon_update字段同步session口径"
```

---

## Task 11: 整机联调验收（flash 确认 + 硬件在环全套验证）

Task 7-9 改了 `index.html`/`app.js`（内嵌进固件二进制的源文件），必须重新 build+flash 才能让这些改动体现在真实板子上。这是全功能完成后的最终验收，对照设计文档 §8 验证表逐项过一遍。

**Files:** 无新改动，纯验证任务。

**Interfaces:** 无

- [ ] **Step 1: 编译（带上 Task 7-9 更新过的 dashboard 资产）**

```
mcp__idf-bridge__build
```

Expected: 绿。

- [ ] **Step 2: 烧录（⚠️ 当场向用户确认后才能执行——这是本功能的最后一次 flash）**

```
mcp__idf-bridge__flash
```

- [ ] **Step 3: 启动 monitor，供后续步骤持续观察**

```
mcp__idf-bridge__monitor_start
```

- [ ] **Step 4: 基础功能不回归**

手机/电脑连接 SoftAP，打开 `http://192.168.4.1/dash/`：

- 视频流正常显示。
- 识别状态（`aiStatus`/`aiFps`）随检测更新。
- 抓取目标（`armTarget`）随 `/arm_target` 更新。

Expected: 页面自动进入直连模式（Task 8），三项数据都在动，与旧版 `/` 页面的识别效果一致。

- [ ] **Step 5: 急停实测（真实机械臂动作，需用户在场）**

1. 点击"抓取启动"（或 `/arm_run?on=1`）开始一轮抓取。
2. 运动过程中点击 dashboard 的"远程急停"→ 确认。
3. 观察：舵机应立即停止；`monitor` 日志应出现 `[estop]` 相关字样；网页 `commandState` 显示"已急停"。
4. 点击"解除急停"。
5. 观察：网页显示"已解除"；再次点击"抓取启动"应能正常开始新一轮（不再被拒绝）。

Expected: 5 项全部符合。这是本功能最高优先级的验收项——之前这个按钮是完全不通的。

- [ ] **Step 6: 溯源 + 碳减排真数据（含一次故意失败）**

1. 正常跑完 1 轮完整抓取（成功）。
2. 制造一次故意失败（比如在识别范围外放置无法稳定获取位姿的物体，触发 `acquire_pose` 连续失败自动停止；或如果条件允许，触发一次抓取/切割失败路径）。
3. 刷新/等待 `/battery_log` 轮询。

Expected：
- 溯源表出现新记录，`BAT-<seq_id>` 格式的 ID，时间戳是合理的本地时间（不是 1970 年或明显错误的值）。
- 若触发了失败路径，对应记录应显示为异常（`abnormal`/警告态），不能显示成正常处理成功。
- 碳减排"总量"与"本次会话"数字只随**成功**轮次增加，失败轮次不增加。
- 阶梯图随成功次数递增，图表可见。

- [ ] **Step 7: 并发稳定性——30 分钟 heap 观察**

保持 dashboard 页面开着（持续轮询 `/detect`+`/arm_target`+`/battery_log`），30 分钟内穿插跑 2-3 轮抓取循环。

```
mcp__idf-bridge__monitor_read
```

Expected:
- `alive Ns heap=...` 心跳里的 `heap_free` 数值没有持续下降趋势（允许正常波动，不能是单调递减）。
- 没有出现 `Guru Meditation`、`CORRUPT HEAP`、`task_wdt`、`abort()`。
- `ai_get_last().infer_ms`（体现在识别 FPS 上）没有因为新增的 `esp_timer` 采样或 `/battery_log` 轮询而明显变慢。

- [ ] **Step 8: 回归 mock-server（无硬件备用路径）**

```bash
cd chargereborn-dashboard/dashboard
node mock-server.js
```

浏览器打开 `http://localhost:8088`，点击"本地代理"。

Expected: 溯源/碳面板正常显示 mock 数据，无控制台报错（可用 `mcp__tools__playwright_browser_console_messages` 检查 `error` 级别为空）。用 Ctrl+C 停止 mock-server。

- [ ] **Step 9: 停止 monitor，整理最终状态**

```
mcp__idf-bridge__monitor_stop
```

- [ ] **Step 10: 最终确认——全部任务 commit 齐整**

```bash
git log --oneline -15
git status --short
```

Expected: 看到 Task 1-10 的全部提交按顺序排列；`git status --short` 干净（本功能相关文件无残留未提交改动；其余任务无关的未跟踪路径不受影响，保持原状）。

---

## 完成后（不属于本计划任务，仅供参考）

明天演示前建议：
1. 提前用真实板子跑一遍完整演示流程（含急停）至少一次，确认现场网络环境（SoftAP 信道干扰等）下轮询延迟可接受。
2. 检查手机/演示设备的浏览器兼容性（`fetch`/`URL`/`RTCPeerConnection` 在现代浏览器都是标准 API，不需要额外 polyfill，但建议用现场实际会用的设备提前试一次）。
3. `docs/ai/CRASH_SIGNATURES.md` 若在联调中撞到新坑，按项目惯例用 `/learn` 沉淀。
