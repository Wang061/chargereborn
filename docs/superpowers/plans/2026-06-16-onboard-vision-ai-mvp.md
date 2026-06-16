# 板上视觉 AI MVP（单类·先猫占位）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 ESP32-S3（Brain）上把"相机帧 → esp-dl 推理 → 识别框/类别/置信度"整条板上回路跑通；先用已验证的猫 `.espdl` 当占位证明回路，电池数据训好后只换模型 + 类名表即成 MVP。**本刀不接机械臂。**

**Architecture:** 新增 `components/ai`(C++ 包 esp-dl，对外暴露 `extern "C"` 的 `ai_*` API)。复用已有 `components/camera`(JPEG 直出)——AI 路 = `camera_capture()(JPEG) → dl::image::sw_decode_jpeg → img_t(RGB888) → ESPDetDetect::run() → std::list<result_t>`，**对现有图传零改动**。结果经新增 `/detect`(JSON) 暴露；采集页加 `<canvas>` 轮询叠框。模型用 esp-dl 的 `esptool_py_flash_to_partition` 烧进独立 `espdet_det` 分区——换模型只重烧该分区、不重编 app。

**Tech Stack:** ESP-IDF 5.5.4 / esp32s3；esp-dl `^3.1.3`（含 `dl::Model` / `dl::image` / `ESPDetPostProcessor`）；cat_detect `0.1.1`（ESPDet-Pico 224×224 占位，`ESPDetDetect` 类）；esp32-camera `^2.1.0`（JPEG VGA，已上板）。

**配套 spec：** 上位 `docs/superpowers/specs/2026-06-13-chargereborn-brain-phase1-vision-spine-design.md`（§6 收敛链）。本刀为该 spine 的"板上推理"增量，不另写 spec。

---

## 已核实事实（写计划前已查证，勿凭记忆推翻）

- **推理 API**（来自 `ai/esp-detection/esp-dl/examples/cat_detect/main/app_main.cpp`，已上板验证）：
  ```cpp
  dl::image::jpeg_img_t jpeg = {.data=(void*)buf, .data_len=len};
  dl::image::img_t img = dl::image::sw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
  ESPDetDetect *det = new ESPDetDetect();           // lazy_load 默认 true，首次 run 才载模型
  std::list<dl::detect::result_t> &r = det->run(img);// res.category(int) res.score(float) res.box[0..3](x1,y1,x2,y2)
  heap_caps_free(img.data);                          // sw_decode_jpeg 的输出必须手动释放
  ```
- **S3 预处理**：`ImagePreprocessor(model,{0,0,0},{255,255,255}, DL_IMAGE_CAP_RGB565_BIG_ENDIAN)` + `enable_letterbox({114,114,114})`，**自动把任意尺寸输入 resize+letterbox 到模型 224×224** → 我们直接喂整帧 RGB888，不用自己缩放。
- **S3 时延**（官方实测）：224×224 ≈ preprocess 8.2ms + model 123.4ms + post 1ms ≈ **~132ms / ~7.5 FPS**，单核忙。
- **模型入分区**（来自 `models/cat_detect/CMakeLists.txt`）：`CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_PARTITION` 下，`esptool_py_flash_to_partition(flash "espdet_det" <packed.espdl>)` + `add_dependencies(flash ...)` → `idf.py flash` 自动打包并烧模型到 **名为 `espdet_det` 的分区**。`ESPDet` 构造里 `path="espdet_det"` 对应。
- **现状**：`partitions.csv` factory 仅 `0x100000`(1MB) → 装不下 esp-dl + 模型，**必须扩**。相机 `components/camera` 直出 `PIXFORMAT_JPEG / FRAMESIZE_VGA`，正好是 `sw_decode_jpeg` 的输入。`sdkconfig.defaults` 已 `CONFIG_PARTITION_TABLE_CUSTOM=y / FILENAME="partitions.csv"`；`.esp32s3` 已 16MB flash + Octal PSRAM 80M。
- **依赖来源**：`ai/esp-detection/esp-dl/models/cat_detect/`（cat 组件，MIT）内部 `override_path: "../../esp-dl"` 指本地 esp-dl。本刀占位阶段**优先用 managed registry** 拉 `espressif/esp-dl`(==3.1.3) + `espressif/cat_detect`(==0.1.1)（自包含、可复现）；若 registry/网络受阻，回退到本地 `override_path` 指 `D:/WJ/jixiebi/ai/esp-detection/esp-dl/...`。两者 API 同（`ESPDetDetect`）。

