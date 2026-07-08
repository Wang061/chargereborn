# 设计：ChargeReborn Dashboard 固件内嵌联调（演示优先版）

> **生成**：2026-07-08。
> **前置**：`docs/ai/DASHBOARD_INTEGRATION.md`（队友 WebSocket 对接计划书，未实现）、
> `chargereborn-dashboard/dashboard/PROTOCOL.md`（网页通信协议）、
> `components/armctrl/include/armctrl.h`（事件钩子/统计接口，已就位）。
> **硬约束**：投稿演示/录视频 **明天（2026-07-09）**；`chargereborn-dashboard/` 目前未纳入 git。
> **用户已拍板**：目标=明天演示/录视频用，风险取舍向"低风险、真数据"倾斜；
> 部署形态=固件内嵌 dashboard，板上直接打开（不依赖笔记本 + node 代理）；
> 碳系数 18 g/颗、"本周"卡片改"本次会话"、类别信息走近似关联（不改 armlink 透传）、
> 动手前先把用户当前 `armctrl.c`/`http_srv.c` 的未提交 WIP 单独提交一次，本设计的改动干净叠加其上。

---

## 0. 一句话

不做 WebSocket 层，让 `chargereborn-dashboard` 网页作为固件内嵌静态页（`/dash/`），全部改走已有的 HTTP 轮询端点 + 一个新增的 `/battery_log` 端点（把 armctrl 事件钩子的真实溯源/碳减排数据带出来），并把网页上唯一"看着能用、实际不通"的红色急停按钮接到已有的 `/arm_estop`，使其在演示当天真正可控、可核验。

## 1. 背景

`chargereborn-dashboard/dashboard/`（队友产出，未入 git）已经实现了两套并存的连接模式：

- **HTTP 直连模式**（`connectHttpFirmware`）：轮询固件已有的 `/detect`、`/arm_target`，`<img>` 接 `:81/stream` MJPEG——这条路径今天就是通的。
- **WebSocket 模式**（`connectWebSocket` + `PROTOCOL.md` 六类消息）：期望 `ws://192.168.4.1/ws`，用于 `device_status`/`battery_log`/`carbon_update`/`control(ESTOP)`/`webrtc_*`/`video_frame`。固件**没有** `/ws` 端点，`CONFIG_HTTPD_WS_SUPPORT` 未开，`docs/ai/DASHBOARD_INTEGRATION.md` 里交给队友的实现计划书未落地。

两个模式共用同一个急停按钮（`sendEstop()`），但该函数只会 `sendMessage`（走 WebSocket）。HTTP 直连模式下 socket 不存在，`sendMessage` 直接返回 `false`，网页显示"演示确认"——**按钮按下去，固件端什么也没收到**。这是当前最大的风险点：演示时如果评委顺着"远程急停"的话术追问或要求演示，会当场露馅。

碳减排面板、电池溯源面板目前也完全没有固件数据源，页面靠 `startDemoUpdates()` 的假数据定时器驱动。

固件侧的钩子已经就位且验证过语义（本次设计前已读 `armctrl.c` 核实）：

- `armctrl_set_event_cb`：每轮循环终止（抓取失败/切割失败/完整成功）都会同步回调一次 `armctrl_cycle_log_t`（4 个时间戳 + `ok`）。
- `armctrl_get_stats`：`total`（NVS 持久化）/`session`（RAM）计数器**只在完整成功路径递增**（`armctrl.c:414-415`）——即碳减排计数天然只算成功处理的电池，失败轮次不会被误计入碳减排，但**会**进入事件回调（`ok=false`），环形缓冲/溯源列表需要正确区分显示，不能把所有条目都当成功。
- `armctrl_estop`：已处理发送 `$DST:0!` + 锁存 + 停循环，是急停唯一合法入口。

## 2. 范围

**做**：
1. 固件新增 `components/net/net_dash.c`（+头文件）：内嵌 dashboard 静态资产、`/battery_log` 端点（溯源+碳减排真数据）、事件回调环形缓冲、检出类别低频采样缓存。
2. `http_srv.c` 挂接 `net_dash_register()`（一行调用），无其余改动。
3. 网页侧：急停接 `/arm_estop`（含解除锁存按钮）、板上自动直连、溯源/碳面板改真数据轮询、`mock-server.js` 补 `/brain/battery_log` mock。
4. 文档：`DEFENSE_GUIDE.md` 部署步骤与话术改为 HTTP 直连口径；`docs/ai/DASHBOARD_INTEGRATION.md` 顶部加状态注记（WS 方案未落地，本轮改走 HTTP，原文留档供未来参考）。
5. Git：先把当前 `armctrl.c`/`http_srv.c`/`ARM_PIPELINE.md` 的未提交 WIP 单独提交；`chargereborn-dashboard/` 随本轮改动一并入库。

