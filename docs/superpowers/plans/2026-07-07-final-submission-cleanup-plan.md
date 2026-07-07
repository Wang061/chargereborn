# 正式提交收尾 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删光 bring-up 脚手架（G0-G4 分级、$KMS 死协议、两个备用模型组件），把识别层升级为恒位置卡尔曼跟踪器（治运动帧污染/置信度闪烁/类别翻转/底部误检四种病），状态机收敛为"单轮含切割+连续开关+急停"的正式演示形态，预埋 dashboard 钩子并交队友对接计划书，最后收缩分区表定稿提交。

**Architecture:** 三层不变（kinematics 纯 IK / armctrl 状态机 / armlink 传输+目标产出），新增 `armlink` 内的纯 C 目标跟踪器 `target_track`（无 ESP 依赖，host gcc 可测，同 `kinematics`/`armlink_frame` 惯例）。跟踪器消费 `ai_result_t`（新增取帧时间戳）逐帧候选框，输出平稳目标供 `armctrl::acquire_pose` 等待 STABLE 后取用。删除件与新增件严格按 `docs/superpowers/specs/2026-07-06-final-submission-cleanup-design.md` 的 §3/§4/§5 执行。

**Tech Stack:** ESP-IDF 5.5.4 / ESP32-S3；C（跟踪器/armctrl/armlink 纯 C，host 单测用 mingw gcc 5.3）；C++（`components/ai` 的 esp-dl 封装，不受本轮改动影响的部分不动）；NVS（新增独立 `"stats"` 命名空间，`armcal` 命名空间/`armcal_t` 结构**不动**）。

## Global Constraints

- `IDF_VERSION = 5.5.4`、`IDF_TARGET = esp32s3`，禁止升级/改 target（版本锁见 `.claude/rules/version-lock.md`）。
- 构建经 `mcp__idf-bridge__build`；**flash 一律当场确认**，不自动 flash。
- `armcal_t` 结构体字段与 `ARMCAL_MAGIC` **绝对不许改动**——单应性 H 已标定入 NVS，结构改了 MAGIC 失配会丢标定，需重标（3 天截止期内不可承受）。
- host 单测编译前缀固定（Anaconda 默认 PATH 下 gcc 对 float 代码会 ICE，见 `.superpowers/sdd/task-2-report.md`）：
  ```
  env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc ...
  ```
- 急停指令字符串固定为 **`$DST:0!`**（唯一真机验证过的串；`$DST!` 已订正废弃，见 spec 2026-07-07 订正）。
- 每个任务完成后必须 `mcp__idf-bridge__build` 绿，才能进入下一任务；host 测试任务额外要求 `ALL PASS`。
- 只改与本计划直接相关的文件；新增/改动匹配现有命名（`TAG`/`ESP_LOGx`/无 magic number）与分层规则。
- 提交信息用中文，遵循仓库现有 commit 风格（`类型(范围): 摘要`）。

---

## Task 1: 删除 battery_yolo / battery_detect，ai 组件收敛为 battery_detect4 单一检测器

**Files:**
- Delete: `components/battery_yolo/`（整目录）
- Delete: `components/battery_detect/`（整目录）
- Modify: `components/ai/Kconfig`（删除，见下）
- Modify: `components/ai/CMakeLists.txt`
- Modify: `components/ai/idf_component.yml`
- Modify: `components/ai/ai.cpp`

**Interfaces:**
- Consumes: 无（本任务不依赖后续任务）
- Produces: `ai_init()`/`ai_detect_jpeg()`/`ai_detect_oneshot()`/`ai_get_last()`/`ai_class_name()` 签名不变（供 Task 5 在 `ai_detect_jpeg` 上加参数、main.c 的 `detect_task` 消费），`components/ai` 只依赖 `battery_detect4`。

- [ ] **Step 1: 删除两个备用检测器组件目录**

```bash
rm -rf components/battery_yolo components/battery_detect
```

- [ ] **Step 2: 删除 `components/ai/Kconfig`（不再需要三选一菜单）**

```bash
rm components/ai/Kconfig
```

说明：删除后本地生成的 `sdkconfig`（未入 git）里可能残留 `CONFIG_AI_DETECTOR_*` 等旧行，属无害孤儿配置项，IDF 重新配置时会忽略，不影响构建，无需手动清理。

- [ ] **Step 3: `components/ai/CMakeLists.txt` 收敛依赖**

将：
```cmake
idf_component_register(
    SRCS "ai.cpp" "battery_angle.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera esp-dl battery_detect battery_yolo battery_detect4
)
```
改为：
```cmake
idf_component_register(
    SRCS "ai.cpp" "battery_angle.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera esp-dl battery_detect4
)
```

- [ ] **Step 4: `components/ai/idf_component.yml` 更新注释**

将：
```yaml
dependencies:
  # esp-dl(43.5MB 第三方库)走 registry + dependencies.lock 复现, 不 vendor 进仓。
  # (2026-06-18 板上 A/B 实测: registry 3.3.5 与本地 override 3.3.5 推理逐框分数完全一致 → 自包含安全)
  espressif/esp-dl:
    version: "==3.3.5"
  # battery_detect(自训 18650 模型)已 vendored 为本地组件 components/battery_detect/，IDF 自动发现，不在此声明
```
改为：
```yaml
dependencies:
  # esp-dl(43.5MB 第三方库)走 registry + dependencies.lock 复现, 不 vendor 进仓。
  # (2026-06-18 板上 A/B 实测: registry 3.3.5 与本地 override 3.3.5 推理逐框分数完全一致 → 自包含安全)
  espressif/esp-dl:
    version: "==3.3.5"
  # battery_detect4(自训 4 类电池模型)已 vendored 为本地组件 components/battery_detect4/，IDF 自动发现，不在此声明
```

- [ ] **Step 5: `components/ai/ai.cpp` 去掉三选一分支——头文件 include**

将：
```cpp
#include "ai.h"
#include "battery_angle.h"
#if CONFIG_AI_DETECTOR_BATTERY_YOLO
#include "battery_yolo_detect.hpp"
#elif CONFIG_AI_DETECTOR_BATTERY_DETECT4
#include "espdet4_detect.hpp"
#else
#include "espdet_detect.hpp"
#endif
#include "dl_image_jpeg.hpp"
```
改为：
```cpp
#include "ai.h"
#include "battery_angle.h"
#include "espdet4_detect.hpp"
#include "dl_image_jpeg.hpp"
```

- [ ] **Step 6: `ai_init()` 去分支**

将：
```cpp
extern "C" esp_err_t ai_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
#if CONFIG_AI_DETECTOR_BATTERY_YOLO
    if (!s_det)  s_det  = new BatteryYoloDetect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl YOLOv8n 4-class, lazy)");
#elif CONFIG_AI_DETECTOR_BATTERY_DETECT4
    if (!s_det)  s_det  = new ESPDet4Detect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet-Pico 4-class, lazy)");
#else
    if (!s_det)  s_det  = new ESPDetDetect();   // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet, lazy)");
#endif
    return (s_det && s_lock) ? ESP_OK : ESP_ERR_NO_MEM;
}
```
改为：
```cpp
extern "C" esp_err_t ai_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_det)  s_det  = new ESPDet4Detect();  // lazy_load=true：首次 run 才载模型
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet-Pico 4-class, lazy)");
    return (s_det && s_lock) ? ESP_OK : ESP_ERR_NO_MEM;
}
```

- [ ] **Step 7: `ai_class_name()` 去分支**

将：
```cpp
extern "C" const char *ai_class_name(int cls) {
#if CONFIG_AI_DETECTOR_BATTERY_YOLO || CONFIG_AI_DETECTOR_BATTERY_DETECT4
    // 4 类电池模型, 类别 id 顺序 = 训练集 data.yaml (0:21700 1:18650 2:9V 3:AA)
    // battery_detect4 的 battery4.yaml 沿用同一顺序(NAMES 建集时对齐)
    switch (cls) {
    case 0: return "21700";
    case 1: return "18650";
    case 2: return "9V";
    case 3: return "AA";
    default: return "obj";
    }
#else
    return (cls == 0) ? "18650" : "obj";   // 单类 18650 模型(M5)
#endif
}
```
改为：
```cpp
extern "C" const char *ai_class_name(int cls) {
    // 4 类电池模型, 类别 id 顺序 = 训练集 data.yaml (0:21700 1:18650 2:9V 3:AA)
    switch (cls) {
    case 0: return "21700";
    case 1: return "18650";
    case 2: return "9V";
    case 3: return "AA";
    default: return "obj";
    }
}
```

- [ ] **Step 8: 全仓残留引用自查**

```bash
grep -rn "battery_yolo\|BatteryYoloDetect\|ESPDetDetect\b\|AI_DETECTOR_" --include="*.c" --include="*.cpp" --include="*.h" --include="*.hpp" --include="CMakeLists.txt" --include="Kconfig" components/ main/
```
Expected: 零匹配（`ESPDetDetect` 与旧 `battery_detect` 组件一起已删除；`ESPDet4Detect` 不含 `ESPDetDetect\b` 精确匹配）。

- [ ] **Step 9: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`，无 `battery_yolo`/`battery_detect`/`AI_DETECTOR` 相关报错。

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "refactor(ai): 删除battery_yolo/battery_detect备用组件,收敛为battery_detect4单一检测器

battery_yolo(YOLOv8n 5.8s/帧已否决)/battery_detect(旧单类,被detect4取代)
从未同时编入过运行二进制,删除只影响仓库/配置/菜单复杂度。ai.cpp/Kconfig/
CMakeLists三选一分支一并去掉。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: 删除 armlink 死协议代码 + net /arm_test /arm_auto 端点 + root 页终态重写

**Files:**
- Modify: `components/armlink/armlink.c`
- Modify: `components/armlink/include/armlink.h`
- Modify: `components/armlink/Kconfig`
- Modify: `components/net/http_srv.c`

**Interfaces:**
- Consumes: 无
- Produces: `armlink.h` 公开面收窄为 `armlink_init/armlink_update_from_ai/armlink_get_last_target`（Task 6 会再加 `armlink_track_suspend/resume/set_exclusions`）；`arm_target_t` 结构本任务不变（Task 6 加字段）。root 页最终 HTML/JS 形态在本任务一次性定稿（后续任务只加后端 handler，不再改 HTML）。

- [ ] **Step 1: `components/armlink/armlink.c` 删除 `$KMS` 编码函数**

将：
```c
int armlink_encode_kms(const arm_target_t *t, char *out, size_t n)
{
    if (!t || !out || n == 0) return -1;
    // 占位：本步无 px->mm 标定，用像素中心填 x,y（单位错误，仅脚手架）。标定后改真实 mm。
    int x = (int)t->center_x_px;
    int y = (int)t->center_y_px;
    int z = 0;
    return snprintf(out, n, "$KMS:%d,%d,%d,%d!", x, y, z, ARMLINK_MOVE_TIME_MS);
}

int armlink_encode_wrist_servo(const arm_target_t *t, char *out, size_t n)
{
    if (!t || !out || n == 0) return -1;
    // 优先用已标定 wrist_deg；未标定(NAN)则用像素角占位（pwm=1500+deg*K，K 待实测）。
    float deg = isnan(t->wrist_deg) ? t->angle_deg : t->wrist_deg;
    int pwm = (int)(ARMLINK_WRIST_PWM_MID + deg * ARMLINK_WRIST_PWM_PER_DEG);
    if (pwm < ARMLINK_WRIST_PWM_MIN) pwm = ARMLINK_WRIST_PWM_MIN;
    if (pwm > ARMLINK_WRIST_PWM_MAX) pwm = ARMLINK_WRIST_PWM_MAX;
    return snprintf(out, n, "{#004P%04dT%04d!}", pwm, ARMLINK_MOVE_TIME_MS);
}

void armlink_set_auto_send(bool on)
{
    s_auto_send = on;
    ESP_LOGW(TAG, "auto_send -> %s", on ? "ON(会自动驱动机械臂!)" : "OFF");
}

bool armlink_get_auto_send(void)
{
    return s_auto_send;
}

#if CONFIG_ARMLINK_UART_ENABLE
// 发一帧裸腕舵机指令 {#idxPppppTtttt!}；armlink_send_test_frame 内部小工具。
static esp_err_t armlink_send_wrist_pwm(int pwm_us)
{
    char cmd[32];
    int len = snprintf(cmd, sizeof(cmd), "{#%03dP%04dT%04d!}",
                        ARMLINK_TEST_SERVO_IDX, pwm_us, ARMLINK_TEST_STEP_MS);
    if (len <= 0) return ESP_FAIL;
    int w = armlink_uart_send(cmd, len);
    ESP_LOGI(TAG, "test frame sent (%d B): %s", w, cmd);
    return (w == len) ? ESP_OK : ESP_FAIL;
}
#endif

esp_err_t armlink_send_test_frame(void)
{
#if CONFIG_ARMLINK_UART_ENABLE
    // 真机固件的 $KMS: 自解算未实现（COM4 直连实测：sscanf 不匹配，两端都无回应）；
    // 改发裸协议已验证可动的序列：腕舵机(#004) 中位 -> 小幅摆 -> 回中位。
    // 阻塞说明：本函数由 /arm_test HTTP handler 同步调用，含 ~1.4s vTaskDelay，
    // 会占用该 httpd worker 线程 ~2s；仅供手动点按联调，不适合高频/自动路径。
    esp_err_t e;

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ARMLINK_TEST_SETTLE_MS));

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID - ARMLINK_TEST_PWM_SWING);
    if (e != ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(ARMLINK_TEST_SETTLE_MS));

    e = armlink_send_wrist_pwm(ARMLINK_TEST_PWM_MID);
    if (e != ESP_OK) return e;

    return ESP_OK;
#else
    ESP_LOGW(TAG, "test frame 请求被忽略: CONFIG_ARMLINK_UART_ENABLE 未开");
    return ESP_ERR_INVALID_STATE;
#endif
}
```
改为：（整段删除，文件到 `armlink_get_last_target` 后直接结束——`armlink_init`/`armlink_update_from_ai`/`armlink_get_last_target` 三个函数保留不动）

- [ ] **Step 2: 删除 armlink.c 顶部现已无用的常量定义**

将：
```c
// 腕角 -> PWM 占位映射（参考旧 OpenMV pwm=1500+deg*K；K 与零位需实测标定）
#define ARMLINK_WRIST_PWM_MID     1500
#define ARMLINK_WRIST_PWM_PER_DEG 5.6f
#define ARMLINK_WRIST_PWM_MIN     500
#define ARMLINK_WRIST_PWM_MAX     2500
#define ARMLINK_MOVE_TIME_MS      800     // 指令运动时间占位(ms)

// 测试帧改走裸协议：真机固件的 $KMS: 自解算未实现（sscanf 不匹配，COM4 直连实测确认，
// 见 docs/ai/CRASH_SIGNATURES.md）；裸腕舵机(#004) 帧经同一实测验证可动。
#define ARMLINK_TEST_SERVO_IDX    4      // 腕舵机(#004)，单路低扭矩，测试最安全
#define ARMLINK_TEST_PWM_MID      1500   // 中位 (us)
#define ARMLINK_TEST_PWM_SWING    150    // 摆幅 (us, ~20°)，远在 [500,2500] 限位内
#define ARMLINK_TEST_STEP_MS      600    // 单步运动时间 (ms)
#define ARMLINK_TEST_SETTLE_MS    700    // 步间等待 (ms)，>= STEP_MS 保证到位，且避开 KM1 uart_get_ok 未清时的丢帧窗口

static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;
static volatile bool     s_auto_send = false;   // 运行时自动发送开关（默认关，防首次上电乱驱动臂）
```
改为：
```c
static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;
```

- [ ] **Step 3: `components/armlink/include/armlink.h` 删除对应声明**

将：
```c
/* —— 运行时安全开关（与编译期 CONFIG_ARMLINK_UART_ENABLE 解耦）——
 * UART 硬件可启用，但"每帧自动发坐标驱动臂"默认关闭；联调确认链路无误后再开。
 * 这样首次上电不会因检测到电池就连续驱动机械臂。 */
// 设置/读取自动发送开关（默认 false=不自动驱动臂）。线程安全。供 /arm_auto。
void armlink_set_auto_send(bool on);
bool armlink_get_auto_send(void);

// 手动发一段安全测试序列：裸腕舵机(#004) 中位 -> 小幅摆(~20°) -> 回中位（真机 COM4 直连验证过的裸协议帧）。
// $KMS:x,y,z,t! 自解算在真机固件上未实现（sscanf 不匹配，COM4 直连实测确认），故测试帧改走裸协议。
// 供 /arm_test 联调用：受控、点一次发一次（含 ~1.4s 延时，调用期间同步阻塞）。
// UART 未启用时返回 ESP_ERR_INVALID_STATE。返回 ESP_OK=已发送。不做并发保护，仅供单次手动点按场景。
esp_err_t armlink_send_test_frame(void);