---

## 风险与红线（按 .claude/rules）

- **改 `partitions.csv` = 危险改动** → 必 build + **flash + boot check**（看是否 boot loop、coredump 分区仍在）。flash **当场确认**（safety 铁律：`允许 AI flash = NO`）。
- **内存共存风险（本刀头号未知）**：esp-dl 推理 + WiFi + 2×httpd + 相机 fb 同时占内部 DRAM(S3 ~320KB)，可能 OOM。缓解阶梯：① esp-dl 静态规划器把激活放 PSRAM（已有 `CONFIG_SPIRAM`）；② 推理放独立低优先级任务；③ 必要时降 `CAM_FB_COUNT` 或合并 httpd。**M2/M3 的 boot 实测就是来撞这个的**——所以先用内置 jpg(M2) 把"esp-dl+分区+WiFi 共存"撞通，再加相机变量(M3)。
- **不碰**：机械臂/舵机/Steward/ESP-NOW（本刀范围外）；不动相机像素格式（保持 JPEG，零风险复用图传）；不升 IDF 主版本、不改 target。
- `*.espdl` 占位猫模型**不入 git**（先 gitignore；最终电池模型再定 commit / git-lfs）。

---

## File Structure

| 文件 | 责任 | 动作 |
|---|---|---|
| `partitions.csv` | 分区布局：扩 factory→3MB + 加 `espdet_det` 模型分区 3MB | **Modify** |
| `components/ai/include/ai.h` | 对外纯 C API（`ai_init`/`ai_detect_jpeg`/`ai_detect_oneshot`/`ai_class_name`/类型） | Create |
| `components/ai/ai.cpp` | C++ 实现：`extern "C"` 包 `ESPDetDetect`，JPEG→decode→run→填结果，互斥保护 | Create |
| `components/ai/espdet_detect.hpp/.cpp` | ESPDet 模型包装（从 cat 组件原样取，MIT） | Create（vendored，占位阶段可由 managed dep 代替） |
| `components/ai/Kconfig` | 模型位置/选择（partition + 224_224_cat） | Create |
| `components/ai/CMakeLists.txt` | 注册 ai 源 + 复用 esp-dl 的模型打包/烧分区机制 | Create |
| `components/ai/idf_component.yml` | 依赖 esp-dl(==3.1.3) [+ cat_detect 或 vendored 模型] | Create |
| `components/ai/models/s3/espdet_pico_224_224_cat.espdl` | 占位模型（从 ai/ 拷入；gitignore） | Create |
| `components/net/http_srv.c` | 加 `/detect`(JSON)；root 页加 `<canvas>` 轮询叠框 | Modify |
| `components/net/CMakeLists.txt` | net 增依赖 `ai` | Modify |
| `main/main.c` | `ai_init()`；起低优先级 detect 任务周期日志 | Modify |
| `main/CMakeLists.txt` | REQUIRES 加 `ai` | Modify |
| `.gitignore` | 加 `*.espdl` | Modify |

---

## Task 1（M1）：扩分区 + 空 ai 组件骨架（build 绿 → flash+boot 校验）

**目的**：先把"分区改动 + 新组件挂载"这步独立验证，不引入 esp-dl 复杂度。