**不做**（YAGNI，全部有明确理由）：
- `net_ws.c` / WebSocket / `CONFIG_HTTPD_WS_SUPPORT`——`docs/ai/DASHBOARD_INTEGRATION.md` 整份计划书按兵不动，作为赛后可选项保留。
- WebRTC 信令、`video_frame` JPEG Base64 回退——MJPEG `:81/stream` 已经是能用的真实视频通道，不需要额外视频链路。
- 按天分桶的碳减排持久化（"今日/本周"真实统计）——需要新 NVS 命名空间+定时任务，超出明天演示的时间预算；改用诚实的"本次会话"口径替代，见 §5.4。
- `armlink`/`target_track` 透传检出类别到 `armctrl_cycle_log_t`——`DASHBOARD_INTEGRATION.md` 已评估"量不大但需要改 `track_output_t` 加字段"，本轮用不改内部结构的近似关联方案顶替（§4.4），把改动完全限制在 `components/net` 内。
- 内嵌静态资产 gzip 压缩——~31KB 明文足够小，SoftAP 局域网带宽不是瓶颈，压缩会增加固件侧 CPU 与构建复杂度换来的收益可忽略。

## 3. 架构

```
手机/电脑浏览器 ── 连接板子 SoftAP (192.168.4.1)
   │
   ├─ GET /dash/*         ← 新增：固件内嵌 dashboard 静态页（EMBED_TXTFILES）
   ├─ GET /detect         ← 已有：AI 检测缓存
   ├─ GET /arm_target     ← 已有：跟踪器目标
   ├─ GET /battery_log    ← 新增：溯源环形缓冲 + 碳减排统计
   ├─ GET/POST /arm_calib ← 已有，不动
   ├─ GET /arm_run        ← 已有，不动
   ├─ GET /arm_estop      ← 已有：网页急停接这里
   ├─ GET /status         ← 已有：心跳/heap 监控
   └─ <img src=":81/stream"> ← 已有：MJPEG
```

单一 `httpd` 实例（80 口），不新起 server，不开新端口，不改 `sdkconfig`。

## 4. 固件侧改动详细设计

### 4.1 新组件文件

```
components/net/
├── net_dash.c          # 新增
├── include/
│   └── net_dash.h       # 新增：void net_dash_register(httpd_handle_t server);
├── http_srv.c           # 改：末尾调用 net_dash_register(server)
└── CMakeLists.txt        # 改：SRCS 加 net_dash.c；EMBED_TXTFILES 三个资产
```

`net_dash.h` 只暴露一个函数，符合"驱动/业务分层、清晰 API"的项目规范；`net_dash.c` 内部私有状态（环形缓冲、类别缓存）不外泄。

### 4.2 静态资产内嵌

`CMakeLists.txt` 用 `EMBED_TXTFILES` 内嵌 `chargereborn-dashboard/dashboard/{index.html,styles.css,app.js}`（`EMBED_TXTFILES` 相比 `EMBED_FILES` 会自动追加 NUL 终止符，适合按 C 字符串处理的文本资产）。链接后以 `_binary_index_html_start` 等符号访问。

路由用**单个通配符 handler**覆盖 `/dash` 与其下所有路径，避免额外的 301 重定向 handler：

- `httpd_config_t.uri_match_fn` 设为 `httpd_uri_match_wildcard`（已查证 ESP-IDF 5.5.4 源码 `httpd_uri.c`：该匹配函数对不含 `*`/`?` 的模板要求**长度完全相等**才算命中，即现有 `/detect`、`/status` 等 8 个精确路径 handler 语义不变，只有新增的带通配符模板才会启用模糊匹配——两者可以在同一个 server 上共存，不需要额外拆分或迁移现有路由）。
- 注册模板 `"/dash/?*"`（`?` 让紧邻的 `/` 变为可选，`*` 允许其后任意字符），单个 handler 同时匹配 `/dash`、`/dash/`、`/dash/app.js`、`/dash/styles.css`：
  - URI 精确等于 `/dash` 或 `/dash/` → 回 `index.html`（`Content-Type: text/html`）。
  - URI 以 `/dash/app.js` 结尾 → 回内嵌 JS（`application/javascript`）。
  - URI 以 `/dash/styles.css` 结尾 → 回内嵌 CSS（`text/css`）。
  - 其余（未知子路径）→ 404。