/* —— 协议编码（纯字符串，不发送；供 UART sink 或上位机复用）—— */
// $KMS:x,y,z,t! —— Steward(KM1) 自做 IK 的坐标指令。
// 注意：本步无 px->mm 标定，机械域是占位，编码值不可直接驱动真臂。
int armlink_encode_kms(const arm_target_t *t, char *out, size_t n);
// {#004P<pwm>T<ms>!} —— 裸腕舵机角指令；pwm 由角度经占位公式（K 待标定）。
int armlink_encode_wrist_servo(const arm_target_t *t, char *out, size_t n);

#ifdef __cplusplus
}
#endif

/*
 * 启用真实 UART 驱动机械臂的步骤（本步默认全关，安全）：
 *  1. 物理装置就绪 + 填 docs/ai/SAFETY.md（舵机限位/电流/上电序/急停）。
 *  2. menuconfig: armlink → 开 ARMLINK_UART_ENABLE，填 ARMLINK_UART_TX_GPIO
 *     （避开 35/36/37 PSRAM、0/3/45/46 strapping、19/20 USB-JTAG）。
 *  3. 不接动力电/抬空，先用逻辑分析仪或 Steward 回显核对 UART 串正确。
 *  4. 标定 px->mm 与 px角->腕角(K/零位)，填 armlink.c 占位公式，低速接电验证。
 */
```
改为：
```c
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: `components/armlink/Kconfig` 删除已死的协议选择**

将：
```
    choice ARMLINK_PROTO
        prompt "发送协议"
        depends on ARMLINK_UART_ENABLE
        default ARMLINK_PROTO_KMS
        help
            KMS = $KMS:x,y,z,t! (Steward 自做 IK)；WRIST_SERVO = {#004P..T..!} (裸腕角)。
        config ARMLINK_PROTO_KMS
            bool "$KMS: 坐标(Steward IK)"
        config ARMLINK_PROTO_WRIST_SERVO
            bool "裸腕舵机角"
    endchoice

endmenu
```
改为：
```
endmenu
```

- [ ] **Step 5: `sdkconfig.defaults` 删除对应行**

Read `sdkconfig.defaults`，将：
```
CONFIG_ARMLINK_PROTO_KMS=y
```
整行删除（该 Kconfig symbol 已在 Step 4 删除，且代码里从未有 `#if CONFIG_ARMLINK_PROTO` 分支引用过——纯冗余配置项）。

- [ ] **Step 6: `components/net/http_srv.c` 删除 `arm_test_get`/`arm_auto_get` 函数**

将：
```c
// 机械臂手动测试帧：点一下发一条固定安全 $KMS: 帧，供首次 UART 联调（受控、点一次发一次）。
static esp_err_t arm_test_get(httpd_req_t *req)
{
    esp_err_t e = armlink_send_test_frame();
    char buf[96];
    int n = snprintf(buf, sizeof(buf),
        "{\"sent\":%s,\"err\":\"%s\"}",
        (e == ESP_OK) ? "true" : "false", esp_err_to_name(e));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 自动发送开关：/arm_auto?on=1 开（检测到电池每帧自动发坐标驱动臂）；on=0 关。默认关。
static esp_err_t arm_auto_get(httpd_req_t *req)
{
    int on = -1;   // -1=仅查询不改
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 64) {
        char q[64], val[8];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
            on = (val[0] == '1') ? 1 : 0;
        }
    }
    if (on >= 0) armlink_set_auto_send(on != 0);
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"auto_send\":%s}",
                     armlink_get_auto_send() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 标定: GET 查询当前; POST body="H0,H1,...,H8" 写入并置 valid。
```
改为：
```c
// 标定: GET 查询当前; POST body="H0,H1,...,H8" 写入并置 valid。
```

- [ ] **Step 7: 删除对应 URI 注册**

将：
```c
    httpd_uri_t arm = { .uri = "/arm_target", .method = HTTP_GET, .handler = arm_target_get };
    httpd_register_uri_handler(server, &arm);
    httpd_uri_t arm_test = { .uri = "/arm_test", .method = HTTP_GET, .handler = arm_test_get };
    httpd_register_uri_handler(server, &arm_test);
    httpd_uri_t arm_auto = { .uri = "/arm_auto", .method = HTTP_GET, .handler = arm_auto_get };
    httpd_register_uri_handler(server, &arm_auto);
    httpd_uri_t calib_g = { .uri = "/arm_calib", .method = HTTP_GET,  .handler = arm_calib_get };
```
改为：
```c
    httpd_uri_t arm = { .uri = "/arm_target", .method = HTTP_GET, .handler = arm_target_get };
    httpd_register_uri_handler(server, &arm);
    httpd_uri_t calib_g = { .uri = "/arm_calib", .method = HTTP_GET,  .handler = arm_calib_get };
```

- [ ] **Step 8: root 页 HTML/JS 终态重写（一次到位；后续任务只加后端，不再改本页）**

将 `root_get()` 整个函数体（`const char *html = ...` 到 `return httpd_resp_sendstr(req, html);`）替换为：

```c
static esp_err_t root_get(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ChargeReborn 采集</title></head>"
        "<body style='font-family:sans-serif;text-align:center'>"
        "<h3>ChargeReborn 数据采集</h3>"
        "<div>类名 <input id=n value='18650' size=8> "
        "间隔 <input id=iv value='1.5' size=3>秒 "
        "<button onclick='shot()'>抓拍</button> "
        "<button id=run onclick='toggle()'>连拍开始</button> "
        "已存 <span id=c>0</span> 张</div>"
        "<div><button id=det onclick='dtog()'>识别开始</button> <span id=ds>-</span></div>"
        "<div style='margin:6px'>"
        "<label><input type=checkbox id=cont> 连续模式</label> "
        "<button onclick='arun(1)'>抓取启动</button> "
        "<button onclick='arun(0)'>停止</button> "
        "<button onclick='aestop()' style='background:#c00;color:#fff'>急停</button> "
        "<span id=gs>-</span></div>"
        "<p style='position:relative;display:inline-block;line-height:0'>"
        "<img id=v style='max-width:96vw;display:block'>"
        "<canvas id=ov style='position:absolute;left:0;top:0;pointer-events:none'></canvas>"
        "</p>"
        "<script>"
        "var t=null,cnt=0;"
        "function nm(){return encodeURIComponent(document.getElementById('n').value||'cap');}"
        "function shot(){"
        "fetch('/capture?name='+nm()).then(function(r){return r.blob();}).then(function(b){"
        "var a=document.createElement('a');a.href=URL.createObjectURL(b);"
        "a.download=(document.getElementById('n').value||'cap')+'_'+Date.now()+'.jpg';"
        "a.click();URL.revokeObjectURL(a.href);"
        "document.getElementById('c').textContent=++cnt;});}"
        "function toggle(){var b=document.getElementById('run');"
        "if(t){clearInterval(t);t=null;b.textContent='连拍开始';return;}"
        "var ms=Math.max(300,parseFloat(document.getElementById('iv').value||'1.5')*1000);"
        "t=setInterval(shot,ms);b.textContent='连拍停止';}"
        "var dt=null;"
        "function draw(d){var im=document.getElementById('v'),cv=document.getElementById('ov');"
        "cv.width=im.clientWidth;cv.height=im.clientHeight;"
        "var g=cv.getContext('2d');g.clearRect(0,0,cv.width,cv.height);"
        "document.getElementById('ds').textContent='n='+d.n+' '+d.infer_ms+'ms';"
        "if(!d.w||!d.n){return;}var sx=cv.width/d.w,sy=cv.height/d.h;"
        "g.lineWidth=2;g.strokeStyle='#0f0';g.fillStyle='#0f0';g.font='14px sans-serif';"
        "for(var i=0;i<d.boxes.length;i++){var b=d.boxes[i];"
        "var x=b.x1*sx,y=b.y1*sy;"
        "g.strokeRect(x,y,(b.x2-b.x1)*sx,(b.y2-b.y1)*sy);"
        "g.fillText(b.name+' '+b.s.toFixed(2),x+2,y>14?y-3:y+12);"
        "if(b.a>=0){var cxp=(b.x1+b.x2)/2*sx,cyp=(b.y1+b.y2)/2*sy,"
        "th=b.a*Math.PI/180,L=Math.max((b.x2-b.x1)*sx,(b.y2-b.y1)*sy),"
        "dx=Math.cos(th)*L/2,dy=Math.sin(th)*L/2;"
        "g.strokeStyle='#f00';g.beginPath();g.moveTo(cxp-dx,cyp-dy);g.lineTo(cxp+dx,cyp+dy);g.stroke();"
        "g.strokeStyle='#0f0';g.fillText(b.a.toFixed(0),cxp+4,cyp-4);}}}"
        "function poll(){fetch('/detect').then(function(r){return r.json();}).then(draw).catch(function(){});}"
        "function dtog(){var b=document.getElementById('det');"
        "if(dt){clearInterval(dt);dt=null;b.textContent='识别开始';return;}"
        "dt=setInterval(poll,180);b.textContent='识别停止';}"
        "function arun(on){var c=document.getElementById('cont').checked?1:0;"
        "fetch('/arm_run?on='+on+'&cont='+c).then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('gs').textContent=d.running?(d.cont?'连续运行中':'运行中'):'已停止';});}"
        "function aestop(){fetch('/arm_estop?on=1').then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('gs').textContent='已急停';});}"
        "document.getElementById('v').src='http://'+location.hostname+':81/stream';"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}
```

注：`/arm_estop` 与 `/arm_run` 的 `cont` 参数此刻后端还不存在（分别在 Task 8、Task 9 才实现），网页上对应按钮此时点击会 404——这是预期的中间状态，不影响本任务的 C 编译；纯前端字符串改动不产生编译错误。

- [ ] **Step 9: 全仓残留引用自查**

```bash
grep -rn "armlink_send_test_frame\|armlink_encode_kms\|armlink_encode_wrist_servo\|armlink_set_auto_send\|armlink_get_auto_send\|arm_test_get\|arm_auto_get\|ARMLINK_PROTO" --include="*.c" --include="*.h" --include="Kconfig" components/ main/
```
Expected: 零匹配。

- [ ] **Step 10: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "refactor(armlink,net): 删除\$KMS死协议/auto_send/测试帧;root页终态重写

\$KMS:在真机KM1固件上从未实现过(CRASH_SIGNATURES 2026-07-02);
armlink_send_test_frame/auto_send系Phase1联调脚手架,驱动权已交
armctrl状态机。root页HTML/JS一次性改到终态(连续模式勾选+急停按钮),
对应后端端点在后续任务(8/9)接上。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: 删除 armctrl G0-G4 分级 + net /arm_grade 端点

**Files:**
- Modify: `components/armctrl/armctrl.c`
- Modify: `components/armctrl/include/armctrl.h`
- Modify: `components/net/http_srv.c`

**Interfaces:**
- Consumes: 无
- Produces: `armctrl_init()` 保留；`armctrl_move_arm/armctrl_move_servo` 签名不变（Task 8 会在函数体内加 estop 早退）；`armctrl_task` 状态机收敛为无 G 分支的单一流程（Task 9 会在末尾加连续判断，Task 7 会重写其中的 `acquire_pose` 调用点周边逻辑）。

- [ ] **Step 1: `components/armctrl/armctrl.c` 删除 `s_grade` 变量与 get/set 函数**

将：
```c
static armcal_t s_cal;
static volatile int  s_grade = 0;       // 0..4, 默认最保守
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_set_grade(int g) { if (g < 0) g = 0; if (g > 4) g = 4; s_grade = g; ESP_LOGW(TAG, "grade=%d", g); }
int  armctrl_get_grade(void) { return s_grade; }
void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
```
改为：
```c
static armcal_t s_cal;
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
```

- [ ] **Step 2: `armctrl_move_arm` 去掉 G0 干跑/G1 慢速分支，只保留"未标定不发字节"兜底**

将：
```c
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    int pwm[4];
    if (kin_move_best(s_cal.link_mm, x, y, z, pwm) != 0) {
        ESP_LOGE(TAG, "不可达 (%.0f,%.0f,%.0f) — 安全停", x, y, z);
        return ESP_ERR_INVALID_ARG;
    }
    // G1 慢速: 时长 x1.5
    int mt = (s_grade <= 1) ? (move_ms * 3 / 2) : move_ms;
    char frame[96];
    int len = armlink_encode_arm_frame(pwm, mt, frame, sizeof(frame));
    if (len <= 0) return ESP_FAIL;
    // 干跑条件: G0 或 未标定 —— 未标定时即使 grade 被中途拉高也绝不发字节(联锁第4道)
    if (s_grade == 0 || !s_cal.valid) {
        ESP_LOGW(TAG, "[G0-dry] 不发送(%s): %s", s_grade == 0 ? "grade=0 干跑" : "未标定", frame);
    } else {
        armlink_uart_send(frame, len);
        ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    }
    vTaskDelay(pdMS_TO_TICKS(mt + SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "UART 未启用, 忽略 move_arm");
    return ESP_ERR_INVALID_STATE;
#endif
}
```
改为：
```c
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    int pwm[4];
    if (kin_move_best(s_cal.link_mm, x, y, z, pwm) != 0) {
        ESP_LOGE(TAG, "不可达 (%.0f,%.0f,%.0f) — 安全停", x, y, z);
        return ESP_ERR_INVALID_ARG;
    }
    char frame[96];
    int len = armlink_encode_arm_frame(pwm, move_ms, frame, sizeof(frame));
    if (len <= 0) return ESP_FAIL;
    // 未标定时绝不发字节(安全联锁最后一道): "mm"是假坐标,不许驱动真舵机。
    if (!s_cal.valid) {
        ESP_LOGW(TAG, "[dry] 未标定,不发送: %s", frame);
    } else {
        armlink_uart_send(frame, len);
        ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "UART 未启用, 忽略 move_arm");
    return ESP_ERR_INVALID_STATE;
#endif
}
```

- [ ] **Step 3: `armctrl_move_servo` 同理去 G0 分支**

将：
```c
void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    char frame[32];
    int len = armlink_encode_servo_frame(idx, pwm, move_ms, frame, sizeof(frame));
    if (len > 0) {
        if (s_grade == 0 || !s_cal.valid) {
            ESP_LOGW(TAG, "[G0-dry] 不发送(%s): %s", s_grade == 0 ? "grade=0 干跑" : "未标定", frame);
        } else {
            armlink_uart_send(frame, len);
            ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}
```
改为：
```c
void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    char frame[32];
    int len = armlink_encode_servo_frame(idx, pwm, move_ms, frame, sizeof(frame));
    if (len > 0) {
        if (!s_cal.valid) {
            ESP_LOGW(TAG, "[dry] 未标定,不发送: %s", frame);
        } else {
            armlink_uart_send(frame, len);
            ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}
```

- [ ] **Step 4: `armctrl_task` 去掉 G 分支——顶部守卫 + 尾部 G3/G4 分叉**

将整个 `armctrl_task` 函数体替换为：
```c
static void armctrl_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_run || !s_ik_ok || !s_cal.valid) {
            if (s_cal_dirty) {
                s_cal_dirty = false;
                armcal_load(&s_cal);
                ESP_LOGI(TAG, "标定已重载 valid=%d", s_cal.valid);
            }
            if (s_run && !s_ik_ok) { ESP_LOGW(TAG, "run 被拒: IK 自检未过"); s_run = false; }
            else if (s_run && !s_cal.valid) { ESP_LOGW(TAG, "run 被拒: 未标定"); s_run = false; }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        s_run = false;   // 单轮; 连续模式在 Task 9 加(改这里的判断)
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
```

- [ ] **Step 5: `armctrl_init` 日志去掉 grade 字样**

将：
```c
    ESP_LOGI(TAG, "init ok (grade=0,run=off,ik=%d,cal=%d)", s_ik_ok, s_cal.valid);
```
改为：
```c
    ESP_LOGI(TAG, "init ok (run=off,ik=%d,cal=%d)", s_ik_ok, s_cal.valid);
```

- [ ] **Step 6: `components/armctrl/include/armctrl.h` 删除 grade 声明**

将：
```c
esp_err_t armctrl_init(void);
void armctrl_set_grade(int g);       // 0..4
int  armctrl_get_grade(void);
void armctrl_request_run(bool on);
```
改为：
```c
esp_err_t armctrl_init(void);
void armctrl_request_run(bool on);
```

- [ ] **Step 7: `components/net/http_srv.c` 删除 `arm_grade_get` 函数**

将：
```c
// 安全级: /arm_grade?g=0..4 设置; 无参查询。
static esp_err_t arm_grade_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "g", val, sizeof(val)) == ESP_OK) {
            armctrl_set_grade(val[0] - '0');
        }
    }
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"grade\":%d}", armctrl_get_grade());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

// 启停一轮抓取: /arm_run?on=1|0。
```
改为：
```c
// 启停一轮抓取: /arm_run?on=1|0。
```