**Files:**
- Modify: `partitions.csv`
- Create: `components/ai/include/ai.h`, `components/ai/ai.cpp`, `components/ai/CMakeLists.txt`
- Modify: `main/main.c`, `main/CMakeLists.txt`

- [ ] **Step 1.1：改 `partitions.csv`**（factory 1MB→3MB，新增 3MB 模型分区，保留 coredump）

```csv
# S3-Forge 自定义分区表（含 coredump 取证 + esp-dl 模型分区）
# Name,     Type, SubType,  Offset,  Size,    Flags
nvs,        data, nvs,      ,        0x6000,
phy_init,   data, phy,      ,        0x1000,
factory,    app,  factory,  ,        0x300000,
espdet_det, data, spiffs,   ,        0x300000,
coredump,   data, coredump, ,        0x10000,
```
> 说明：空 offset 由 IDF 自动算（nvs@0x9000→factory 落 0x10000，64KB 对齐合法）。总占用 ~6.4MB / 16MB，余量留给后续 OTA/数据集。模型分区 SubType 用 `spiffs`（仅占位类型，esp-dl 按 label `espdet_det` 直接 mmap 读，不挂文件系统）。

- [ ] **Step 1.2：建 `components/ai/include/ai.h`**（纯 C API，先全是 stub 能编）

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

#define AI_MAX_BOXES 10

typedef struct {
    int   cls;            // 类别 id
    float score;          // 置信度 0..1
    int   x1, y1, x2, y2; // 框（模型输入坐标系，左上/右下，像素）
} ai_box_t;

typedef struct {
    int       count;                 // 命中框数（<=AI_MAX_BOXES）
    ai_box_t  boxes[AI_MAX_BOXES];
    uint32_t  infer_ms;              // 本次推理耗时(ms)，含 decode+model+post
    int       src_w, src_h;          // 推理所用源帧尺寸（供 /detect 坐标换算）
} ai_result_t;

// 构造检测器（lazy：首次 detect 才真正载模型）。重复调用安全。
esp_err_t ai_init(void);
// 对一帧 JPEG 推理，结果写 out。线程安全（内部互斥）。
esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out);
// 便捷：抓一帧相机(JPEG)→detect→归还帧。线程安全。
esp_err_t ai_detect_oneshot(ai_result_t *out);
// 类别 id → 人类可读名（占位："cat"；换电池模型时改这张表）。
const char *ai_class_name(int cls);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 1.3：建 `components/ai/ai.cpp`**（M1 先 stub，能编能链）

```cpp
#include "ai.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ai";

extern "C" esp_err_t ai_init(void) {
    ESP_LOGW(TAG, "ai_init: stub (M1 骨架，未接 esp-dl)");
    return ESP_OK;
}
extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out) {
    (void)jpg; (void)len;
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;   // M2 实现
}
extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
}
extern "C" const char *ai_class_name(int cls) {
    return (cls == 0) ? "cat" : "obj";   // 占位
}
```

- [ ] **Step 1.4：建 `components/ai/CMakeLists.txt`**（M1 仅注册源，暂不挂 esp-dl）

```cmake
idf_component_register(
    SRCS "ai.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera          # ai_detect_oneshot 会用 camera_capture
)
```

- [ ] **Step 1.5：`main/main.c` 调 `ai_init()`**（在 net 之后、心跳之前）

在 `#include "camera.h"` 后加 `#include "ai.h"`；在 `net_stream_start();` 后加：
```c
    if (ai_init() != ESP_OK) {
        ESP_LOGW(TAG, "ai init failed — 推理不可用，继续运行");
    }
```

- [ ] **Step 1.6：`main/CMakeLists.txt` 的 `REQUIRES` 加 `ai`**（与 bsp net camera 并列）

- [ ] **Step 1.7：build（绿）**

Run: `mcp__idf-bridge__build_start` 然后 `build_read` 轮询（首次会因改分区/加组件全量重编，用非阻塞）。
Expected: `ok/rc=0`；无 `region overflow`/`component not found`。