URI handler 总数：现有 8 个 + `/dash` 通配 1 个 + `/battery_log` 1 个 = 10，仍在 `config.max_uri_handlers = 16` 之内。

### 4.3 溯源环形缓冲 + 类别近似关联

**环形缓冲**（`net_dash.c` 内静态数组，不用堆）：

```c
#define DASH_LOG_RING_CAP 16   // 单次演示会话内够用；超出后覆盖最旧条目（见 §8 已知限制）

typedef struct {
    armctrl_cycle_log_t core;      // armctrl 原样透传
    char  cls_name[16];            // 近似关联的类别名，"?" 表示无可用采样
    float cls_score;               // 对应置信度，0 表示无可用采样
} dash_log_entry_t;

static dash_log_entry_t s_ring[DASH_LOG_RING_CAP];
static uint8_t  s_ring_head;       // 下一个写入位置
static uint8_t  s_ring_count;      // 有效条目数(<=CAP)
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
```

- **写入方（armctrl_task 内同步调用）**：`armctrl_set_event_cb` 注册的回调只做 `taskENTER_CRITICAL(&s_ring_lock)` → memcpy 一个条目到 `s_ring[s_ring_head]` → 更新 head/count → `taskEXIT_CRITICAL`，不做 JSON 格式化、不做耗时操作，满足头文件注释"必须快速返回、不可阻塞"的约束。**只在系统启动时注册一次**（`net_dash_register` 内调用，与 `DASHBOARD_INTEGRATION.md` 的边界要求一致）。
- **读取方（`/battery_log` httpd handler，独立任务）**：同样的临界区内把 `s_ring` 整体 memcpy 到栈上局部副本，出临界区后再格式化 JSON——避免持锁做 snprintf。
- 用 `portMUX`（自旋锁）而非互斥量：临界区极短（几个字段的 memcpy），且写入方是"不可阻塞"的强约束，自旋锁比互斥量更不容易在这个场景引入优先级反转/阻塞风险。

**类别近似关联**（不改 `armlink`/`ai` 内部，只在 `net_dash.c` 里加一个 `esp_timer` 周期回调）：

```c
static char  s_last_cls[16] = "?";
static float s_last_score = 0;
static esp_timer_handle_t s_sample_timer;   // 周期 500ms，复用现有 ai_get_last()（已是轻量缓存读，见 http_srv.c detect_get 注释）

static void sample_cb(void *arg) {
    ai_result_t r;
    ai_get_last(&r);
    if (r.count > 0 && r.boxes[0].score >= 0.40f) {   // 与 detect_get 的 DETECT_OVERLAY_MIN_SCORE 门限一致
        strncpy(s_last_cls, ai_class_name(r.boxes[0].cls), sizeof(s_last_cls)-1);
        s_last_score = r.boxes[0].score;
    }
}
```

`armctrl_set_event_cb` 回调触发时，把当前 `s_last_cls`/`s_last_score` 一并拷进环形缓冲条目。**这是"最近一次检出"的近似值，不是该轮真正抓取目标的类别**（跟踪器不透传类别，参见 `DASHBOARD_INTEGRATION.md` §2 表格的已知缺口）——命名与文档都要明确标注"近似"，不能让网页/答辩话术暗示这是精确关联。`esp_timer` 回调运行在 Timer Task 上下文，`ai_get_last()` 只读缓存不触发相机/推理，符合"快速返回"要求，不需要额外的 FreeRTOS 任务/栈。

### 4.4 `/battery_log` 端点

```
GET /battery_log →
{
  "now_us": 123456789,          // esp_timer_get_time()，供网页把 t_*_us 换算成真实钟表时间
  "total": 42,                   // armctrl_get_stats() 的 total（NVS 持久化，只含完整成功轮次）
  "session": 7,                  // 同上 session
  "co2_g_per_cell": 18.0,
  "logs": [
    {
      "seq_id": 41,
      "ok": true,
      "cls": "18650", "score": 0.94,          // 近似关联，可能是 "?"/0
      "t_identified_us": ..., "t_picked_us": ..., "t_cut_us": ..., "t_placed_us": ...
    },
    ...   // 最多 16 条，倒序（最新在前）
  ]
}
```