- [ ] **Step 8: 删除 `/arm_grade` URI 注册**

将：
```c
    httpd_uri_t grade = { .uri = "/arm_grade", .method = HTTP_GET, .handler = arm_grade_get };
    httpd_register_uri_handler(server, &grade);
    httpd_uri_t run = { .uri = "/arm_run", .method = HTTP_GET, .handler = arm_run_get };
```
改为：
```c
    httpd_uri_t run = { .uri = "/arm_run", .method = HTTP_GET, .handler = arm_run_get };
```

- [ ] **Step 9: 全仓残留引用自查**

```bash
grep -rn "s_grade\|armctrl_set_grade\|armctrl_get_grade\|arm_grade_get\|G0-dry\|\"grade\"" --include="*.c" --include="*.h" components/ main/
```
Expected: 零匹配。

- [ ] **Step 10: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "refactor(armctrl,net): 删除G0-G4安全分级,状态机收敛为单一完整流程

分级式bring-up已完成历史使命(G1-G4硬件验证均已实测通过,见
docs/ai/ARM_PIPELINE.md);终态固件始终走完整抓取+切割流程,
未标定不发字节的联锁原样保留。/arm_grade端点随之删除。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 4: 目标跟踪器 `target_track`（恒位置卡尔曼 + 关联 + 时间门限 + 稳定判据，host TDD）

这是本轮的技术核心：把逐帧带噪声/丢检/误检/运动帧污染的检测框，滤成一个平稳的抓取目标估计。纯 C、无 ESP 依赖，host gcc 可测（同 `kinematics`/`armlink_frame` 惯例）。算法细节见 spec §4；本任务把它变成可编译可测的代码。

**Files:**
- Create: `components/armlink/include/target_track.h`
- Create: `components/armlink/target_track.c`
- Create: `components/armlink/test/test_track.c`
- Modify: `components/armlink/CMakeLists.txt`

**Interfaces:**
- Consumes: 无（纯 C，不依赖 `ai.h`/ESP 类型；调用方在 Task 6 里把 `ai_box_t` 转换成 `track_box_t`）
- Produces: `track_state_t`（可静态分配的完整结构体，同 `armcal_t` 惯例）、`track_init/track_set_exclusions/track_suspend/track_resume/track_update/track_get`（Task 6 的 `armlink.c` 直接调用这些函数）。

- [ ] **Step 1: 写完整头文件 `components/armlink/include/target_track.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 纯 C 目标跟踪器：把逐帧带噪声/丢检/误检的检测框，滤成一个平稳的抓取目标估计。
// 无 ESP 依赖，可 host gcc 单测(同 kinematics/armlink_frame 的约定)。
// 设计：每个数值通道(cx,cy,w,h,cos2θ,sin2θ)各一个标量恒位置(随机游走)卡尔曼滤波,
// 见 docs/superpowers/specs/2026-07-06-final-submission-cleanup-design.md §4。

// —— 输入：单帧一个候选框(与 ai_box_t 解耦，调用方负责从 ai_box_t 转换)——
typedef struct {
    float cx, cy;       // 中心像素坐标
    float w, h;         // 框宽高(像素)
    float angle_deg;    // 长轴角 [0,180)；<0 = 无效(该框不参与角度关联/更新)
    float score;        // 置信度 0..1
    float anisotropy;   // 角度置信 0..1
} track_box_t;

// —— 输出：跟踪器当前估计 ——
typedef struct {
    bool     confirmed;    // 已过 min_hits，可用作抓取目标
    bool     coasting;     // 本帧未关联到检测，靠 predict 滑行(陈旧值，不可用于开始新动作)
    bool     stable;       // 已过稳定判据(含滞回)，armctrl 可据此结束 ACQUIRE
    float    cx, cy;       // 滤波后中心(像素)
    float    w, h;         // 滤波后宽高(像素)
    float    angle_deg;    // 滤波后长轴角 [0,180)
    float    score;        // 最近一次真实关联(非滑行)帧的分数，滑行期间保持不变
    uint32_t hits;         // 累计确认命中数(供调试/日志)
} track_output_t;

#define TRACK_MAX_EXCLUSIONS 8

// —— 内部状态：字段全暴露供调用方静态分配(同 armcal_t 惯例)，调用方不应直接改字段，用下方 API ——
typedef struct {
    bool  initialized;         // 是否已收到过首帧(false 时全部估计无意义)
    bool  confirmed;
    bool  coasting;
    bool  stable;              // 稳定判据锁存值(含滞回)
    bool  suspended;           // track_suspend() 期间为 true，track_update 直接忽略

    uint32_t hits;             // 连续命中计数(达 TRACK_MIN_HITS 转 confirmed)

    // 每通道标量 KF：估计值 x, 方差 P
    float x_cx, p_cx;
    float x_cy, p_cy;
    float x_w,  p_w;
    float x_h,  p_h;
    float x_c2, p_c2;          // cos(2*angle) 通道
    float x_s2, p_s2;          // sin(2*angle) 通道

    int64_t last_ts_us;        // 上次成功 predict/update 的时间戳(算 dt)
    int64_t last_hit_ts_us;    // 上次真实关联命中的时间戳(算 max_age)
    int64_t gate_ts_us;        // track_resume() 设置；早于此的检测整帧丢弃

    float last_nu_cx, last_nu_cy;   // 最近一次关联的新息(供稳定判据)
    float last_score;

    // 挪动重锁缓冲：连续门外候选(最多看最近 3 个，判断是否彼此聚集)
    float reject_cx[3], reject_cy[3];
    int   reject_count;

    // 稳定判据滑窗(环形，长度 5)
    bool  win_good[5];
    float win_cx[5], win_cy[5], win_ang[5];
    int   win_idx;
    int   win_filled;
    int   stable_enter_streak;
    int   stable_exit_streak;

    // 连续模式防重抓：初始化排除区(px 域)；跨 track_resume 持续，只由 track_set_exclusions 管理
    float excl_px[TRACK_MAX_EXCLUSIONS][2];
    int   excl_count;
} track_state_t;

// 清零到 LOST/未初始化状态，含排除区一并清空(用于开机/全新会话)。
void track_init(track_state_t *tr);

// 设置本轮排除区(连续模式防重抓)。pts_px[i]={px,py}；n=0 清空。n>TRACK_MAX_EXCLUSIONS 截断到上限。
void track_set_exclusions(track_state_t *tr, const float pts_px[][2], int n);

// 挂起：armctrl 抓取序列第一个运动原语起调用。挂起期间 track_update 立即返回 false，不做任何计算。
void track_suspend(track_state_t *tr);

// 恢复=硬重置：清估计、清 confirmed/hits/滑窗/稳定锁存，记录 gate_ts_us=now_us
// (早于此的检测按运动帧丢弃)。排除区(excl_px/excl_count)不受影响——那是跨轮状态。
void track_resume(track_state_t *tr, int64_t now_us);

// 喂一帧检测(count 可为 0，boxes 对应可为 NULL)。capture_ts_us 早于 gate_ts_us 或 tr->suspended
// 时整帧丢弃，返回 false。否则跑一次 predict+关联+update(或滑行)+稳定判据推进，返回 true。
bool track_update(track_state_t *tr, const track_box_t *boxes, int count, int64_t capture_ts_us);

// 读取当前估计到 out。tr 从未 initialized 时 out 全零、confirmed/coasting/stable 皆 false。
void track_get(const track_state_t *tr, track_output_t *out);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写第一个测试 `components/armlink/test/test_track.c`（只含运动帧拒收场景）**

这是 2026-07-06 晚失败场景的根因复现：臂运动中拍到的旧帧必须被时间门限拒收。

```c
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
```

- [ ] **Step 3: 编译，确认因缺少实现而失败**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c -Icomponents/armlink/include -lm -o /tmp/tt
```
Expected: 链接错误（`undefined reference to 'track_init'` 等），因为 `target_track.c` 还不存在。

- [ ] **Step 4: 写完整实现 `components/armlink/target_track.c`**

```c
#include "target_track.h"
#include <math.h>

// —— 滤波参数(像素单位；上板实测后按运行手册流程调整这里的数值，不改算法结构)——
#define TRACK_R_POS       4.0f      // cx,cy 测量噪声方差(px²), σ≈2px 起点
#define TRACK_R_WH        9.0f      // w,h 测量噪声方差(px²), σ≈3px 起点
#define TRACK_R_ANG       0.011f    // cos2θ/sin2θ 测量噪声方差(单位向量域)
#define TRACK_LAMBDA_POS  0.06f     // 位置通道 λ=q/R(主旋钮): 稳态K≈0.22
#define TRACK_LAMBDA_WH   0.03f     // 尺寸通道 λ 减半(更钝)
#define TRACK_LAMBDA_ANG  0.06f     // 角度通道同位置

#define TRACK_MIN_SCORE_NEW     0.40f   // 新建轨迹门限
#define TRACK_MIN_SCORE_UPDATE  0.25f   // 已确认轨迹更新门限(ByteTrack 双阈值)
#define TRACK_MIN_ANISOTROPY    0.10f   // 角度置信门限

#define TRACK_GATE_NU_K        3.0f     // 新息门限系数: |nu| <= K*sqrt(P+R)
#define TRACK_GATE_MIN_PX      20.0f    // 空间门限最小值(px)

#define TRACK_MIN_HITS          3       // 连续命中数达此值才 confirmed
#define TRACK_MAX_AGE_US   1200000LL    // 1.2s，滑行超时判 LOST(微秒)

#define TRACK_RECAPTURE_COUNT       3       // 连续N帧门外聚集候选才判"目标被挪动"
#define TRACK_RECAPTURE_CLUSTER_PX  20.0f   // 聚集判定：候选两两中心距离阈值

#define TRACK_EXCLUSION_RADIUS_PX   60.0f   // 初始化排除半径(连续模式防重抓)

#define TRACK_STABLE_NU_PX      3.0f    // 稳定判据: 单帧新息阈值
#define TRACK_STABLE_SIGMA_PX   2.0f    // 稳定判据: sqrt(P) 阈值
#define TRACK_STABLE_EXTENT_PX  4.0f    // 稳定判据: 滑窗中心极差阈值
#define TRACK_STABLE_EXTENT_DEG 5.0f    // 稳定判据: 滑窗角度极差阈值
#define TRACK_STABLE_MAX_MISS   1       // 滑窗内(5帧)允许的最大miss数
#define TRACK_STABLE_ENTER_N    5       // 连续满足N帧才进入STABLE
#define TRACK_STABLE_EXIT_N     2       // 连续不满足N帧才退出STABLE

#define TRACK_DT_MAX_S   2.0f     // dt钳位上限(防止长间隔后 predict 让 P 异常增大)

// —— 单通道标量KF ——
static void kf1_predict(float *p, float q_dot, float dt_s) { *p += q_dot * dt_s; }   // 恒位置模型: x 不变

static float kf1_update(float *x, float *p, float z, float r) {
    float s  = *p + r;
    float nu = z - *x;
    float k  = *p / s;
    *x += k * nu;
    *p *= (1.0f - k);
    return nu;
}

static void kf1_init(float *x, float *p, float z, float r) { *x = z; *p = 10.0f * r; }

// —— 角度双通道编解码(mod-180°回绕安全) ——
static void angle_to_vec(float angle_deg, float *c2, float *s2) {
    float two_theta_rad = angle_deg * (float)M_PI / 90.0f;   // *2 倍角, deg->rad
    *c2 = cosf(two_theta_rad);
    *s2 = sinf(two_theta_rad);
}
static float vec_to_angle(float c2, float s2) {
    float two_theta_deg = atan2f(s2, c2) * 180.0f / (float)M_PI;   // (-180,180]
    float theta_deg = two_theta_deg / 2.0f;                         // (-90,90]
    if (theta_deg < 0.0f) theta_deg += 180.0f;                      // 折到 [0,180)
    return theta_deg;
}
static float angle_diff_mod180(float a, float b) {
    float d = a - b;
    while (d > 90.0f) d -= 180.0f;
    while (d < -90.0f) d += 180.0f;
    return d;
}

// —— 内部：清空KF估计与生命周期状态，不碰 gate_ts_us / excl_* ——
static void reset_estimate(track_state_t *tr) {
    tr->initialized = false;
    tr->confirmed = false;
    tr->coasting = false;
    tr->stable = false;
    tr->hits = 0;
    tr->x_cx = tr->p_cx = 0.0f; tr->x_cy = tr->p_cy = 0.0f;
    tr->x_w  = tr->p_w  = 0.0f; tr->x_h  = tr->p_h  = 0.0f;
    tr->x_c2 = tr->p_c2 = 0.0f; tr->x_s2 = tr->p_s2 = 0.0f;
    tr->last_ts_us = 0;
    tr->last_hit_ts_us = 0;
    tr->last_nu_cx = tr->last_nu_cy = 0.0f;
    tr->last_score = 0.0f;
    tr->reject_count = 0;
    tr->win_idx = 0; tr->win_filled = 0;
    for (int i = 0; i < 5; i++) { tr->win_good[i] = false; tr->win_cx[i] = tr->win_cy[i] = tr->win_ang[i] = 0.0f; }
    tr->stable_enter_streak = 0;
    tr->stable_exit_streak = 0;
}

void track_init(track_state_t *tr) {
    if (!tr) return;
    reset_estimate(tr);
    tr->suspended = false;
    tr->gate_ts_us = 0;
    tr->excl_count = 0;
    for (int i = 0; i < TRACK_MAX_EXCLUSIONS; i++) { tr->excl_px[i][0] = 0.0f; tr->excl_px[i][1] = 0.0f; }
}

void track_set_exclusions(track_state_t *tr, const float pts_px[][2], int n) {
    if (!tr) return;
    if (n < 0) n = 0;
    if (n > TRACK_MAX_EXCLUSIONS) n = TRACK_MAX_EXCLUSIONS;
    tr->excl_count = n;
    for (int i = 0; i < n; i++) { tr->excl_px[i][0] = pts_px[i][0]; tr->excl_px[i][1] = pts_px[i][1]; }
}

void track_suspend(track_state_t *tr) { if (tr) tr->suspended = true; }

void track_resume(track_state_t *tr, int64_t now_us) {
    if (!tr) return;
    reset_estimate(tr);
    tr->suspended = false;
    tr->gate_ts_us = now_us;
}

static void advance_stability_window(track_state_t *tr, bool hit) {
    bool instant_ok = tr->confirmed && !tr->coasting && hit
                       && fabsf(tr->last_nu_cx) <= TRACK_STABLE_NU_PX
                       && fabsf(tr->last_nu_cy) <= TRACK_STABLE_NU_PX
                       && sqrtf(tr->p_cx) <= TRACK_STABLE_SIGMA_PX
                       && sqrtf(tr->p_cy) <= TRACK_STABLE_SIGMA_PX;

    tr->win_good[tr->win_idx] = instant_ok;
    tr->win_cx[tr->win_idx] = tr->x_cx;
    tr->win_cy[tr->win_idx] = tr->x_cy;
    tr->win_ang[tr->win_idx] = vec_to_angle(tr->x_c2, tr->x_s2);
    tr->win_idx = (tr->win_idx + 1) % 5;
    if (tr->win_filled < 5) tr->win_filled++;

    bool overall_ok = false;
    if (tr->win_filled == 5) {
        int miss = 0;
        float minx = tr->win_cx[0], maxx = tr->win_cx[0];
        float miny = tr->win_cy[0], maxy = tr->win_cy[0];
        for (int i = 0; i < 5; i++) {
            if (!tr->win_good[i]) miss++;
            if (tr->win_cx[i] < minx) minx = tr->win_cx[i];
            if (tr->win_cx[i] > maxx) maxx = tr->win_cx[i];
            if (tr->win_cy[i] < miny) miny = tr->win_cy[i];
            if (tr->win_cy[i] > maxy) maxy = tr->win_cy[i];
        }
        float ang_extent = 0.0f;
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++) {
                float d = fabsf(angle_diff_mod180(tr->win_ang[i], tr->win_ang[j]));
                if (d > ang_extent) ang_extent = d;
            }
        overall_ok = (miss <= TRACK_STABLE_MAX_MISS)
                     && (maxx - minx) <= TRACK_STABLE_EXTENT_PX
                     && (maxy - miny) <= TRACK_STABLE_EXTENT_PX
                     && ang_extent <= TRACK_STABLE_EXTENT_DEG;
    }

    if (overall_ok) { tr->stable_enter_streak++; tr->stable_exit_streak = 0; }
    else            { tr->stable_exit_streak++;  tr->stable_enter_streak = 0; }

    if (!tr->stable && tr->stable_enter_streak >= TRACK_STABLE_ENTER_N) tr->stable = true;
    if (tr->stable  && tr->stable_exit_streak  >= TRACK_STABLE_EXIT_N)  tr->stable = false;
}

bool track_update(track_state_t *tr, const track_box_t *boxes, int count, int64_t capture_ts_us)
{
    if (!tr) return false;
    if (tr->suspended) return false;
    if (capture_ts_us < tr->gate_ts_us) return false;   // 运动帧/挂起前遗留帧: 整帧丢弃

    float dt = 0.0f;
    if (tr->initialized) {
        dt = (float)(capture_ts_us - tr->last_ts_us) / 1e6f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > TRACK_DT_MAX_S) dt = TRACK_DT_MAX_S;
        kf1_predict(&tr->p_cx, TRACK_LAMBDA_POS * TRACK_R_POS, dt);
        kf1_predict(&tr->p_cy, TRACK_LAMBDA_POS * TRACK_R_POS, dt);
        kf1_predict(&tr->p_w,  TRACK_LAMBDA_WH  * TRACK_R_WH,  dt);
        kf1_predict(&tr->p_h,  TRACK_LAMBDA_WH  * TRACK_R_WH,  dt);
        kf1_predict(&tr->p_c2, TRACK_LAMBDA_ANG * TRACK_R_ANG, dt);
        kf1_predict(&tr->p_s2, TRACK_LAMBDA_ANG * TRACK_R_ANG, dt);
    }
    tr->last_ts_us = capture_ts_us;

    // —— 关联：在门限内找最佳候选 ——
    int best = -1;
    float best_score = -1.0f;
    float min_score = tr->confirmed ? TRACK_MIN_SCORE_UPDATE : TRACK_MIN_SCORE_NEW;

    for (int i = 0; i < count; i++) {
        const track_box_t *b = &boxes[i];
        if (b->score < min_score) continue;
        if (!tr->initialized) {
            // 新建阶段: 排除区生效(连续模式防重抓)，无空间门限(还没有估计可比)
            bool excluded = false;
            for (int e = 0; e < tr->excl_count; e++) {
                float dx = b->cx - tr->excl_px[e][0], dy = b->cy - tr->excl_px[e][1];
                if (dx * dx + dy * dy <= TRACK_EXCLUSION_RADIUS_PX * TRACK_EXCLUSION_RADIUS_PX) { excluded = true; break; }
            }
            if (excluded) continue;
        } else {
            // 已有估计: 空间门限 + 新息门限(排除区不适用——滤波更新不受排除影响)
            float dx = b->cx - tr->x_cx, dy = b->cy - tr->x_cy;
            float gate_r = fmaxf(0.5f * sqrtf(fmaxf(tr->x_w * tr->x_h, 0.0f)), TRACK_GATE_MIN_PX);
            if (dx * dx + dy * dy > gate_r * gate_r) continue;
            float thr_cx = TRACK_GATE_NU_K * sqrtf(tr->p_cx + TRACK_R_POS);
            float thr_cy = TRACK_GATE_NU_K * sqrtf(tr->p_cy + TRACK_R_POS);
            if (fabsf(dx) > thr_cx || fabsf(dy) > thr_cy) continue;
        }
        if (b->score > best_score) { best_score = b->score; best = i; }
    }

    bool hit = (best >= 0);

    if (hit) {
        const track_box_t *b = &boxes[best];
        if (!tr->initialized) {
            kf1_init(&tr->x_cx, &tr->p_cx, b->cx, TRACK_R_POS);
            kf1_init(&tr->x_cy, &tr->p_cy, b->cy, TRACK_R_POS);
            kf1_init(&tr->x_w,  &tr->p_w,  b->w,  TRACK_R_WH);
            kf1_init(&tr->x_h,  &tr->p_h,  b->h,  TRACK_R_WH);
            tr->last_nu_cx = 0.0f; tr->last_nu_cy = 0.0f;
            if (b->angle_deg >= 0.0f && b->anisotropy >= TRACK_MIN_ANISOTROPY) {
                float c2, s2; angle_to_vec(b->angle_deg, &c2, &s2);
                kf1_init(&tr->x_c2, &tr->p_c2, c2, TRACK_R_ANG);
                kf1_init(&tr->x_s2, &tr->p_s2, s2, TRACK_R_ANG);
            } else {
                kf1_init(&tr->x_c2, &tr->p_c2, 1.0f, TRACK_R_ANG);   // 默认0度,角度置信不足
                kf1_init(&tr->x_s2, &tr->p_s2, 0.0f, TRACK_R_ANG);
            }
            tr->initialized = true;
            tr->hits = 1;
        } else {
            tr->last_nu_cx = kf1_update(&tr->x_cx, &tr->p_cx, b->cx, TRACK_R_POS);
            tr->last_nu_cy = kf1_update(&tr->x_cy, &tr->p_cy, b->cy, TRACK_R_POS);
            kf1_update(&tr->x_w, &tr->p_w, b->w, TRACK_R_WH);
            kf1_update(&tr->x_h, &tr->p_h, b->h, TRACK_R_WH);
            if (b->angle_deg >= 0.0f && b->anisotropy >= TRACK_MIN_ANISOTROPY) {
                float c2, s2; angle_to_vec(b->angle_deg, &c2, &s2);
                kf1_update(&tr->x_c2, &tr->p_c2, c2, TRACK_R_ANG);
                kf1_update(&tr->x_s2, &tr->p_s2, s2, TRACK_R_ANG);
            }
            tr->hits++;
        }
        tr->last_score = b->score;
        tr->last_hit_ts_us = capture_ts_us;
        tr->coasting = false;
        tr->reject_count = 0;
        if (!tr->confirmed && tr->hits >= TRACK_MIN_HITS) tr->confirmed = true;
    } else if (tr->initialized) {
        tr->coasting = true;
        bool timed_out = (capture_ts_us - tr->last_hit_ts_us) > TRACK_MAX_AGE_US;
        if (timed_out) {
            reset_estimate(tr);   // LOST: 下一帧起按全新目标处理(简化设计: 不预置新位置,省去二次关联)
        } else if (count > 0) {
            // 挪动重锁判定: 把本帧最高分但被门限拒绝的候选记入 reject 缓冲
            int rj = -1; float rj_score = -1.0f;
            for (int i = 0; i < count; i++) {
                if (boxes[i].score >= min_score && boxes[i].score > rj_score) { rj_score = boxes[i].score; rj = i; }
            }
            if (rj >= 0) {
                if (tr->reject_count < 3) {
                    tr->reject_cx[tr->reject_count] = boxes[rj].cx;
                    tr->reject_cy[tr->reject_count] = boxes[rj].cy;
                    tr->reject_count++;
                }
                if (tr->reject_count >= TRACK_RECAPTURE_COUNT) {
                    bool clustered = true;
                    for (int i = 0; i < tr->reject_count && clustered; i++)
                        for (int j = i + 1; j < tr->reject_count; j++) {
                            float dx = tr->reject_cx[i] - tr->reject_cx[j];
                            float dy = tr->reject_cy[i] - tr->reject_cy[j];
                            if (dx * dx + dy * dy > TRACK_RECAPTURE_CLUSTER_PX * TRACK_RECAPTURE_CLUSTER_PX) { clustered = false; break; }
                        }
                    if (clustered) reset_estimate(tr);   // 目标被挪动: 硬重置,下一帧起按新目标重新确认
                }
            }
        }
    }
    // tr->initialized==false 且无命中: 什么都不做,继续等待首帧候选

    advance_stability_window(tr, hit);
    return true;
}

void track_get(const track_state_t *tr, track_output_t *out) {
    if (!out) return;
    if (!tr || !tr->initialized) {
        out->confirmed = false; out->coasting = false; out->stable = false;
        out->cx = out->cy = out->w = out->h = out->angle_deg = out->score = 0.0f;
        out->hits = 0;
        return;
    }
    out->confirmed = tr->confirmed;
    out->coasting  = tr->coasting;
    out->stable    = tr->confirmed && tr->stable;
    out->cx = tr->x_cx; out->cy = tr->x_cy;
    out->w  = tr->x_w;  out->h  = tr->x_h;
    out->angle_deg = vec_to_angle(tr->x_c2, tr->x_s2);
    out->score = tr->last_score;
    out->hits  = tr->hits;
}
```