- [ ] **Step 1.8：flash + boot check（⚠️ 当场确认）**

改了分区，必须实测启动。确认后 `mcp__idf-bridge__flash`（COM7）→ `monitor_start`（COM11）→ `monitor_read`。
Expected: 正常启动、`camera up`、`http server up`、`ai_init: stub`、`alive Ns` 心跳，**无 boot loop**；`coredump` 分区仍在（`idf.py partition-table` 可核）。
> 若 boot loop / 分区报错 → 回退 partitions.csv，定位后再试（不要带病往下走）。

- [ ] **Step 1.9：commit**

```bash
git add partitions.csv components/ai main/main.c main/CMakeLists.txt
git commit -m "feat(ai): 扩分区(factory 3MB+espdet_det 3MB)+ai组件骨架(stub)"
```

---

## Task 2（M2）：接 esp-dl + 猫模型，内置 jpg 推理（撞通"esp-dl+分区+WiFi 共存"）

**目的**：用**已知正确的输入**（内置一张 jpg）先把 esp-dl 在我们板子/分区/WiFi 共存下跑通，排除相机变量。这是最大未知的拆弹步。

**Files:**
- Create: `components/ai/espdet_detect.hpp`, `components/ai/espdet_detect.cpp`, `components/ai/Kconfig`, `components/ai/idf_component.yml`, `components/ai/models/s3/espdet_pico_224_224_cat.espdl`
- Modify: `components/ai/CMakeLists.txt`, `components/ai/ai.cpp`, `main/main.c`, `.gitignore`

- [ ] **Step 2.1：拿到 esp-dl + 猫模型依赖**（二选一，优先 A）

**A（优先，自包含）** `components/ai/idf_component.yml`：
```yaml
dependencies:
  espressif/esp-dl: "==3.1.3"
  espressif/cat_detect: "==0.1.1"
```
用 managed 组件时，`ESPDetDetect` 由 cat_detect 提供；**此时 Step 2.2 的 vendored 文件与 models/ 可省略**（模型分区烧录由 cat_detect 的 CMake 自动挂到 `flash`）。

**B（回退，本地 override）** 若网络/registry 受阻：
```yaml
dependencies:
  espressif/esp-dl:
    version: "==3.1.3"
    override_path: "D:/WJ/jixiebi/ai/esp-detection/esp-dl/esp-dl"
  espressif/cat_detect:
    version: "==0.1.1"
    override_path: "D:/WJ/jixiebi/ai/esp-detection/esp-dl/models/cat_detect"
```
> build 前先清代理（见 `mem:dev-flow-build-flash-monitor`）：`$env:ALL_PROXY='';$env:HTTP_PROXY='';$env:HTTPS_PROXY='';$env:all_proxy='';$env:http_proxy='';$env:https_proxy=''`。idf-bridge 的 `_clean_env` 已自动清。

> **vendored 兜底（A/B 都失败时）**：把 `models/cat_detect/{espdet_detect.hpp,.cpp,Kconfig}` 拷进 `components/ai/`，`models/s3/espdet_pico_224_224_cat.espdl` 拷进 `components/ai/models/s3/`，并把 cat 的 CMake 模型打包段并入本组件 CMakeLists（见 Step 2.4）。实现者按实际 build 结果择路，**以 build 绿为准**。

- [ ] **Step 2.2：选 A 时**——确认 cat_detect 暴露 `ESPDetDetect`（读 `managed_components/espressif__cat_detect/espdet_detect.hpp` 核对类名/枚举 `ESPDET_PICO_224_224_CAT`）。**选 B/vendored 时**——`components/ai/espdet_detect.{hpp,cpp}` 内容与 `ai/esp-detection/esp-dl/models/cat_detect/espdet_detect.{hpp,cpp}` 一致（MIT，原样）。

- [ ] **Step 2.3：模型位置配置**（sdkconfig.defaults 追加；让模型进 `espdet_det` 分区）