`ok=false` 的条目（抓取失败/切割失败）也会出现在 `logs` 里——这是真实数据的一部分，前端必须按 `ok` 字段区分展示，不能一律渲染成"处理成功"。

碳系数常量：

```c
// 反推自 dashboard demo 假数据(总量126g/一周7颗≈18g/颗)，答辩口径与队友原型保持一致；
// 如有实测/官方系数，改这一处即可，其余代码不依赖具体数值。
#define CARBON_G_PER_CELL 18.0f
```

### 4.5 `http_srv.c` 挂接点

`net_http_start()` 末尾、`ESP_LOGI(TAG, "http server up...")` 之前加一行 `net_dash_register(server);`。不改 `httpd_config_t` 之外的现有代码路径（`uri_match_fn` 赋值也在 `net_http_start` 内的 `config` 初始化处补一行）。

## 5. 网页侧改动详细设计（`chargereborn-dashboard/dashboard/`）

### 5.1 急停真实化

`app.js` 的 `sendEstop()` 分支：HTTP 直连模式下改为 `fetch('/arm_estop?on=1')`，按响应 JSON 的 `estopped` 字段真实显示"已急停"/"请求失败"（复用 `http_srv.c` 已有的失败提示口径）。

`index.html` 急停按钮旁新增一个次要按钮"解除急停"（`fetch('/arm_estop?on=0')`）——当前网页只有急停没有解除，真按了会把臂锁死到只能回旧的 `/`（根页面）操作，演示现场必须能从新页面自己解锁。

### 5.2 板上自动直连

页面加载时检测 `location.pathname` 是否以 `/dash/` 开头：是→跳过手动填地址/点按钮的步骤，自动以 `location.origin` 为 base 调用 `connectHttpFirmware()`。手机连上 SoftAP、浏览器打开 `http://192.168.4.1/dash/` 后无需任何点击即进入直连模式，降低现场操作失误风险。

### 5.3 溯源真数据

新增 `pollBatteryLog()`，与现有 `pollDetect`/`pollArmTarget` 一起进 `pollFirmwareOnce()` 的 `Promise.allSettled`（2s 周期，比 `/detect`/`/arm_target` 的 600ms 慢，因为溯源数据变化频率低得多，没必要抢带宽）。收到后：

- 用 `now_us` 与 `t_*_us` 的差值换算出"距现在多少秒前"，加到 `Date.now()` 上得到本地时间戳字符串，喂给现有 `renderTrace()`。
- `id` 字段用 `"BAT-" + seq_id`（`DASHBOARD_INTEGRATION.md` 建议的顶替方案）。
- `model`/`confidence` 用 `cls`/`score`，`cls === "?"` 时显示"未知（近似值缺失）"而非编造。
- `abnormal` 字段：暂无固件侧异常判定信号源，`ok === false` 时映射为 `abnormal: true`（"识别到但处理未完整"本质上就是需要复核的异常路径），`renderTrace` 现有的"异常复核"徽章语义可以复用，不用新增字段。

### 5.4 碳面板改真数据

- `总量`卡片：`armctrl_get_stats().total × 18` 克（真实，NVS 持久化，跨重启保留）。
- 原"今日/本周"两张卡片 → 改标签为**"本次会话"单卡**（`session × 18` 克），如实反映数据来源（无按天分桶持久化，见 §2 不做清单）——不显示编造的"今日"/"本周"数字。
- 原七日柱状图 → 改为**本次会话逐颗累计阶梯图**：横轴是环形缓冲里 `ok=true` 条目的顺序（最多 16 颗），纵轴是累计克数（单调递增阶梯线），是真实数据、且天然好看（demo 越跑越高）。超过 16 颗后图表只反映最近 16 颗的窗口（环形缓冲覆盖旧数据），这是已知限制，不影响"总量"数字的正确性。

### 5.5 `mock-server.js`

补 `/brain/battery_log` mock handler（返回结构与固件端一致的假 JSON），保证无硬件时开发/备用演示路径依然可用；`BRAIN_HTTP` 代理模式下自动透传到真实 `/battery_log`，不需要额外改动代理逻辑。