- [ ] **Step 5: 编译并运行，确认测试1通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 6: Commit（核心实现 + 首个回归测试）**

```bash
git add components/armlink/include/target_track.h components/armlink/target_track.c components/armlink/test/test_track.c
git commit -m "feat(armlink): 目标跟踪器target_track核心实现+运动帧拒收回归测试

恒位置标量卡尔曼(cx,cy,w,h,cos2θ/sin2θ六通道)+双阈值关联(0.40新建/
0.25更新)+空间/新息门限+时间门限(取帧时间戳<gate_ts整帧丢弃,根治
2026-07-06晚acquire_pose吃臂运动帧的bug)+min_hits确认+滑行/丢锁/
挪动重锁+滞回稳定判据。纯C无ESP依赖,host gcc可测。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 7: 追加测试2（类别翻转框跳）**

在 `test_track.c` 的 `test_motion_frame_rejected` 函数后追加：

```c
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
```

并把 `main()` 改为：
```c
int main(void) {
    test_motion_frame_rejected();
    test_class_flip_no_jump();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 8: 编译运行，确认测试1+2通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 9: 追加测试3（置信度闪烁）**

在 `test_class_flip_no_jump` 后追加：

```c
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
```

并把 `main()` 里追加一行 `test_score_flicker_no_break();`（放在 `test_class_flip_no_jump();` 之后）。

- [ ] **Step 10: 编译运行，确认测试1-3通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 11: 追加测试4（底部持续误检不得劫持轨迹）**

在 `test_score_flicker_no_break` 后追加：

```c
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
```

并把 `main()` 里追加一行 `test_bottom_false_positive_rejected();`。

- [ ] **Step 12: 编译运行，确认测试1-4通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 13: Commit（测试2-4）**

```bash
git add components/armlink/test/test_track.c
git commit -m "test(armlink): target_track加类别翻转/置信度闪烁/底部误检回归测试

三个场景均取自2026-07-06晚真实日志框坐标(AA/9V翻转框、误检
[8,417,277,471]@0.27)。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 14: 追加测试5（稳态收敛）**

在 `test_bottom_false_positive_rejected` 后追加（含一个简单确定性伪随机数生成器，避免依赖平台 `rand()` 的差异）：

```c
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
```

并把 `main()` 里追加一行 `test_steady_state_convergence();`。

- [ ] **Step 15: 编译运行，确认测试1-5通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 16: 追加测试6（滑行→丢锁→重捕获；目标挪动→重锁）**

在 `test_steady_state_convergence` 后追加：

```c
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
```

并把 `main()` 里追加一行 `test_coast_lost_recapture();`。

- [ ] **Step 17: 编译运行，确认测试1-6通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 18: 追加测试7（角度回绕）**

在 `test_coast_lost_recapture` 后追加：

```c
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

int main(void) {
    test_motion_frame_rejected();
    test_class_flip_no_jump();
    test_score_flicker_no_break();
    test_bottom_false_positive_rejected();
    test_steady_state_convergence();
    test_coast_lost_recapture();
    test_angle_wraparound();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

注：这里的 `main()` 是最终完整版，替换掉之前 Step 2/7/9/11/14/16 里逐步追加的临时版本。

- [ ] **Step 19: 编译运行，确认全部7个测试通过**

```bash
env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt
```
Expected: `ALL PASS`

- [ ] **Step 20: 把 `target_track.c` 接入 armlink 组件构建**

`components/armlink/CMakeLists.txt` 将：
```cmake
idf_component_register(
    SRCS "armlink.c" "armlink_uart.c" "armlink_frame.c"
    INCLUDE_DIRS "include"
    REQUIRES ai
    PRIV_REQUIRES driver
)
```
改为：
```cmake
idf_component_register(
    SRCS "armlink.c" "armlink_uart.c" "armlink_frame.c" "target_track.c"
    INCLUDE_DIRS "include"
    REQUIRES ai
    PRIV_REQUIRES driver
)
```

（此时 `target_track.c` 已编入固件但尚无调用方——Task 6 接线。ESP-IDF 对"编入但暂未调用"的纯 C 文件不会报错，只需注意 `-Wunused-function` 一类告警：`target_track.c` 里的静态函数全部被 `track_update`/`track_get` 等公开函数使用，无游离未用符号，不会触发。）

- [ ] **Step 21: 整机 build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 22: Commit（测试5-7 + CMake 接线）**

```bash
git add -A
git commit -m "test(armlink): target_track加稳态收敛/滑行丢锁重捕获/挪动重锁/角度回绕测试;接入构建

7类回归测试全绿,覆盖spec §4.7全部场景。target_track.c编入
armlink组件SRCS(暂无调用方,Task 6接线)。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 5: `ai_result_t` 取帧时间戳 + espdet4 score_thr 0.25 + `/detect` 叠加门限

**Files:**
- Modify: `components/ai/include/ai.h`
- Modify: `components/ai/ai.cpp`
- Modify: `components/battery_detect4/espdet4_detect.hpp`
- Modify: `components/net/http_srv.c`

**Interfaces:**
- Consumes: 无
- Produces: `ai_result_t.capture_ts_us`（Task 6 的 `armlink_update_from_ai` 直接读取，喂给 `track_update`）。`ai_detect_jpeg` 签名变为 4 参（唯一调用方 `ai_detect_oneshot` 在本任务内同步更新，无其它调用方——已 grep 确认）。

- [ ] **Step 1: `components/ai/include/ai.h` 加时间戳字段**

将：
```c
typedef struct {
    int       count;                 // 命中框数（<=AI_MAX_BOXES）
    ai_box_t  boxes[AI_MAX_BOXES];
    uint32_t  infer_ms;              // 本次推理耗时(ms)
    int       src_w, src_h;          // 推理所用源帧尺寸
} ai_result_t;

// 构造检测器（lazy）。重复调用安全。
esp_err_t ai_init(void);
// 对一帧 JPEG 推理，结果写 out。线程安全。
esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out);
```
改为：
```c
typedef struct {
    int       count;                 // 命中框数（<=AI_MAX_BOXES）
    ai_box_t  boxes[AI_MAX_BOXES];
    uint32_t  infer_ms;              // 本次推理耗时(ms)
    int       src_w, src_h;          // 推理所用源帧尺寸
    int64_t   capture_ts_us;         // 相机拿到这帧的时刻(esp_timer_get_time(),since-boot us)；
                                      // 供 armlink 目标跟踪器的时间门限用，根治"检测结果落后于
                                      // 臂运动"的陈旧帧污染(见 2026-07-06 晚失败场景)
} ai_result_t;

// 构造检测器（lazy）。重复调用安全。
esp_err_t ai_init(void);
// 对一帧 JPEG 推理，结果写 out。capture_ts_us 由调用方传入(相机取帧时刻)。线程安全。
esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out, int64_t capture_ts_us);
```

- [ ] **Step 2: `components/ai/ai.cpp` — `run_img` 接收并落盘时间戳**

将：
```cpp
// 调用方持锁；对已 decode 的 img 跑模型并填 out，同时刷新缓存 s_last。
static void run_img(dl::image::img_t &img, ai_result_t *out) {
    int64_t t0 = esp_timer_get_time();
    auto &results = s_det->run(img);   // esp-dl 保证按 score 降序(parse_stage 有序插入,nms 只删不排)
    int64_t t1 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->src_w = img.width; out->src_h = img.height;
    out->infer_ms = (uint32_t)((t1 - t0) / 1000);
```
改为：
```cpp
// 调用方持锁；对已 decode 的 img 跑模型并填 out，同时刷新缓存 s_last。
static void run_img(dl::image::img_t &img, ai_result_t *out, int64_t capture_ts_us) {
    int64_t t0 = esp_timer_get_time();
    auto &results = s_det->run(img);   // esp-dl 保证按 score 降序(parse_stage 有序插入,nms 只删不排)
    int64_t t1 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->src_w = img.width; out->src_h = img.height;
    out->infer_ms = (uint32_t)((t1 - t0) / 1000);
    out->capture_ts_us = capture_ts_us;   // 必须在 memset 之后、s_last=*out 之前写入(见下)
```

- [ ] **Step 3: `ai_detect_jpeg` 透传参数**

将：
```cpp
extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out) {
    if (!s_det || !out) return ESP_ERR_INVALID_STATE;
    dl::image::jpeg_img_t j = {.data = (void *)jpg, .data_len = len};
    dl::image::img_t img = dl::image::sw_decode_jpeg(j, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    run_img(img, out);
    xSemaphoreGive(s_lock);
    heap_caps_free(img.data);   // sw_decode_jpeg 输出必须释放
    return ESP_OK;
}

extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    camera_fb_t *fb = camera_capture();          // 相机直出 JPEG VGA
    if (!fb) { memset(out, 0, sizeof(*out)); return ESP_FAIL; }
    esp_err_t e = ai_detect_jpeg(fb->buf, fb->len, out);
    camera_return(fb);                            // 与 camera_capture 配对，杜绝帧缓冲泄漏
    return e;
}
```
改为：
```cpp
extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out, int64_t capture_ts_us) {
    if (!s_det || !out) return ESP_ERR_INVALID_STATE;
    dl::image::jpeg_img_t j = {.data = (void *)jpg, .data_len = len};
    dl::image::img_t img = dl::image::sw_decode_jpeg(j, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    run_img(img, out, capture_ts_us);
    xSemaphoreGive(s_lock);
    heap_caps_free(img.data);   // sw_decode_jpeg 输出必须释放
    return ESP_OK;
}

extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    camera_fb_t *fb = camera_capture();          // 相机直出 JPEG VGA
    if (!fb) { memset(out, 0, sizeof(*out)); return ESP_FAIL; }
    // 取帧后立即打戳(与 armctrl 侧 esp_timer_get_time() 同源,避免跨时钟域比较);
    // 不用 camera_fb_t.timestamp(gettimeofday 域,与 esp_timer 是否同源依赖实现细节,不做假设)。
    int64_t cap_ts_us = esp_timer_get_time();
    esp_err_t e = ai_detect_jpeg(fb->buf, fb->len, out, cap_ts_us);
    camera_return(fb);                            // 与 camera_capture 配对，杜绝帧缓冲泄漏
    return e;
}
```

- [ ] **Step 4: `components/battery_detect4/espdet4_detect.hpp` 降低置信度门限**

将：
```cpp
    static inline constexpr float default_score_thr = 0.40;