`sdkconfig.defaults` 末尾追加：
```
# ---- esp-dl ESPDet 模型：放独立 flash 分区(换模型只重烧分区) ----
CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_PARTITION=y
CONFIG_FLASH_ESPDET_PICO_224_224_CAT=y
CONFIG_ESPDET_PICO_224_224_CAT=y
```
> 这些符号来自 cat_detect 的 Kconfig；准确名以该组件 Kconfig 为准（实现者 build 时若报未知符号，读 `managed_components/espressif__cat_detect/Kconfig` 校正）。模型分区 label 必须与 CMake 的 `esptool_py_flash_to_partition(flash "espdet_det" ...)` 一致——已在 partitions.csv 命名 `espdet_det`。

- [ ] **Step 2.4：CMakeLists 接入**（选 A：仅加 REQUIRES；选 vendored：并入打包段）

选 A，`components/ai/CMakeLists.txt`：
```cmake
idf_component_register(
    SRCS "ai.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera esp-dl cat_detect
)
```

- [ ] **Step 2.5：内置一张测试 jpg**（证明链路；用 cat 的 espdet.jpg）

把 `ai/esp-detection/esp-dl/examples/cat_detect/main/espdet.jpg` 拷到 `components/ai/test_cat.jpg`；CMakeLists 加 `EMBED_FILES "test_cat.jpg"`。在 `ai.cpp` 暴露一个 M2 自检函数：
```cpp
#include "espdet_detect.hpp"
#include "dl_image_jpeg.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "camera.h"

extern const uint8_t test_cat_start[] asm("_binary_test_cat_jpg_start");
extern const uint8_t test_cat_end[]   asm("_binary_test_cat_jpg_end");

static ESPDetDetect *s_det = nullptr;
static SemaphoreHandle_t s_lock = nullptr;

extern "C" esp_err_t ai_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_det)  s_det  = new ESPDetDetect();   // lazy_load=true：此处不载，首 run 才载
    ESP_LOGI(TAG, "ai_init ok (esp-dl ESPDet, lazy)");
    return s_det ? ESP_OK : ESP_ERR_NO_MEM;
}

// 内部：对已 decode 的 img_t 跑模型，填 out（调用方持锁）
static void run_img(dl::image::img_t &img, ai_result_t *out) {
    int64_t t0 = esp_timer_get_time();
    auto &results = s_det->run(img);
    int64_t t1 = esp_timer_get_time();
    memset(out, 0, sizeof(*out));
    out->src_w = img.width; out->src_h = img.height;
    out->infer_ms = (uint32_t)((t1 - t0) / 1000);
    for (auto &r : results) {
        if (out->count >= AI_MAX_BOXES) break;
        ai_box_t *b = &out->boxes[out->count++];
        b->cls = r.category; b->score = r.score;
        b->x1 = r.box[0]; b->y1 = r.box[1]; b->x2 = r.box[2]; b->y2 = r.box[3];
    }
}

extern "C" esp_err_t ai_detect_jpeg(const uint8_t *jpg, size_t len, ai_result_t *out) {
    if (!s_det || !out) return ESP_ERR_INVALID_STATE;
    dl::image::jpeg_img_t j = {.data=(void*)jpg, .data_len=len};
    dl::image::img_t img = dl::image::sw_decode_jpeg(j, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
    if (!img.data) return ESP_FAIL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    run_img(img, out);
    xSemaphoreGive(s_lock);
    heap_caps_free(img.data);
    return ESP_OK;
}

// M2 自检：对内置 jpg 推理（无相机）
extern "C" esp_err_t ai_selftest_builtin(ai_result_t *out) {
    return ai_detect_jpeg(test_cat_start, (size_t)(test_cat_end - test_cat_start), out);
}
```
（`ai_selftest_builtin` 暂在 `ai.h` 加声明，M3 接相机后可删。）