## 6. 文档改动

- **`DEFENSE_GUIDE.md`**：
  - 部署步骤"方案 A"改为主推：连 SoftAP 后浏览器直接打开 `http://192.168.4.1/dash/`（无需 node、无需改地址栏）；"方案 B 本地代理"降级为"无硬件/开发用备用方案"。
  - 第 27 行"再通过 ESP-NOW 通知机械臂控制端停机"→ 按 `DASHBOARD_INTEGRATION.md` 已指出的纠正，改成"再通过 UART 直接向机械臂控制板发送急停指令"。
- **`PROTOCOL.md`**：第 46 行同一处 ESP-NOW 表述错误一并改掉（该文件本就是队友维护，顺手修正）。
- **`index.html`**：控制面板下方"指令经 WebSocket 下发至 Brain，再由 UART 通知 Steward"的说明文案改为"指令经固件 HTTP 接口直达急停锁存，同步通过 UART 通知 Steward"，与 §5.1 的真实实现一致。
- **`docs/ai/DASHBOARD_INTEGRATION.md`**：顶部加一段状态注记——"2026-07-08 更新：WS 层未实现，明日演示改走 HTTP 轮询方案（见 `2026-07-08-dashboard-firmware-integration-design.md`）；本文档的映射关系与边界仍是赛后补做 WS 层时的有效参考，不删除"。

## 7. Git 处理

当前工作区有未提交的 WIP（`components/armctrl/armctrl.c` 抓取补偿相关、`components/net/http_srv.c` 页面文案精简、`docs/ai/ARM_PIPELINE.md` 语义更新）。执行本设计前：

1. 先对这三个文件的当前 WIP 单独 `git add` + commit（沿用最近提交风格的 conventional commit）。
2. 再新建/修改 `net_dash.c`/`net_dash.h`/`CMakeLists.txt`/`http_srv.c`（追加式改动）/`chargereborn-dashboard/`，作为独立的后续 commit(s)。

这样两批改动各自可回滚、可审查，不混在一个大 diff 里。

## 8. 验证计划

| 步骤 | 做法 | 通过标准 |
|---|---|---|
| 1. 编译 | `mcp__idf-bridge__build` | 绿，无新增警告 |
| 2. 烧录+启动 | `flash`（当场确认）→ monitor | 正常启动日志，`http server up`，无 boot loop |
| 3. 静态资产 | 浏览器开 `http://192.168.4.1/dash/`、`/dash`（无斜杠）、`/dash/app.js` | 页面/资产正确加载，Content-Type 正确 |
| 4. 现有端点不回归 | 网页原有识别框叠加、抓取目标十字 | 与改动前行为一致（`/detect`、`/arm_target` 未改） |
| 5. 急停实测 | 跑 `/arm_run?on=1` 一轮中，网页点急停 | 舵机停 + `[estop]` 日志 + 网页显示"已急停"；点"解除"后可再次 `arm_run` |
| 6. 溯源+碳 | 完整跑 1-2 轮抓取（含至少一次故意失败，如切割位置外的空抓） | `/battery_log` 出现对应 `ok=true`/`ok=false` 条目，时间戳可换算；网页溯源表/碳面板同步更新，失败条目不算进碳数字 |
| 7. 并发稳定性 | 网页保持轮询（`/detect`+`/arm_target`+`/battery_log`）跑一次完整循环 | `/status` 的 `heap_free` 30 分钟内无持续下降；`ai_get_last().infer_ms` 未因新增 `esp_timer` 采样明显变慢 |
| 8. 回归 mock | 无板子环境下 `node mock-server.js` | 溯源/碳面板显示 mock 数据，无报错 |

## 9. 已知限制（明确写下，不当成 bug）

- 溯源列表/阶梯图窗口 = 最近 16 轮（环形缓冲容量），超出后旧条目被覆盖；`total` 累计数字不受影响。
- 检出类别是"最近 500ms 内采样"的近似值，非该轮真正抓取目标的精确类别；类别缺失时显示"未知"而非编造。
- 碳减排"本次会话"口径 = 本次上电以来，不是"今日"/"本周"（无按天持久化）。
- WebRTC/`/ws`/`video_frame` 均未实现，`PROTOCOL.md` 对应章节保持"预留"状态。