```
改为：
```cpp
    static inline constexpr float default_score_thr = 0.25;  // 2026-07-07: 降至0.25让弱框进ai_result_t喂跟踪器
                                                               // (target_track双阈值关联自己再按0.40/0.25分新建/更新处理);
                                                               // 网页/叠加展示门限仍在 net/http_srv.c 单独按0.40过滤
```

- [ ] **Step 5: `components/net/http_srv.c` 的 `/detect` 只叠加高分框**

将：
```c
// 实时检测结果（JSON）。读 ai 缓存(生产者=main 的 detect_task)，不触发相机/推理，故 handler 轻。
static esp_err_t detect_get(httpd_req_t *req)
{
    ai_result_t r;
    ai_get_last(&r);
    char buf[1536];
    int n = snprintf(buf, sizeof(buf),
        "{\"w\":%d,\"h\":%d,\"infer_ms\":%u,\"n\":%d,\"boxes\":[",
        r.src_w, r.src_h, (unsigned)r.infer_ms, r.count);
    for (int i = 0; i < r.count && n < (int)sizeof(buf) - 128; i++) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"name\":\"%s\",\"s\":%.2f,\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"a\":%.1f,\"aniso\":%.2f}",
            i ? "," : "", ai_class_name(r.boxes[i].cls), r.boxes[i].score,
            r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2,
            r.boxes[i].angle_deg, r.boxes[i].anisotropy);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```
改为：
```c
// 仅用于网页叠加显示的原始框门限(与跟踪器喂料门限0.25独立;跟踪器仍吃0.25-1.0全量弱框)。
#define DETECT_OVERLAY_MIN_SCORE 0.40f