- [ ] **Step 2.6：main 里 M2 自检**——`ai_init()` 后调一次 `ai_selftest_builtin(&r)` 并日志 `count/score/box/infer_ms`。

- [ ] **Step 2.7：`.gitignore` 加 `*.espdl`**（占位猫模型不入库）。

- [ ] **Step 2.8：build（绿）**——`build_start`+`build_read`。Expected rc=0；注意 esp-dl 拉取/编译耗时较长。

- [ ] **Step 2.9：flash + boot（⚠️确认）+ monitor**

Expected（COM11）：
```
I (xxxx) ai: ai_init ok (esp-dl ESPDet, lazy)
I (xxxx) ai: selftest: count=1 cls=0(cat) score=0.88 box=[..] infer=~130ms
```
**这一步绿 = esp-dl + 3MB 分区 + WiFi/httpd 共存通过**（最大未知拆弹成功）。若 OOM/`mem allocation failed` → 按"内存共存阶梯"调（激活入 PSRAM / 降 fb_count / 合并 httpd），重 build。

- [ ] **Step 2.10：commit** `feat(ai): 接 esp-dl+猫模型, 内置jpg板上推理通过(~130ms)`

---

## Task 3（M3）：相机帧 → 推理 回路

**Files:** Modify `components/ai/ai.cpp`（实现 `ai_detect_oneshot`）、`main/main.c`（detect 任务）

- [ ] **Step 3.1：实现 `ai_detect_oneshot`**（相机 JPEG 直喂，复用 2.5 的 `ai_detect_jpeg`）

```cpp
extern "C" esp_err_t ai_detect_oneshot(ai_result_t *out) {
    camera_fb_t *fb = camera_capture();          // JPEG VGA
    if (!fb) return ESP_FAIL;
    esp_err_t e = ai_detect_jpeg(fb->buf, fb->len, out);
    camera_return(fb);                            // 与 capture 配对
    return e;
}
```

- [ ] **Step 3.2：main 起 detect 任务**（低优先级、栈给足、周期推理 + 日志）

```c
static void detect_task(void *arg) {
    ai_result_t r;
    while (1) {
        if (ai_detect_oneshot(&r) == ESP_OK) {
            ESP_LOGI(TAG, "detect: n=%d infer=%ums", r.count, r.infer_ms);
            for (int i = 0; i < r.count; i++)
                ESP_LOGI(TAG, "  #%d %s %.2f [%d,%d,%d,%d]", i,
                         ai_class_name(r.boxes[i].cls), r.boxes[i].score,
                         r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2);
        }
        vTaskDelay(pdMS_TO_TICKS(500));          // ~2Hz，留 CPU 给 WiFi/图传
    }
}
```
在 `ai_init()` 成功后：`xTaskCreate(detect_task, "detect", 8192, NULL, 3, NULL);`
> 栈 8192：esp-dl run 调用栈较深 + 局部 result 列表；优先级 3：低于 WiFi(任务默认~23?)与图传 httpd，避免抢占网络。理由随代码注释。

- [ ] **Step 3.3：build（绿）→ flash（确认）→ monitor**

把相机对准**手机上一张猫图**。Expected：`detect: n=1 infer~130ms` + 框坐标随猫移动变化。证明 **相机→推理 闭环成立**。
> 若一直 n=0：默认 `score_thr=0.25`；可临时降阈值/换更清晰猫图确认（占位模型只认猫，认不出电池是正常的）。

- [ ] **Step 3.4：commit** `feat(ai): 相机帧→推理 回路打通(~2Hz, 板上实时)`

---

## Task 4（M4）：/detect 端点 + 浏览器叠框（demo 可视化 + 数据自检工具）

**Files:** Modify `components/net/http_srv.c`、`components/net/CMakeLists.txt`

- [ ] **Step 4.1：net 依赖加 `ai`**——`components/net/CMakeLists.txt` 的 `PRIV_REQUIRES` 加 `ai`。

- [ ] **Step 4.2：`/detect` JSON 处理器**（http_srv.c，仿现有 `capture_get`）

```c
#include "ai.h"
static esp_err_t detect_get(httpd_req_t *req) {
    ai_result_t r;
    if (ai_detect_oneshot(&r) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "detect failed");
        return ESP_FAIL;
    }
    char buf[640];
    int n = snprintf(buf, sizeof(buf),
        "{\"w\":%d,\"h\":%d,\"infer_ms\":%u,\"n\":%d,\"boxes\":[",
        r.src_w, r.src_h, r.infer_ms, r.count);
    for (int i = 0; i < r.count && n < (int)sizeof(buf) - 64; i++) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"cls\":%d,\"name\":\"%s\",\"s\":%.2f,\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}",
            i ? "," : "", r.boxes[i].cls, ai_class_name(r.boxes[i].cls), r.boxes[i].score,
            r.boxes[i].x1, r.boxes[i].y1, r.boxes[i].x2, r.boxes[i].y2);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```
注册：`httpd_uri_t detect = {.uri="/detect",.method=HTTP_GET,.handler=detect_get}; httpd_register_uri_handler(server, &detect);`

- [ ] **Step 4.3：root 页加 `<canvas>` 叠框**（在现有 `<img id=v>` 上覆盖一层 canvas，JS 轮询 `/detect` 画框，按 src_w/h 缩放到显示尺寸）。保持现有采集/连拍不动，仅追加一段叠框 JS（单引号 HTML，沿用现风格）。

- [ ] **Step 4.4：build（绿）→ flash（确认）→ 浏览器实测**

连 `chargereborn` → `http://192.168.4.1/`：实时画面上**猫被框出 + 类名/置信度**。这就是本刀的可视化交付物 + 后续数据自检工具。

- [ ] **Step 4.5：commit** `feat(net): /detect 端点 + 浏览器实时叠框`

---

## Task 5（M5，延后·数据就绪后）：换单类电池模型 → MVP 识别成立

> 依赖用户用连拍攒的单类电池数据集（按 `mem:brain-vision-progress` 并行任务）。本刀**不执行**，仅记录路径。

- [ ] 用 `ai/esp-detection/espdet_run.py` 训练单类（如 `battery`）ESPDet-Pico 224，导出 `<battery>.espdl` + 部署包（含定制 `espdet_detect.{cpp,hpp}` + Kconfig）。
- [ ] 用部署包替换 `components/ai` 的 vendored 模型文件；`models/s3/<battery>.espdl` 放入；改 `ai_class_name` 表为电池类名；Kconfig 选新模型。
- [ ] **只重烧模型分区**（`idf.py flash` 会自动重打包烧 `espdet_det`，app 不必全重编）→ boot + 浏览器实测电池被框出。
- [ ] commit + `/learn` 沉淀；更新 `mem:brain-vision-progress`。

---

## Self-Review（写完计划回看 spec）

- **Spec 覆盖**：vision-spine §6"板上推理"增量 → M1-M4 覆盖（取帧已在前刀；缩放由 ImagePreprocessor 内建；推理=esp-dl；结果=/detect+日志）。✅
- **类型一致**：`ai_result_t`/`ai_box_t` 在 ai.h 定义，main/net/ai.cpp 一致使用；`ESPDetDetect::run` 返回 `std::list<result_t>{category,score,box[4]}` 已核对。✅
- **占位符**：无 TBD；每步给了真实代码或明确"拷自 <路径>"。esp-dl 内部不复述（vendored/managed，MIT）。✅
- **风险闭环**：分区改动→flash+boot 校验步在 M1/M2；内存共存→M2 内置 jpg 先撞、附缓解阶梯；flash 全程当场确认。✅
- **可逆**：每 M 独立 commit + build 绿 + （改硬件行为的）flash 实测；坏味即停回退。✅
```