// 实时检测结果（JSON）。读 ai 缓存(生产者=main 的 detect_task)，不触发相机/推理，故 handler 轻。
// 注: "n"字段报告的是本帧原始检出总数(含弱框,供诊断参考);"boxes"数组只含>=0.40的框(网页叠加用),
// 两者数量可能不等——这是有意为之,不是bug。
static esp_err_t detect_get(httpd_req_t *req)
{
    ai_result_t r;
    ai_get_last(&r);
    char buf[1536];
    int n = snprintf(buf, sizeof(buf),
        "{\"w\":%d,\"h\":%d,\"infer_ms\":%u,\"n\":%d,\"boxes\":[",
        r.src_w, r.src_h, (unsigned)r.infer_ms, r.count);
    int written = 0;
    for (int i = 0; i < r.count && n < (int)sizeof(buf) - 128; i++) {
        if (r.boxes[i].score < DETECT_OVERLAY_MIN_SCORE) continue;
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"name\":\"%s\",\"s\":%.2f,\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"a\":%.1f,\"aniso\":%.2f}",
            written ? "," : "", ai_class_name(r.boxes[i].cls), r.boxes[i].score,
            r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2,
            r.boxes[i].angle_deg, r.boxes[i].anisotropy);
        written++;
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```

- [ ] **Step 6: 全仓残留调用点自查**

```bash
grep -rn "ai_detect_jpeg(" --include="*.cpp" --include="*.h" components/ main/
```
Expected: 只有 `components/ai/include/ai.h`（声明）与 `components/ai/ai.cpp`（定义 + 内部调用）两处，均已是 4 参版本。

- [ ] **Step 7: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(ai): ai_result_t加取帧时间戳capture_ts_us;score_thr 0.40->0.25

capture_ts_us在ai_detect_oneshot里camera_capture()返回后立即用
esp_timer_get_time()打戳(与armctrl侧同源,不用camera_fb_t.timestamp
避免跨时钟域假设),供Task 6跟踪器的时间门限用。espdet4模型置信度
门限降到0.25让弱框进入喂跟踪器,/detect网页叠加仍按0.40过滤,两者
数量故意不等。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 6: armlink 接入跟踪器（`arm_target_t` 新字段 + suspend/resume/exclusions 包装 + 并发锁）

**关键正确性点（不要跳过）**：`s_tracker`（`track_state_t`）会被两个不同 FreeRTOS 任务访问——`detect_task`（经 `armlink_update_from_ai`）与 `armctrl_task`（经 `armlink_track_suspend/resume/set_exclusions`）。必须用现有的 `s_lock` 互斥量把**所有**这些入口串行化，否则是未加锁的跨任务共享结构体访问（真实的并发 bug，不是理论风险）。

**Files:**
- Modify: `components/armlink/include/armlink.h`
- Modify: `components/armlink/armlink.c`
- Modify: `components/net/http_srv.c`（`arm_target_get` 输出新字段）

**Interfaces:**
- Consumes: `target_track.h` 的 `track_state_t/track_init/track_set_exclusions/track_suspend/track_resume/track_update/track_get/track_box_t/track_output_t`（Task 4 产出）；`ai_result_t.capture_ts_us`（Task 5 产出）。
- Produces: `arm_target_t` 新增 `stable`/`coasting` 字段；`armlink_track_suspend()`/`armlink_track_resume()`/`armlink_set_exclusions()`（Task 7 的 `armctrl.c` 直接调用；Task 9 的连续模式也调用 `armlink_set_exclusions`）。

- [ ] **Step 1: `components/armlink/include/armlink.h` 完整重写**

将整个文件内容替换为：
```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "ai.h"

#ifdef __cplusplus
extern "C" {
#endif

// 机械臂目标：从检测结果流里,经内建目标跟踪器(target_track)滤波后的"最佳电池"位姿。
typedef struct {
    bool     valid;            // 跟踪器已 confirmed(连续命中数达标)
    bool     stable;           // 已过稳定判据(含滞回); acquire_pose 据此判断可以动臂
    bool     coasting;         // 本帧靠滑行(丢检期间),不可用于开始新动作
    float    center_x_px;      // 滤波后中心 x(640x480 源像素)
    float    center_y_px;      // 滤波后中心 y
    float    angle_deg;        // 滤波后电池长轴角 [0,180)，图像 y 向下
    float    score;            // 最近一次真实关联(非滑行)帧的分数
    float    wrist_deg;        // 机械域占位(NAN,未使用;armctrl 侧经 homography 独立换算腕角)
    uint32_t src_w, src_h;     // 参考帧尺寸
    uint32_t frame_id;         // 单调递增，区分新旧目标
} arm_target_t;

// 初始化 armlink(建缓存锁 + 内建跟踪器; 若 CONFIG_ARMLINK_UART_ENABLE 则起 UART)。重复调用安全。
esp_err_t armlink_init(void);

// 从一次检测结果更新机械臂目标: 逐框喂入内建跟踪器(target_track), 派生 arm_target_t。
// 由 detect_task 每帧调用。线程安全。
void armlink_update_from_ai(const ai_result_t *r);

// 读取最近一次机械臂目标缓存。线程安全。供 /arm_target 与 armctrl::acquire_pose。
void armlink_get_last_target(arm_target_t *out);

// —— 跟踪器生命周期控制(armctrl 状态机调用；均线程安全，内部持同一把锁)——
// 挂起: 抓取序列第一个运动原语起调用,期间不关联/不预测。
void armlink_track_suspend(void);
// 恢复=硬重置: 回观察位停稳后调用(go_observe_ex 内部已自动调用)。
void armlink_track_resume(void);
// 设置连续模式防重抓排除区(px 域,最多 TRACK_MAX_EXCLUSIONS 点)。n=0 清空。
void armlink_set_exclusions(const float pts_px[][2], int n);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: `components/armlink/armlink.c` 完整重写**

将整个文件内容替换为：
```c
#include "armlink.h"
#include "armlink_uart.h"
#include "target_track.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "armlink";

// 目标选框分数下限在 target_track 内部处理(TRACK_MIN_SCORE_NEW=0.40/TRACK_MIN_SCORE_UPDATE=0.25)；
// 角度置信门限同理(TRACK_MIN_ANISOTROPY=0.10)。此处不再重复定义。

static arm_target_t      s_last;
static SemaphoreHandle_t s_lock;     // 保护 s_last 与 s_tracker 的唯一互斥量(两个任务都会访问,见 Task 6 说明)
static track_state_t     s_tracker;

esp_err_t armlink_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    memset(&s_last, 0, sizeof(s_last));
    s_last.wrist_deg = NAN;
    track_init(&s_tracker);
    esp_err_t e = armlink_uart_init();   // 禁用时为空桩返回 ESP_OK
#if CONFIG_ARMLINK_UART_ENABLE
    ESP_LOGW(TAG, "init ok — UART sink ENABLED (port=%d tx=%d), 会驱动机械臂!",
             CONFIG_ARMLINK_UART_PORT_NUM, CONFIG_ARMLINK_UART_TX_GPIO);
#else
    ESP_LOGI(TAG, "init ok — UART sink 禁用(仅产出目标, 不驱动机械臂)");
#endif
    return e;
}

void armlink_update_from_ai(const ai_result_t *r)
{
    if (!r || !s_lock) return;

    // 纯本地转换(不碰共享状态): ai_box_t -> track_box_t
    track_box_t boxes[AI_MAX_BOXES];
    int n = 0;
    for (int i = 0; i < r->count && n < AI_MAX_BOXES; i++) {
        const ai_box_t *b = &r->boxes[i];
        track_box_t *tb = &boxes[n++];
        tb->cx = 0.5f * (float)(b->x1 + b->x2);
        tb->cy = 0.5f * (float)(b->y1 + b->y2);
        tb->w  = (float)(b->x2 - b->x1);
        tb->h  = (float)(b->y2 - b->y1);
        tb->angle_deg  = b->angle_deg;
        tb->score      = b->score;
        tb->anisotropy = b->anisotropy;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_update(&s_tracker, boxes, n, r->capture_ts_us);
    track_output_t out;
    track_get(&s_tracker, &out);

    arm_target_t t;
    memset(&t, 0, sizeof(t));
    t.wrist_deg   = NAN;
    t.src_w       = (uint32_t)r->src_w;
    t.src_h       = (uint32_t)r->src_h;
    t.valid       = out.confirmed;
    t.stable      = out.stable;
    t.coasting    = out.coasting;
    t.center_x_px = out.cx;
    t.center_y_px = out.cy;
    t.angle_deg   = out.angle_deg;
    t.score       = out.score;
    t.frame_id    = s_last.frame_id + 1;   // 每次更新递增，消费端可判新旧
    s_last = t;
    xSemaphoreGive(s_lock);
}

void armlink_get_last_target(arm_target_t *out)
{
    if (!out) return;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_last;
    if (s_lock) xSemaphoreGive(s_lock);
}

void armlink_track_suspend(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_suspend(&s_tracker);
    xSemaphoreGive(s_lock);
}

void armlink_track_resume(void)
{
    if (!s_lock) return;
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_resume(&s_tracker, now);
    xSemaphoreGive(s_lock);
}

void armlink_set_exclusions(const float pts_px[][2], int n)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_set_exclusions(&s_tracker, pts_px, n);
    xSemaphoreGive(s_lock);
}
```

- [ ] **Step 3: `components/net/http_srv.c` 的 `arm_target_get` 输出新字段**

将：
```c
// 机械臂目标（JSON）。读 armlink 缓存（生产者=detect_task），不触发相机/推理。
static esp_err_t arm_target_get(httpd_req_t *req)
{
    arm_target_t t;
    armlink_get_last_target(&t);
    char buf[256];
    int n;
    if (t.valid) {
        // wrist_deg 本步未标定，恒输出 null（标定后改真实值）
        n = snprintf(buf, sizeof(buf),
            "{\"valid\":true,\"cx\":%.1f,\"cy\":%.1f,\"angle_deg\":%.1f,\"score\":%.2f,"
            "\"wrist_deg\":null,\"w\":%u,\"h\":%u,\"frame_id\":%u}",
            t.center_x_px, t.center_y_px, t.angle_deg, t.score,
            (unsigned)t.src_w, (unsigned)t.src_h, (unsigned)t.frame_id);
    } else {
        n = snprintf(buf, sizeof(buf), "{\"valid\":false,\"frame_id\":%u}", (unsigned)t.frame_id);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```
改为：
```c
// 机械臂目标（JSON）。读 armlink 缓存（生产者=detect_task），不触发相机/推理。
static esp_err_t arm_target_get(httpd_req_t *req)
{
    arm_target_t t;
    armlink_get_last_target(&t);
    char buf[256];
    int n;
    if (t.valid) {
        n = snprintf(buf, sizeof(buf),
            "{\"valid\":true,\"stable\":%s,\"coasting\":%s,\"cx\":%.1f,\"cy\":%.1f,"
            "\"angle_deg\":%.1f,\"score\":%.2f,\"w\":%u,\"h\":%u,\"frame_id\":%u}",
            t.stable ? "true" : "false", t.coasting ? "true" : "false",
            t.center_x_px, t.center_y_px, t.angle_deg, t.score,
            (unsigned)t.src_w, (unsigned)t.src_h, (unsigned)t.frame_id);
    } else {
        n = snprintf(buf, sizeof(buf), "{\"valid\":false,\"frame_id\":%u}", (unsigned)t.frame_id);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```

- [ ] **Step 4: 全仓残留引用自查**

```bash
grep -rn "wrist_deg\":null" --include="*.c" components/ main/
```
Expected: 零匹配（已改为真实 stable/coasting 字段，`wrist_deg` 本身仍是占位 NAN 但不再单独输出到 JSON）。

- [ ] **Step 5: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(armlink): 接入target_track跟踪器,arm_target_t加stable/coasting字段

armlink_update_from_ai逐框转换喂给跟踪器,派生结果写s_last;新增
armlink_track_suspend/resume/set_exclusions供armctrl调用(Task 7/9)。
s_tracker与s_last同受s_lock保护,消除跨任务(detect_task/armctrl_task)
无锁访问风险。/arm_target端点输出stable/coasting。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 7: armctrl `acquire_pose` 重写为等待 STABLE + track_suspend/resume 接线

**Files:**
- Modify: `components/armctrl/armctrl.c`

**Interfaces:**
- Consumes: `armlink_track_suspend()`/`armlink_track_resume()`（Task 6 产出）、`arm_target_t.stable`（Task 6 产出）。
- Produces: `acquire_pose(float*,float*,float*)` 签名不变（Task 10 会再加一个 `armctrl_get_stats`/事件相关的独立改动，不动这个签名）。

- [ ] **Step 1: 删除旧的 5 帧抖动窗口逻辑，改为等待 STABLE**

将：
```c
#define POSE_FRAMES 5
#define POSE_INTERVAL_MS 60
#define POSE_FRESH_TIMEOUT_MS 1500
#define POSE_CENTER_RANGE_PX 12.0f  // 2026-07-06 由4px放宽: 4px在640x480实拍下过严,量化噪声+像素抖动易频繁触发"位姿不稳"重试
#define POSE_ANGLE_RANGE_DEG 12.0f

// 连续读 N 帧目标缓存, 抖动超门限判失败, 否则输出中心/角度均值。
static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    float cx[POSE_FRAMES], cy[POSE_FRAMES], ang[POSE_FRAMES];
    uint32_t prev_fid = 0;
    for (int i = 0; i < POSE_FRAMES; i++) {
        arm_target_t t;
        int waited = 0;
        // 第0帧只需有效; 后续帧必须是新检测(frame_id 变化)。每 POSE_INTERVAL_MS 轮询, 超 POSE_FRESH_TIMEOUT_MS 判失败。
        while (1) {
            armlink_get_last_target(&t);
            if (t.valid && (i == 0 || t.frame_id != prev_fid)) break;
            if (waited >= POSE_FRESH_TIMEOUT_MS) {
                ESP_LOGW(TAG, "pose: 第%d帧等新检测超时", i);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(POSE_INTERVAL_MS));
            waited += POSE_INTERVAL_MS;
        }
        prev_fid = t.frame_id;
        cx[i] = t.center_x_px; cy[i] = t.center_y_px; ang[i] = t.angle_deg;
    }
    float minx = cx[0], maxx = cx[0], miny = cy[0], maxy = cy[0];
    float mina = ang[0], maxa = ang[0], sx = 0, sy = 0, sa = 0;
    for (int i = 0; i < POSE_FRAMES; i++) {
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
        if (ang[i] < mina) mina = ang[i];
        if (ang[i] > maxa) maxa = ang[i];
        sx += cx[i]; sy += cy[i]; sa += ang[i];
    }
    if ((maxx - minx) > POSE_CENTER_RANGE_PX || (maxy - miny) > POSE_CENTER_RANGE_PX) {
        ESP_LOGW(TAG, "pose: 中心抖动 %.1f,%.1f", maxx - minx, maxy - miny); return false;
    }
    if ((maxa - mina) > POSE_ANGLE_RANGE_DEG) {
        ESP_LOGW(TAG, "pose: 角度抖动 %.1f", maxa - mina); return false;
    }
    *out_px = sx / POSE_FRAMES; *out_py = sy / POSE_FRAMES; *out_ang = sa / POSE_FRAMES;
    ESP_LOGI(TAG, "pose ok px=%.1f py=%.1f ang=%.1f", *out_px, *out_py, *out_ang);
    return true;
}
```
改为：
```c
// 等待 armlink 内建跟踪器(target_track)进入 STABLE(见 armlink/target_track.h),超时判失败。
// 旧的"5帧中心/角度抖动窗口"逻辑已整体退役——抖动判定、时间门限、丢检滑行现全部
// 在 target_track 内部完成(见 docs/superpowers/specs/2026-07-06-final-submission-cleanup-design.md §4.6)。
#define ACQUIRE_TIMEOUT_MS 8000
#define ACQUIRE_POLL_MS    60

static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    int waited = 0;
    while (1) {
        arm_target_t t;
        armlink_get_last_target(&t);
        if (t.valid && t.stable) {
            *out_px = t.center_x_px; *out_py = t.center_y_px; *out_ang = t.angle_deg;
            ESP_LOGI(TAG, "pose ok(stable) px=%.1f py=%.1f ang=%.1f", *out_px, *out_py, *out_ang);
            return true;
        }
        if (waited >= ACQUIRE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "pose: 等待STABLE超时(%dms)", ACQUIRE_TIMEOUT_MS);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(ACQUIRE_POLL_MS));
        waited += ACQUIRE_POLL_MS;
    }
}
```

- [ ] **Step 2: `go_observe_ex` 末尾接入 `armlink_track_resume()`**

将：
```c
static void go_observe_ex(bool open_grip)
{
    if (open_grip) {
        armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 开爪
    }
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 600);                     // 腕中位
    armctrl_move_arm(0, s_cal.observe_y, s_cal.carry_z, 1200);             // 中转(正前高处)
    armctrl_move_arm(s_cal.observe_x, s_cal.observe_y, s_cal.observe_z, 1200);
}
```
改为：
```c
static void go_observe_ex(bool open_grip)
{
    if (open_grip) {
        armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 开爪
    }
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 600);                     // 腕中位
    armctrl_move_arm(0, s_cal.observe_y, s_cal.carry_z, 1200);             // 中转(正前高处)
    armctrl_move_arm(s_cal.observe_x, s_cal.observe_y, s_cal.observe_z, 1200);
    armlink_track_resume();   // 臂已停稳在观察位: 跟踪器硬重置+记新门限时间戳(根治运动帧污染)
}
```

- [ ] **Step 3: `armctrl_task` 里 acquire_pose 成功后立即挂起跟踪器**

将：
```c
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
```
改为：
```c
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
```

- [ ] **Step 4: 全仓残留引用自查**

```bash
grep -rn "POSE_FRAMES\|POSE_INTERVAL_MS\|POSE_FRESH_TIMEOUT_MS\|POSE_CENTER_RANGE_PX\|POSE_ANGLE_RANGE_DEG" components/
```
Expected: 零匹配。

- [ ] **Step 5: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor(armctrl): acquire_pose改为等待STABLE;接线track_suspend/resume

旧5帧中心/角度抖动窗口逻辑整体退役,判稳逻辑现全在target_track内部
(时间门限+关联门限+滞回)。go_observe_ex末尾resume跟踪器(臂停稳=新
门限时刻);acquire_pose成功后立即suspend(抓取序列期间不关联)。
这正是2026-07-06晚失败场景的根治点。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 8: armctrl 急停（`$DST:0!`）+ `/arm_estop` 端点

**急停指令订正（已与用户确认，见 spec 2026-07-07 订正）**：唯一真机验证过的急停串是 **`$DST:0!`**（`CRASH_SIGNATURES.md` 2026-07-02 COM4 直连实测：回显+派发，KM1 内部重路由到 `#000PDST!`）。`SAFETY.md`/旧 spec 里的裸 `$DST!` 从未在真机测试出现过，本任务同时订正 `SAFETY.md`。

**Files:**
- Modify: `components/armctrl/include/armctrl.h`
- Modify: `components/armctrl/armctrl.c`
- Modify: `components/net/http_srv.c`
- Modify: `docs/ai/SAFETY.md`

**Interfaces:**
- Consumes: 无
- Produces: `armctrl_estop()`/`armctrl_is_estopped()`/`armctrl_clear_estop()`（Task 9 的 `armctrl_request_run` 会检查 `s_estop`，net 端点在本任务内接线）。

- [ ] **Step 1: `components/armctrl/include/armctrl.h` 加急停声明**

将：
```c
esp_err_t armctrl_init(void);
void armctrl_request_run(bool on);
bool armctrl_is_running(void);
void armctrl_reload_cal(void);        // 请求重载 NVS 标定(armctrl 空闲时生效, 免重启; 供 /arm_calib POST 后调用)
```
改为：
```c
esp_err_t armctrl_init(void);
void armctrl_request_run(bool on);
bool armctrl_is_running(void);
void armctrl_reload_cal(void);        // 请求重载 NVS 标定(armctrl 空闲时生效, 免重启; 供 /arm_calib POST 后调用)

// 急停: 立即发送 $DST:0!(唯一真机验证过的急停串,见 CRASH_SIGNATURES.md 2026-07-02)并停止循环,
// 锁存直到 armctrl_clear_estop() 被调用——期间任何 run 请求都会被拒绝。
void armctrl_estop(void);
bool armctrl_is_estopped(void);
void armctrl_clear_estop(void);
```

- [ ] **Step 2: `components/armctrl/armctrl.c` 加 `s_estop` 变量与急停函数**

将：
```c
static armcal_t s_cal;
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
```
改为：
```c
static armcal_t s_cal;
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;
static volatile bool s_cal_dirty = false;   // /arm_calib POST 后置位, armctrl 空闲时重载标定(免重启)
static volatile bool s_estop = false;   // 急停锁存; 置位后拒绝任何新 run,且原语层直接短路不发字节

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_request_run(bool on) {
    if (on && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中,先 /arm_estop?on=0 清除"); return; }
    s_run = on;
    ESP_LOGW(TAG, "run=%d", on);
}

void armctrl_estop(void)
{
    s_estop = true;
    s_run = false;
#if CONFIG_ARMLINK_UART_ENABLE
    armlink_uart_send("$DST:0!", 7);
#endif
    ESP_LOGW(TAG, "急停! 已发送 $DST:0! 并停止循环");
}

bool armctrl_is_estopped(void) { return s_estop; }

void armctrl_clear_estop(void)
{
    s_estop = false;
    ESP_LOGW(TAG, "急停已清除");
}
```

- [ ] **Step 3: `armctrl_move_arm`/`armctrl_move_servo` 加急停短路**

将：
```c
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    int pwm[4];
```
改为：
```c
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    if (s_estop) { ESP_LOGW(TAG, "[estop] 忽略 move_arm"); return ESP_ERR_INVALID_STATE; }
    int pwm[4];
```

将：
```c
void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    char frame[32];
```
改为：
```c
void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    if (s_estop) { ESP_LOGW(TAG, "[estop] 忽略 move_servo"); return; }
    char frame[32];
```

- [ ] **Step 4: `armctrl_task` 顶部守卫加 `s_estop` 检查**

将：
```c
        if (!s_run || !s_ik_ok || !s_cal.valid) {
            if (s_cal_dirty) {
                s_cal_dirty = false;
                armcal_load(&s_cal);
                ESP_LOGI(TAG, "标定已重载 valid=%d", s_cal.valid);
            }
            if (s_run && !s_ik_ok) { ESP_LOGW(TAG, "run 被拒: IK 自检未过"); s_run = false; }
            else if (s_run && !s_cal.valid) { ESP_LOGW(TAG, "run 被拒: 未标定"); s_run = false; }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
```
改为：
```c
        if (!s_run || !s_ik_ok || !s_cal.valid || s_estop) {
            if (s_cal_dirty) {
                s_cal_dirty = false;
                armcal_load(&s_cal);
                ESP_LOGI(TAG, "标定已重载 valid=%d", s_cal.valid);
            }
            if (s_run && !s_ik_ok) { ESP_LOGW(TAG, "run 被拒: IK 自检未过"); s_run = false; }
            else if (s_run && !s_cal.valid) { ESP_LOGW(TAG, "run 被拒: 未标定"); s_run = false; }
            else if (s_run && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中"); s_run = false; }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
```

- [ ] **Step 5: `components/net/http_srv.c` 加 `/arm_estop` handler**

在 `arm_run_get` 函数定义后（`capture_get` 之前）插入：
```c
// 急停: /arm_estop?on=1 立即停止并锁存; ?on=0 清除锁存(之后才能再次 run)。
static esp_err_t arm_estop_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
            if (val[0] == '1') armctrl_estop();
            else armctrl_clear_estop();
        }
    }
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"estopped\":%s}", armctrl_is_estopped() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```

并在 `net_http_start()` 的注册列表末尾（`run` 之后）加：
```c
    httpd_uri_t estop = { .uri = "/arm_estop", .method = HTTP_GET, .handler = arm_estop_get };
    httpd_register_uri_handler(server, &estop);
```

- [ ] **Step 6: `docs/ai/SAFETY.md` 订正急停字符串**

将：
```
- 急停方式：物理电源开关断动力电（首选）；软件 `$DST!`（总线停）+ 停止抓取循环。
```
改为：
```
- 急停方式：物理电源开关断动力电（首选）；软件 `$DST:0!`（总线停，**2026-07-07 订正**：唯一真机验证过的急停串，见 `docs/ai/CRASH_SIGNATURES.md` 2026-07-02；旧文档误写裸 `$DST!` 从未在真机测试出现过）+ 停止抓取循环。
```

- [ ] **Step 7: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(armctrl,net): 急停\$DST:0!+ /arm_estop端点;订正SAFETY.md急停串

armctrl_estop()发\$DST:0!(唯一真机验证过,CRASH_SIGNATURES 2026-07-02)
+停循环+锁存;move_arm/move_servo原语层短路兜底(每个运动原语间都
检查);armctrl_task顶部守卫同步拒绝。SAFETY.md旧写的裸\$DST!订正。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 9: armctrl 连续模式 + px 域防重抓排除表 + `/arm_run` 加 `cont` 参数

**设计取舍说明**（写进 commit，供日后查阅）：排除表在**每次 `/arm_run?on=1` 请求时清空**（不论 `cont` 是否为 1）——"本次上电内存"理解为"本次运行会话"，避免上一次运行的排除点污染下一次全新会话。3 次连续 `acquire_pose` 超时自动停止的逻辑对单轮/连续模式一视同仁（更安全，防止单轮模式在无目标时无限重试耗尽演示时间）。

**Files:**
- Modify: `components/armctrl/include/armctrl.h`
- Modify: `components/armctrl/armctrl.c`
- Modify: `components/net/http_srv.c`

**Interfaces:**
- Consumes: `armlink_set_exclusions()`（Task 6 产出）。
- Produces: `armctrl_request_run(bool on, bool cont)`（**签名变更**，本任务同步更新其唯一调用方 `http_srv.c` 的 `arm_run_get`）、`armctrl_is_continuous()`。

- [ ] **Step 1: `components/armctrl/include/armctrl.h` 改 `armctrl_request_run` 签名**

将：
```c
void armctrl_request_run(bool on);
bool armctrl_is_running(void);
```
改为：
```c
void armctrl_request_run(bool on, bool cont);
bool armctrl_is_running(void);
bool armctrl_is_continuous(void);
```

- [ ] **Step 2: `components/armctrl/armctrl.c` 改函数体 + 加连续模式状态**

将：
```c
void armctrl_request_run(bool on) {
    if (on && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中,先 /arm_estop?on=0 清除"); return; }
    s_run = on;
    ESP_LOGW(TAG, "run=%d", on);
}
```
改为：
```c
static volatile bool s_continuous = false;
static float s_processed_px[8][2];   // 连续模式防重抓: 本次运行会话已处理目标的px中心
static int   s_processed_count = 0;
static int   s_acquire_fail_streak = 0;

void armctrl_request_run(bool on, bool cont) {
    if (on && s_estop) { ESP_LOGW(TAG, "run 被拒: 急停锁存中,先 /arm_estop?on=0 清除"); return; }
    s_run = on;
    s_continuous = cont;
    if (on) {
        s_processed_count = 0;
        s_acquire_fail_streak = 0;
        armlink_set_exclusions(NULL, 0);   // 新会话: 清空上一次运行留下的排除点
    }
    ESP_LOGW(TAG, "run=%d cont=%d", on, cont);
}

bool armctrl_is_continuous(void) { return s_continuous; }
```

- [ ] **Step 3: `armctrl_task` 里接入 acquire 失败计数 + 挂起 + 排除记录 + 连续循环判断**

将：
```c
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        s_run = false;   // 单轮; 连续模式在 Task 9 加(改这里的判断)
        vTaskDelay(pdMS_TO_TICKS(200));
```
改为：
```c
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试");
            s_acquire_fail_streak++;
            if (s_acquire_fail_streak >= 3) {
                ESP_LOGW(TAG, "连续3次未能稳定获取目标,自动停止");
                s_run = false;
                s_acquire_fail_streak = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        s_acquire_fail_streak = 0;
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        // 记录本次目标 px 中心到防重抓排除表(px域: 放回原位后,同一观察位下一轮会在同一像素
        // 位置再次被检出;记 px 而非 mm 是零坐标转换的最简方案)。
        if (s_processed_count < 8) {
            s_processed_px[s_processed_count][0] = px;
            s_processed_px[s_processed_count][1] = py;
            s_processed_count++;
            armlink_set_exclusions(s_processed_px, s_processed_count);
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        if (!s_continuous) {
            s_run = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
```

- [ ] **Step 4: `components/net/http_srv.c` 的 `arm_run_get` 解析 `cont` 参数**

将：
```c
// 启停一轮抓取: /arm_run?on=1|0。
static esp_err_t arm_run_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
            httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
            armctrl_request_run(val[0] == '1');
        }
    }
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "{\"running\":%s}", armctrl_is_running() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```
改为：
```c
// 启停一轮/连续抓取: /arm_run?on=1|0&cont=1|0。
static esp_err_t arm_run_get(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 32) {
        char q[32], val_on[4], val_cont[4];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            int on = -1, cont = 0;
            if (httpd_query_key_value(q, "on", val_on, sizeof(val_on)) == ESP_OK) on = (val_on[0] == '1');
            if (httpd_query_key_value(q, "cont", val_cont, sizeof(val_cont)) == ESP_OK) cont = (val_cont[0] == '1');
            if (on >= 0) armctrl_request_run(on != 0, cont != 0);
        }
    }
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"running\":%s,\"cont\":%s}",
                     armctrl_is_running() ? "true" : "false",
                     armctrl_is_continuous() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```

- [ ] **Step 5: 全仓残留调用点自查**

```bash
grep -rn "armctrl_request_run(" --include="*.c" components/ main/
```
Expected: 只有 `armctrl.c`(定义) 与 `http_srv.c`(调用) 两处，调用处已是 2 参版本。

- [ ] **Step 6: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(armctrl,net): 连续模式+px域防重抓排除表+/arm_run加cont参数

armctrl_request_run(on,cont);cont=1时完整循环结束后不停,回观察位
继续下一轮;每轮成功后把目标px中心记入排除表(<=8项,新会话/arm_run
时清空)喂给跟踪器,防止刚放回的电池被立刻重抓。连续3次acquire_pose
超时自动停(单轮/连续都适用,防止无目标时死等耗尽演示时间)。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 10: armctrl dashboard 事件钩子 + NVS `stats` 统计（队友 WebSocket 对接用）

**范围说明**：本任务只提供钩子（回调 + 计数），不实现 WebSocket——那是 Task 11 交付的计划书里队友的工作。`armctrl_cycle_log_t` 目前不含电池类别/分数（`target_track` 的输出本身不携带类别，且当前 `arm_target_t` 也不追踪具体类别），这是已知的、有意为之的最小化范围，已在 Task 11 的对接文档里向队友挑明。

**Files:**
- Modify: `components/armctrl/include/armctrl.h`
- Modify: `components/armctrl/armctrl.c`
- Modify: `components/armctrl/CMakeLists.txt`

**Interfaces:**
- Consumes: 无
- Produces: `armctrl_cycle_log_t`/`armctrl_event_cb_t`/`armctrl_set_event_cb()`/`armctrl_get_stats()`（队友的 `net_ws.c` 直接调用，见 Task 11 的对接文档）。

- [ ] **Step 1: `components/armctrl/include/armctrl.h` 加类型与函数声明**

在文件末尾 `#ifdef __cplusplus }` 之前插入：
```c
// —— dashboard 事件钩子(队友 WebSocket 集成用,见 docs/ai/DASHBOARD_INTEGRATION.md) ——
#include <stdint.h>

typedef struct {
    uint32_t seq_id;             // 单调递增,每轮+1
    int64_t  t_identified_us;    // acquire_pose 成功(STABLE)时刻
    int64_t  t_picked_us;        // pick_sequence 成功时刻(失败为0)
    int64_t  t_cut_us;           // cut_sequence 成功时刻(未到达/失败为0)
    int64_t  t_placed_us;        // place_back 完成时刻(失败也记录尝试完成的时刻;为0表示未到达)
    bool     ok;                 // 本轮是否完整成功(pick+cut+place 全过)
} armctrl_cycle_log_t;

typedef void (*armctrl_event_cb_t)(const armctrl_cycle_log_t *log, void *arg);

// 注册每轮终止(成功或失败提前结束)时的回调; cb=NULL 取消注册。
// 线程安全提示: 回调在 armctrl_task 内直接同步调用,实现必须快速返回、不可阻塞/不可长时间持锁。
void armctrl_set_event_cb(armctrl_event_cb_t cb, void *arg);

// 读取处理计数: total=累计(NVS "stats"命名空间持久化,与 armcal 命名空间完全独立),
// session=本次上电内计数(不持久化)。
void armctrl_get_stats(uint32_t *out_total, uint32_t *out_session);
```

- [ ] **Step 2: `components/armctrl/armctrl.c` 加 include + 统计 NVS 存取 + 回调状态**

将文件顶部的 include 块：
```c
#include "armctrl.h"
#include "armcal.h"
#include "kinematics.h"
#include "armlink_frame.h"
#include "armlink_uart.h"
#include "armlink.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
```
改为：
```c
#include "armctrl.h"
#include "armcal.h"
#include "kinematics.h"
#include "armlink_frame.h"
#include "armlink_uart.h"
#include "armlink.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <math.h>
#include <inttypes.h>
```

在 `s_acquire_fail_streak` 声明后（Task 9 已加）追加：
```c
// —— dashboard 统计: 独立 NVS 命名空间"stats",与 armcal 完全隔离,不影响标定数据 ——
#define STATS_NS  "stats"
#define STATS_KEY "cnt"
static uint32_t s_stats_total = 0;
static uint32_t s_stats_session = 0;
static armctrl_event_cb_t s_event_cb = NULL;
static void *s_event_cb_arg = NULL;
static uint32_t s_cycle_seq = 0;

static void stats_load(void)
{
    nvs_handle_t h;
    if (nvs_open(STATS_NS, NVS_READONLY, &h) != ESP_OK) { s_stats_total = 0; return; }
    uint32_t v = 0;
    size_t sz = sizeof(v);
    if (nvs_get_blob(h, STATS_KEY, &v, &sz) == ESP_OK && sz == sizeof(v)) s_stats_total = v;
    else s_stats_total = 0;
    nvs_close(h);
}

static void stats_save(void)
{
    nvs_handle_t h;
    if (nvs_open(STATS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, STATS_KEY, &s_stats_total, sizeof(s_stats_total));
    nvs_commit(h);
    nvs_close(h);
}

void armctrl_set_event_cb(armctrl_event_cb_t cb, void *arg) { s_event_cb = cb; s_event_cb_arg = arg; }

void armctrl_get_stats(uint32_t *out_total, uint32_t *out_session)
{
    if (out_total) *out_total = s_stats_total;
    if (out_session) *out_session = s_stats_session;
}

static void emit_cycle_log(int64_t t_id, int64_t t_pick, int64_t t_cut, int64_t t_place, bool ok)
{
    if (!s_event_cb) return;
    armctrl_cycle_log_t log = {
        .seq_id = ++s_cycle_seq,
        .t_identified_us = t_id,
        .t_picked_us = t_pick,
        .t_cut_us = t_cut,
        .t_placed_us = t_place,
        .ok = ok,
    };
    s_event_cb(&log, s_event_cb_arg);
}
```

- [ ] **Step 3: `armctrl_init` 里加载统计**

将：
```c
esp_err_t armctrl_init(void)
{
    esp_err_t e = armcal_load(&s_cal);
    if (e != ESP_OK) ESP_LOGW(TAG, "标定未就绪(valid=false), 自动模式将被拒绝");
    s_ik_ok = (kin_selftest() == 0);
    if (!s_ik_ok) ESP_LOGE(TAG, "IK 自检失败, 自动模式禁用");
    xTaskCreate(armctrl_task, "armctrl", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "init ok (run=off,ik=%d,cal=%d)", s_ik_ok, s_cal.valid);
    return ESP_OK;
}
```
改为：
```c
esp_err_t armctrl_init(void)
{
    esp_err_t e = armcal_load(&s_cal);
    if (e != ESP_OK) ESP_LOGW(TAG, "标定未就绪(valid=false), 自动模式将被拒绝");
    s_ik_ok = (kin_selftest() == 0);
    if (!s_ik_ok) ESP_LOGE(TAG, "IK 自检失败, 自动模式禁用");
    stats_load();
    xTaskCreate(armctrl_task, "armctrl", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "init ok (run=off,ik=%d,cal=%d,stats_total=%" PRIu32 ")", s_ik_ok, s_cal.valid, s_stats_total);
    return ESP_OK;
}
```

- [ ] **Step 4: `armctrl_task` 里打点时间戳 + 各终止路径调用 `emit_cycle_log` + 成功路径累加统计**

将：
```c
        s_acquire_fail_streak = 0;
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        // 记录本次目标 px 中心到防重抓排除表(px域: 放回原位后,同一观察位下一轮会在同一像素
        // 位置再次被检出;记 px 而非 mm 是零坐标转换的最简方案)。
        if (s_processed_count < 8) {
            s_processed_px[s_processed_count][0] = px;
            s_processed_px[s_processed_count][1] = py;
            s_processed_count++;
            armlink_set_exclusions(s_processed_px, s_processed_count);
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        if (!s_continuous) {
            s_run = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
```
改为：
```c
        s_acquire_fail_streak = 0;
        int64_t t_identified = esp_timer_get_time();
        armlink_track_suspend();   // 即将开始运动序列: 挂起跟踪器,直到下次 go_observe_ex 内部 resume
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            emit_cycle_log(t_identified, 0, 0, 0, false);
            go_observe(); s_run = false; continue;
        }
        int64_t t_picked = esp_timer_get_time();
        // 切割: 抓起 -> 移刀口切割 -> 放回 -> 回观察位(G分级已去除,始终走完整含切流程)
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持撤离刀口回观察位(等人工取回)");
            emit_cycle_log(t_identified, t_picked, 0, 0, false);
            (void)armctrl_move_arm(s_cal.blade_x, s_cal.blade_y, s_cal.blade_safe_z, 1400);
            go_observe_ex(false);   // 不开爪 —— 半切开的电池绝不在刀口旁松掉
            s_run = false;
            continue;
        }
        int64_t t_cut = esp_timer_get_time();
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        int64_t t_placed = esp_timer_get_time();
        emit_cycle_log(t_identified, t_picked, t_cut, t_placed, true);
        // 记录本次目标 px 中心到防重抓排除表(px域: 放回原位后,同一观察位下一轮会在同一像素
        // 位置再次被检出;记 px 而非 mm 是零坐标转换的最简方案)。
        if (s_processed_count < 8) {
            s_processed_px[s_processed_count][0] = px;
            s_processed_px[s_processed_count][1] = py;
            s_processed_count++;
            armlink_set_exclusions(s_processed_px, s_processed_count);
        }
        s_stats_total++;
        s_stats_session++;
        stats_save();
        go_observe();
        ESP_LOGI(TAG, "完整循环完成(累计%" PRIu32 "/本次%" PRIu32 ")", s_stats_total, s_stats_session);
        if (!s_continuous) {
            s_run = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
```

- [ ] **Step 5: `components/armctrl/CMakeLists.txt` 加 `nvs_flash` 依赖**

将：
```cmake
idf_component_register(
    SRCS "armctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES armcal kinematics armlink
    PRIV_REQUIRES driver
)
```
改为：
```cmake
idf_component_register(
    SRCS "armctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES armcal kinematics armlink
    PRIV_REQUIRES driver nvs_flash
)
```

- [ ] **Step 6: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(armctrl): dashboard事件钩子(armctrl_set_event_cb)+NVS独立stats统计

armctrl_cycle_log_t含4个阶段时间戳(identified/picked/cut/placed)+
ok标志,armctrl_task在每轮的3个终止路径(pick失败/cut失败/完整成功)
都调用emit_cycle_log;stats用独立NVS命名空间\"stats\",与armcal完全
隔离,不影响标定数据。范围明确不含电池类别(target_track当前不追踪
类别),已在Task 11对接文档向队友挑明。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 11: `docs/ai/DASHBOARD_INTEGRATION.md`（队友 WebSocket 对接计划书）

本任务是纯文档交付——固件钩子已在 Task 10 就位，本任务把"怎么用这些钩子实现 `chargereborn-dashboard/PROTOCOL.md` 里的六类消息"写清楚，交给做 dashboard 对接的队友。

**Files:**
- Create: `docs/ai/DASHBOARD_INTEGRATION.md`

**Interfaces:**
- Consumes: Task 10 的 `armctrl_cycle_log_t`/`armctrl_set_event_cb`/`armctrl_get_stats`/`armctrl_estop`；`chargereborn-dashboard/PROTOCOL.md` 的六类消息格式。
- Produces: 无代码产出，供队友后续实现 `components/net/net_ws.c` 时参照。

- [ ] **Step 1: 写 `docs/ai/DASHBOARD_INTEGRATION.md`**

```markdown
# DASHBOARD_INTEGRATION.md — chargereborn-dashboard 固件对接计划书

> 面向实现 `components/net/net_ws.c`（新文件，你来写）的队友。固件侧的埋点已经就位
> （见 `components/armctrl/include/armctrl.h` 的 `armctrl_set_event_cb`/`armctrl_get_stats`/
> `armctrl_estop`），本文档告诉你怎么把这些接到 `chargereborn-dashboard/PROTOCOL.md`
> 定义的六类 WebSocket 消息上。

## 1. 固件侧要加什么（新文件，不改 armctrl/armlink/ai 内部）

1. `menuconfig` 开 `CONFIG_HTTPD_WS_SUPPORT=y`（ESP-IDF `esp_http_server` 组件的 WebSocket
   支持，5.5.4 自带，不需要额外组件）。
2. 新建 `components/net/net_ws.c` + `net_ws.h`，在**现有** `net_http_start()` 的同一个
   `httpd_handle_t`（80 口）上注册一个 `/ws` 的 `httpd_uri_t`（`.is_websocket = true`）。
   **不要另起一个 httpd 实例**——SoftAP 单核资源紧张，复用现有 server 更省内存/更简单。
3. `chargereborn-dashboard/app.js` 的默认地址（`ws://192.168.4.1:8080/ws`）改成
   `ws://192.168.4.1/ws`（80 口，免第二个端口）。这是 `app.js` 里的一行常量改动。

## 2. PROTOCOL.md 六类消息 ↔ 固件数据源映射

| PROTOCOL.md 消息 | 固件数据源 | 实现方式 |
|---|---|---|
| `device_status` | `ai_get_last()`(fps 用 `1000/infer_ms` 估算) + `armctrl_is_running()` | 起一个 1Hz FreeRTOS 定时任务，组 JSON 广播给所有连接的 ws 客户端 |
| `battery_log` | `armctrl_set_event_cb()` 注册的回调 | 回调里把 `armctrl_cycle_log_t` 的 4 个时间戳（`t_identified_us`/`t_picked_us`/`t_cut_us`/`t_placed_us`，单位 us，需要你自己转成 `"YYYY-MM-DD HH:MM:SS"` 或前端能读的格式）与 `ok` 字段组成 `battery_log` JSON。**已知缺口**：当前不追踪具体电池类别（`model`/`confidence` 字段）——`target_track` 只输出几何量，不带类别；`id` 字段可以先用 `seq_id` 顶替（如 `"BAT-" + seq_id`），如果确实需要类别/置信度，需要先扩展 `armlink`/`target_track` 让最佳关联帧的 `cls`/`score` 也透传出来，这是本轮范围外的工作，量不大但需要改 `track_output_t` 加字段 + `armlink.c` 透传，评估后再做 |
| `carbon_update` | `armctrl_get_stats()` 的 `total`/`session` | 按你们定的"每颗电池节约多少克 CO2"系数相乘；`today`/`week`/`series` 若没有按天分桶的持久化，可以先用 `session`（本次上电内计数）近似顶替，说明是近似值 |
| `control`(`ESTOP`) | `armctrl_estop()` | 收到 ws 消息后直接调用，不要自己重新发 `$DST:0!`——`armctrl_estop()` 已经处理好了发送+锁存+停循环。回 `command_ack` |
| `webrtc_offer`/`webrtc_answer`/`webrtc_ice` | 无固件数据源 | 若时间来不及打通完整 WebRTC 信令，直接跳过，用下一行的 `video_frame` 回退方案 |
| `video_frame` | `camera_capture()` 拿到的 JPEG buffer | 建议 **≤2fps**、单客户端限流：JPEG 直接 base64（膨胀 1.33x），`httpd_ws_send_frame_async` 异步发送避免阻塞 detect_task。QVGA 分辨率下预估带宽 100-200Kbps，SoftAP 单客户端可行；多客户端/高帧率会挤占检测循环的相机吞吐，需要实测 |
| `trace_query` | 无——需要你自己加一个环形缓冲 | 建议在 `net_ws.c` 里维护一个 RAM 环形数组（≥16 条 `armctrl_cycle_log_t` 副本），`armctrl_set_event_cb` 的回调里 push 进去；收到 `trace_query` 按 `id`/`seq_id` 线性查找 |

## 3. 已知的表述纠正

- `chargereborn-dashboard/DEFENSE_GUIDE.md` 第 27 行答辩话术提到"Brain 再通过 ESP-NOW 通知机械臂控制端停机"——**本项目没有 ESP-NOW**，Brain→KM1 是直接 UART（GPIO1→KM1 RX2/GPIO41）。答辩话术需要改成"Brain 经 UART 直接向机械臂控制板发送急停指令"，避免被现场提问戳穿。

## 4. 验证步骤（分阶段，不要一把梭）

1. **PC 端假连接**：用 `wscat`（或任意 WS 客户端）连 `ws://<板子IP>/ws`，逐条手测：连接后应收到周期性 `device_status`；发 `{"type":"control","command":"ESTOP",...}` 应看到板子日志出现 `armctrl_estop` 的 `[estop]` 日志且收到 `command_ack`。
2. **真实 dashboard 联调**：`chargereborn-dashboard/index.html` 直接打开，连板子，过一遍 `DEFENSE_GUIDE.md` 的五步答辩话术，逐项核对界面显示与固件实际状态一致。
3. **并发稳定性**：dashboard 保持连接的同时跑一次完整抓取循环（`/arm_run?on=1`），观察 `/status` 的 `heap_free` 曲线 30 分钟内应保持平稳（无持续下降 = 无泄漏）；若 `video_frame` 打开，同时观察检测帧率（`ai_get_last().infer_ms`）没有因 WS 发送阻塞而显著变慢。
4. 全部通过后，把 `chargereborn-dashboard/` 一并纳入固件仓库的构建产物清单（或按你们的部署方式说明放哪里），更新 `README.md` 提一句"网页监控终端見 `chargereborn-dashboard/`"。

## 5. 不要碰的边界

- **不要改 `components/armctrl`/`components/armlink`/`components/ai` 内部实现**——所有需要的钩子已经在 Task 10 就位；如果发现真的需要新字段（如上面提到的电池类别透传），单独找我或在 PR 里说明，不要绕开钩子直接 include 内部头文件掏私有状态。
- **`armcal` 的 NVS 命名空间和 `armctrl` 的 `stats` 命名空间都不要碰**——WebSocket 层需要新的持久化（比如按天分桶的碳减排曲线）请开一个新的 NVS 命名空间，不要复用/覆盖已有的。
- 急停路径**只能**通过 `armctrl_estop()`，不要在 `net_ws.c` 里自己拼 UART 帧发送——那样会绕开 `s_estop` 锁存机制，导致"网页急停了但固件状态没同步"的不一致。
```

- [ ] **Step 2: Commit**

```bash
git add docs/ai/DASHBOARD_INTEGRATION.md
git commit -m "docs(ai): dashboard对接计划书,交队友实现WebSocket层

固件侧钩子(Task 10)与chargereborn-dashboard/PROTOCOL.md六类消息的
映射表+已知缺口(电池类别未透传)+验证步骤+不可碰边界(勿绕开
armctrl_estop自己拼UART帧)。DEFENSE_GUIDE.md的ESP-NOW话术纠正待队友
一并改。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 12: 文档终态化（ARM_PIPELINE.md 重写 + README 核对）

**Files:**
- Modify: `docs/ai/ARM_PIPELINE.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: 无（纯文档，反映 Task 1-10 的最终代码行为）
- Produces: 无代码产出

- [ ] **Step 1: `docs/ai/ARM_PIPELINE.md` §2（安全模型）重写——去 G 级语义，改为运行/急停语义**

将 `## 2. 安全模型` 整节（从 `### 2.1 四重联锁` 到 `### 2.3 失败回退：绝不在刀口旁松爪` 结束，即到 §3 标定流程之前）替换为：

```markdown
## 2. 安全模型

### 2.1 三重联锁（全绿才动手）

`armctrl_task` 主循环执行任何运动前，三个条件缺一不可：

1. **编译期** `CONFIG_ARMLINK_UART_ENABLE` —— 未开则所有 `armctrl_move_*` 是空操作（`ESP_ERR_INVALID_STATE`），固件不驱动任何舵机。
2. **运行时 run**（`/arm_run?on=&cont=`，`s_run`，默认 **off**）—— 抓取循环总开关；单轮模式每轮结束自动 `s_run=false`，连续模式（`cont=1`）持续循环直到手动停止/急停/连续 3 次未获取到稳定目标。
3. **IK 自检 + 标定 valid** —— 启动时 `kin_selftest()` 对拍内嵌 golden PWM，失败 `s_ik_ok=false` 禁用自动模式；`armcal_load` 无有效标定则 `s_cal.valid=false`，未标定时任何 `run` 请求直接被拒。运动原语层（`armctrl_move_arm`/`armctrl_move_servo`）额外做"未标定绝不发字节"的最后一道兜底，即使联锁被绕过也不会发出真实坐标。

### 2.2 急停

- **软件急停**：`/arm_estop?on=1` → `armctrl_estop()` 立即发送 `$DST:0!`（唯一真机验证过的急停串，见 `docs/ai/CRASH_SIGNATURES.md` 2026-07-02）→ 停止循环 → 锁存 `s_estop`；锁存期间任何 `run` 请求都被拒绝，需 `/arm_estop?on=0` 显式清除才能恢复。
- **物理急停**：断动力电（首选，任何时候都可用，不依赖固件状态）。
- 每个运动原语（`armctrl_move_arm`/`armctrl_move_servo`）内部都检查 `s_estop`，急停触发后即使当前序列还在执行中，下一个原语调用也会立即短路不发送。

### 2.3 失败回退：绝不在刀口旁松爪

- **切割失败** → **保持夹持撤离**：先尽力撤到 `blade_safe_z`（返回值有意忽略，撤离优先），再 `go_observe_ex(false)` **不开爪** 回观察位，半切开的电池夹着等人工取回。
- **抓取失败** → `go_observe()` 正常开爪回观察位、`s_run=false`。
- 复位默认安全位：腕中位 #004=1500、开爪 #005=开、抬到 `carry_z`。
```

- [ ] **Step 2: `docs/ai/ARM_PIPELINE.md` §6（HTTP 端点速查）更新**

将：
```markdown
## 6. HTTP 端点速查（SoftAP `192.168.4.1`）

| 端点 | 方法 | 作用 | 返回 |
|---|---|---|---|
| `/detect` | GET | 读最近检测缓存（不触发推理，handler 轻） | `{w,h,infer_ms,n,boxes[...]}` |
| `/arm_target` | GET | 读最近机械臂目标缓存（选中的最佳电池位姿） | 目标 JSON |
| `/arm_calib` | GET | 查当前 H + 观察位 + valid | `{valid,H[9],observe[3]}` |
| `/arm_calib` | POST | body=`H0,...,H8` 九浮点，写 NVS 置 valid，并触发 armctrl 空闲重载（免重启） | `{saved:bool}` |
| `/arm_grade` | GET | `?g=0..4` 设安全级；无参查询 | `{grade:N}` |
| `/arm_run` | GET | `?on=1|0` 启停一轮抓取循环 | `{running:bool}` |
| `/arm_test` | GET | 手动发裸腕舵机 #004 测试序列（中位→摆→回中，含 ~1.4s 阻塞） | `{sent:bool,err}` |
| `/arm_auto` | GET | `?on=1|0` 自动发送意向位；无参查询 | `{auto_send:bool}` |

操作页（`/` root）已加 **G 级下拉 + 抓取启动/停止** 按钮，直连 `/arm_grade`、`/arm_run`。
```
改为：
```markdown
## 6. HTTP 端点速查（SoftAP `192.168.4.1`）

| 端点 | 方法 | 作用 | 返回 |
|---|---|---|---|
| `/detect` | GET | 读最近检测缓存（不触发推理，handler 轻）；`boxes[]` 只含 ≥0.40 高分框（弱框只喂跟踪器不上叠加） | `{w,h,infer_ms,n,boxes[...]}` |
| `/arm_target` | GET | 读跟踪器滤波后的机械臂目标缓存 | `{valid,stable,coasting,cx,cy,angle_deg,score,w,h,frame_id}` |
| `/arm_calib` | GET | 查当前 H + 观察位 + valid | `{valid,H[9],observe[3]}` |
| `/arm_calib` | POST | body=`H0,...,H8` 九浮点，写 NVS 置 valid，并触发 armctrl 空闲重载（免重启） | `{saved:bool}` |
| `/arm_run` | GET | `?on=1|0&cont=1|0` 启停抓取循环；`cont=1` 完整循环后不停,继续下一轮 | `{running:bool,cont:bool}` |
| `/arm_estop` | GET | `?on=1` 立即急停并锁存(发 `$DST:0!`)；`?on=0` 清除锁存 | `{estopped:bool}` |

操作页（`/` root）有**连续模式勾选 + 抓取启动/停止 + 急停**按钮，直连 `/arm_run`、`/arm_estop`。
```

- [ ] **Step 3: `docs/ai/ARM_PIPELINE.md` 顶部摘要行去 G 级措辞**

将文首（第 1-5 行附近）出现的 "G0-G4"/"安全分级" 等描述性措辞，改为与当前实现一致的"运行/连续/急停"措辞（逐处按上下文通顺改写，不追求逐字对应，保持文档其余部分——标定手册 §3、已知坑清单 §5——不动，那些是历史事实记录）。

- [ ] **Step 4: `README.md` 核对**

Read `README.md`，确认"已完成能力"一节里关于 armlink/机械臂的描述（当前写的是"默认关闭、不驱动实物"）仍准确——若已上板验证过抓取/切割且默认演示形态已经是"可运行"而非"默认关闭"，相应改一句反映真实状态（具体措辞视 Task 13 上板验证结果而定，若时间不够可保守地不改，不确定就不动）。

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "docs(ai): ARM_PIPELINE.md去G级语义,改为运行/连续/急停终态描述

安全模型章节从四重联锁(G0-G4分级)重写为三重联锁+急停(\$DST:0!);
HTTP端点速查表同步终态(去arm_grade/arm_test/arm_auto,加arm_estop+
arm_run的cont参数)。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 13: 上板验证（build 已绿后的实测清单；人工在场，不自动 flash）

**这是硬件操作任务，不适用 subagent 全自主执行**——`flash` 必须当场确认（`docs/ai/SAFETY.md`/`.claude/rules/safety.md` 铁律），且部分步骤要求物理观察（卷尺、目视急停响应等）。本任务的产出是**跑通并记录结果**，不是新代码。

**Files:**
- Modify: `docs/ai/ARM_PIPELINE.md`(§4 待实测参数表，补充跟踪器实标值)
- Modify: `docs/ai/CRASH_SIGNATURES.md`(若发现新坑)

**Interfaces:**
- Consumes: Task 1-10 的全部代码改动（已 build 绿）。
- Produces: 上板验证记录（写入文档），供 Task 14 分区收缩前确认功能已跑通。

- [ ] **Step 1: 静止场景实标 R（跟踪器测量噪声方差）**

用 `mcp__idf-bridge__flash`（**当场确认**）烧录当前固件；`mcp__idf-bridge__monitor_start` 起串口记录；桌面摆一颗电池静止不动，跑 ≥100 帧检测（可用网页 `/detect` 轮询或直接看串口 `main: detect:` 日志），记录 `cx,cy,w,h` 序列，算样本方差。若算出的 `σ_cx²`/`σ_cy²` 与 `target_track.c` 里的 `TRACK_R_POS=4.0f`（σ≈2px）明显不符（例如差 2 倍以上），据实测值调整该常量并重新 build+flash。

- [ ] **Step 2: 重放 2026-07-06 晚失败场景**

`/arm_run?on=1`（**当场确认，人在场**）；观察 `go_observe()` 完成后，`acquire_pose` 是否**一次性**进入 STABLE（串口应看到 `pose ok(stable)` 而不是反复的 `位姿不稳,重试`）。这是本轮最关键的回归点——Task 4/7 的根治点必须在真机上复现验证，不能只信 host 测试。

- [ ] **Step 3: 真电池单轮验证（不切割，先验证抓取-放回）**

若 §5.2 的完整流程（含切割）暂不具备条件，可临时用 `armctrl_move_arm` 层面手动验证抓取-抬起-放回（不修改代码，只是观察正常流程走到 `pick_sequence` 成功为止）；目标：≥8/10 成功抓起。记录成功率。

- [ ] **Step 4: 装刀完整单轮（含切割）**

条件具备时执行完整 `/arm_run?on=1`（不勾连续），验证 `cut_sequence` 成功、`place_back` 成功、回观察位。

- [ ] **Step 5: 连续模式 + 急停实测**

`/arm_run?on=1&cont=1`（**当场确认，人在场，手不离电源开关**），放 2-3 颗电池；验证：①放回原位的电池不会被立刻重抓（防重抓排除表生效）；②循环中途点击"急停"按钮，观察臂是否立即停止动作、串口出现 `急停!` 日志。若 `$DST:0!` 实测效果与 `CRASH_SIGNATURES.md` 记录不符（比如臂没有真的停），立即物理断电，并把急停降级为"仅锁存状态机+提示断电"的话术，記录到 `CRASH_SIGNATURES.md`。

- [ ] **Step 6: 记录结果**

把 Step 1-5 的实测值（`TRACK_R_POS`/`TRACK_R_WH` 最终采用值、各轮成功率、急停实测结论）追加进 `docs/ai/ARM_PIPELINE.md` §4 待实测参数表对应行；新发现的坑按 `/learn` 格式追加进 `docs/ai/CRASH_SIGNATURES.md`。

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "docs(ai): 上板验证记录——跟踪器实标R值+重放场景+抓取/切割/连续/急停实测

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 14: 分区表收缩（最后一步）+ 全片重烧 + boot/标定回读校验 + tag

**这是最后一个任务，必须在 Task 1-13 全部完成、build 绿、上板功能验证通过之后才执行**——改分区表要求全片重烧（含分区表本身），风险最高，放最后一次性做完。

**Files:**
- Modify: `partitions.csv`

**Interfaces:**
- Consumes: 全部前序任务
- Produces: 无（终态收尾）

- [ ] **Step 1: 收缩分区表**

将：
```
# S3-Forge 自定义分区表（含 coredump 取证 + esp-dl 模型分区）
# factory 8MB: 为 YOLOv8n 级模型(~3.2MB) rodata 内嵌 + 双模型 A/B 共存腾量(2026-07-04, docs/ai/DECISIONS.md)
# Name,     Type, SubType,  Offset,  Size,    Flags
nvs,        data, nvs,      ,        0x6000,
phy_init,   data, phy,      ,        0x1000,
factory,    app,  factory,  ,        0x800000,
espdet_det, data, spiffs,   ,        0x300000,
coredump,   data, coredump, ,        0x10000,
```
改为：
```
# S3-Forge 自定义分区表（含 coredump 取证；4MB factory，2026-07-07 定稿收缩）
# 收缩理由: battery_yolo/battery_detect已删除(Task 1),4类detect4模型走rodata内嵌
# 从未用过espdet_det这个3MB spiffs分区;当前app实测~3.2MB,4MB留够余量。
# NVS/phy_init位置与大小不变(H标定数据存活),仅factory缩小+删espdet_det+coredump顺移。
# Name,     Type, SubType,  Offset,  Size,    Flags
nvs,        data, nvs,      ,        0x6000,
phy_init,   data, phy,      ,        0x1000,
factory,    app,  factory,  ,        0x400000,
coredump,   data, coredump, ,        0x10000,
```

- [ ] **Step 2: build 校验**

调用 `mcp__idf-bridge__build`。
Expected: `ok:true, rc:0`；`mcp__idf-bridge__size` 确认 app 实际大小仍在 4MB 以内（预期 ~3.2MB，留有余量）。

- [ ] **Step 3: 记录标定回读基准值（重烧前）**

烧录**前**，先用现有固件（分区表还是旧的）访问 `GET /arm_calib`，把返回的 `H[9]` 完整记录下来（作为"重烧后应该完全一致"的基准）。

- [ ] **Step 4: 全片重烧（当场确认）**

调用 `mcp__idf-bridge__flash`（**当场确认，人在场**）。分区表变更要求重新烧录分区表本身，这是全片操作，不是增量 app-only 烧录。

- [ ] **Step 5: boot check**

`mcp__idf-bridge__monitor_start` 起串口监视，确认正常启动（无 boot loop、`bsp_print_sysinfo`/`bsp_psram_selftest`/`IK 自检通过` 等预期日志正常出现）。

- [ ] **Step 6: NVS 标定回读校验**

访问 `GET /arm_calib`，确认 `valid:true` 且 `H[9]` 与 Step 3 记录的基准值完全一致（逐个浮点数比对，允许 JSON 序列化的显示精度差异，但不应有实质性数值差异）。**这是本次分区收缩最关键的验证点**——证明 NVS 数据在分区表变更后完好存活。若不一致，立即停止，不要继续，回退到旧 `partitions.csv` 重新烧录以恢复标定数据，然后再排查原因。

- [ ] **Step 7: Commit + tag**

```bash
git add -A
git commit -m "chore(partitions): 分区表收缩8MB->4MB,删未用espdet_det分区(定稿最后一步)

battery_yolo/battery_detect已删(Task 1),从未使用过的3MB espdet_det
spiffs分区一并删除;NVS/phy_init位置不变,H标定数据经boot+回读校验
存活。这是提交前的最后一次全片重烧。

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git tag -a submission-final -m "正式提交定稿: 冗余清除+卡尔曼目标跟踪+状态机终态化"
```

---

## 收尾自查清单（全部任务完成后过一遍）

- [ ] `grep -rn "s_grade\|G0-dry\|KMS\|battery_yolo\|ESPDET_PICO_224_224_BATTERY\b\|armlink_send_test_frame\|auto_send" components/ main/` 零匹配（Task 1-3 删除项零残留）。
- [ ] `env PATH="/d/anaconda/Library/mingw-w64/bin:/usr/bin:/bin" gcc components/armlink/test/test_track.c components/armlink/target_track.c -Icomponents/armlink/include -lm -o /tmp/tt && /tmp/tt` 输出 `ALL PASS`（跟踪器 host 测试仍绿，未被后续任务意外破坏）。
- [ ] 既有 host 测试（`test_kinematics.c`/`test_frame.c`/`test_homography.c`）仍能编译通过（未被本轮改动波及，抽查一遍即可）。
- [ ] `mcp__idf-bridge__build` 最终绿，`mcp__idf-bridge__size` 记录最终镜像大小。
- [ ] `docs/ai/ARM_PIPELINE.md`/`docs/ai/SAFETY.md`/`README.md` 与代码终态一致，无 G 级/`$KMS`/`$DST!`（裸）等过时措辞残留。
- [ ] `docs/ai/DASHBOARD_INTEGRATION.md` 已交付。
- [ ] Task 13 的上板验证记录已写入文档；急停实测结论明确（`$DST:0!` 真机有效，或已降级为软停话术并记录）。
- [ ] 分区收缩后 `GET /arm_calib` 的 `H[9]` 与收缩前基准值一致。
