# 相机点亮 + 数据集采集工具 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Brain 板 ESP32-S3-WROOM-1-N16R8 上用排针+杜邦线点亮相机(OV2640 先行),浏览器实时 MJPEG 取景 + 一键抓拍下载 JPEG,当天即可开始拍电池攒数据集。

**Architecture:** 新增 `components/camera/`(封装 `espressif/esp32-camera` 托管组件,JPEG 直出住 PSRAM,SCCB 自动探测传感器);扩 `components/net/` 加单张 `/capture`(80 口,attachment 下载)与 `/stream`(独立 81 口 MJPEG,避免占满 80 口 worker);`main.c` 仅编排。驱动不碰 HTTP,net 从 camera 拉帧。

**Tech Stack:** ESP-IDF 5.5.4 · ESP32-S3 · espressif/esp32-camera(托管组件)· esp_http_server(双实例 80/81)· PSRAM 帧缓冲

配套 spec:`docs/superpowers/specs/2026-06-16-camera-bringup-dataset-capture-design.md`

---

## 前置约定(每个任务都适用)

- **构建/烧录/监视**:本机 `idf-bridge` build/monitor 曾挂死(持久记忆 `dev-flow-build-flash-monitor`)。**build 走** `powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build`(首次拉 esp32-camera 托管组件需联网+几分钟→ `run_in_background`;增量前台)。**flash 走** `$env:ESPPORT='COM7'; powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 flash`(当场确认;BOARD.md 允许 AI flash = NO)。**monitor 走** .NET SerialPort 抓 **COM11**(115200,RTS 复位抓启动日志)。
- **ESP API 以 build 为准**:下方代码是结构与意图。落地若 build 报 esp32-camera API 不符(最可能:SCCB 字段名 `pin_sccb_sda`↔旧版 `pin_sscb_sda`;组件版本号),读真实头 `managed_components/espressif__esp32-camera/driver/include/esp_camera.h` 或查 `espressif-documentation` MCP 后 `esp-build-fix` 最小修正(铁律:不凭记忆定稿 ESP 代码)。
- **嵌入式"测试"的含义**:改代码 → build 绿;改启动/运行行为 → flash + monitor 看到预期日志(无 boot loop)。硬件相关步骤标注「需相机接好」,软件 build-绿 是随时可做的硬门槛。
- **接线契约**:`components/camera/include/camera.h` 的 `CAM_PIN_*` 宏即接线表,**物理杜邦线必须与其一一对应**。改线就改宏,唯一硬约束是避开禁用脚(见 Task 1)。
- **版本锁**:IDF 5.5.4 / target esp32s3,全程不改。
- **flash 节流**:Task 1/2/3 各是一个硬件检查点(需接好相机后烧一次);软件多任务可先连续 build 绿、攒到接好相机再逐个 flash 验。

## 文件结构(本计划新建/修改)

```
WORKplace/
├── main/
│   ├── main.c                       # 修改:init 加 cam_init()(T1)、net_stream_start()(T3)
│   └── CMakeLists.txt               # 修改:REQUIRES 加 camera(T1)
├── components/
│   ├── camera/                      # 新增组件
│   │   ├── idf_component.yml        # 新建:依赖 espressif/esp32-camera
│   │   ├── include/camera.h         # 新建:接线表宏 + cam_init/cam_capture/cam_return
│   │   ├── camera.c                 # 新建:esp_camera_init + 取帧/归还封装
│   │   └── CMakeLists.txt           # 新建
│   └── net/
│       ├── include/net.h            # 修改:加 net_stream_start 声明(T3)
│       ├── http_srv.c              # 修改:加 /capture(T2)、采集页 root(T3)
│       ├── stream_srv.c            # 新建:81 口 MJPEG 流(T3)
│       └── CMakeLists.txt           # 修改:PRIV_REQUIRES 加 camera(T2)、SRCS 加 stream_srv.c(T3)
└── docs/ai/BOARD.md                 # 修改:落地后记录锁定的相机接线表(T4)
```

---

## Task 0: 基线确认(build 绿)

产出:确认当前 Track A 工程在本机能 build 绿,作为往上长相机的干净地基。

- [ ] **Step 1: 基线构建**

Run(后台,首次可能拉缓存):`powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build`
Expected: 末尾 `Project build complete.` / 生成 `build/chargereborn_brain.bin`,无 error。
若红:先修地基再继续(不在本计划往上叠)。

---

## Task 1: camera 组件 + 锁定接线表 + 拉 esp32-camera(取帧打通)

产出:新增 `camera` 组件,`cam_init()` 跑通 esp32-camera、自动探测传感器并打印 PID;`main` 启动时初始化相机(失败不崩、打印诊断便于查接线)。

**Files:**
- Create: `components/camera/include/camera.h`
- Create: `components/camera/camera.c`
- Create: `components/camera/idf_component.yml`
- Create: `components/camera/CMakeLists.txt`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 建 `components/camera/include/camera.h`**

```c
#pragma once
#include "esp_err.h"
#include "esp_camera.h"   // camera_fb_t / pixformat_t（托管组件 espressif/esp32-camera）
#ifdef __cplusplus
extern "C" {
#endif

// ===== 相机接线表（排针 → DevKitC GPIO）。物理杜邦线必须与此表一一对应。=====
// 安全性：均落在 GPIO4..18，避开 Octal PSRAM 专用 35/36/37、USB-JTAG 19/20、
//         SPI flash/PSRAM 段 26..37、strapping 0/3/45/46。
// PWDN/RESET 不接（-1，用 SCCB 软复位）。飞线不稳：先降 CAM_XCLK_HZ→10MHz，再降分辨率。
#define CAM_PIN_PWDN    (-1)
#define CAM_PIN_RESET   (-1)
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD     4   // SCCB SDA
#define CAM_PIN_SIOC     5   // SCCB SCL
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK    13
#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      11
#define CAM_PIN_D2      10
#define CAM_PIN_D1       9
#define CAM_PIN_D0       8

#define CAM_XCLK_HZ     16000000   // 16MHz：启用 ESP32-S3 EDMA 模式、对飞线更友好；不稳降 10000000

// 初始化相机（DVP+SCCB，自动探测传感器）。成功 ESP_OK；失败返回错误码并打印诊断，不 abort。
esp_err_t cam_init(void);
// 取一帧（JPEG），失败返回 NULL。用完必须 cam_return（配对，杜绝帧缓冲泄漏）。
camera_fb_t *cam_capture(void);
// 归还帧缓冲。
void cam_return(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 建 `components/camera/camera.c`**

```c
#include "camera.h"
#include "esp_log.h"

static const char *TAG = "camera";

esp_err_t cam_init(void)
{
    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,   // 旧版字段名 pin_sscb_sda；build 报错就改这两行
        .pin_sccb_scl = CAM_PIN_SIOC,   // 旧版字段名 pin_sscb_scl
        .pin_d7 = CAM_PIN_D7, .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5, .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3, .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1, .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href  = CAM_PIN_HREF,
        .pin_pclk  = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_HZ,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,    // 直出 JPEG，省 RAM、可直接传
        .frame_size   = FRAMESIZE_VGA,     // 640x480 采集；OV5640 可上更高
        .jpeg_quality = 12,                // 0-63，越小越清
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM, // 帧缓冲住 PSRAM（已实测 8MB 可用）
        .grab_mode    = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x (%s) — 核对接线表/供电/共地",
                 err, esp_err_to_name(err));
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // 用官方枚举宏判别(sensor.h，经 esp_camera.h 引入):OV2640_PID=0x26、OV5640_PID=0x5640
        const char *name = (s->id.PID == OV2640_PID) ? "OV2640"
                         : (s->id.PID == OV5640_PID) ? "OV5640" : "other";
        ESP_LOGI(TAG, "camera up: sensor PID=0x%04x (%s), JPEG VGA", s->id.PID, name);
    }
    return ESP_OK;
}

camera_fb_t *cam_capture(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) ESP_LOGW(TAG, "esp_camera_fb_get returned NULL");
    return fb;
}

void cam_return(camera_fb_t *fb)
{
    if (fb) esp_camera_fb_return(fb);
}
```

- [ ] **Step 3: 建 `components/camera/idf_component.yml`**

```yaml
dependencies:
  espressif/esp32-camera: "^2.1.0"   # 解析到当前 2.1.7(依赖 IDF>=5.1, 兼容 5.5);避免回退到早期 2.0.x
```
> 官方核对(2026-06-16):latest=2.1.7,支持所有 target(含 esp32s3),依赖 ESP-IDF>=5.1。若依赖解析异常,可钉死 `"2.1.7"`。

- [ ] **Step 4: 建 `components/camera/CMakeLists.txt`**

```cmake
# REQUIRES（非 PRIV）：camera.h 公开 #include "esp_camera.h"，需让 net/main 也拿到该头
idf_component_register(SRCS "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32-camera)
```

- [ ] **Step 5: 改 `main/main.c`** —— 顶部 include 区加 `#include "camera.h"`,并在 `bsp_psram_selftest();` 之后、`net_softap_start();` 之前加:

```c
    if (cam_init() != ESP_OK) {
        ESP_LOGW(TAG, "camera init failed — 图传不可用，继续运行便于查接线");
    }
```

- [ ] **Step 6: 改 `main/CMakeLists.txt` 的 REQUIRES 加 camera**

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES bsp net camera)
```

- [ ] **Step 7: 构建(首次拉 esp32-camera 托管组件,需联网)**

Run(后台):`powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build`
Expected: `managed_components/espressif__esp32-camera/` 被拉下;`Project build complete.` 无 error。
若 build 报 `pin_sccb_sda`/`pin_sccb_scl` 字段不存在 → 改成 `pin_sscb_sda`/`pin_sscb_scl` 重 build(见前置约定)。

- [ ] **Step 8: 烧录 + 监视(需相机接好;当场确认 flash)**

先按 camera.h 接线表用杜邦线接好 OV2640(VCC→3V3、GND→GND 共地、其余 14 脚一一对应),再:
Run: `$env:ESPPORT='COM7'; powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 flash`(确认后)→ 用 COM11 SerialPort 抓启动日志。
Expected(无 boot loop):
```
I (xxx) camera: camera up: sensor PID=0x0026 (OV2640), JPEG VGA
```
若 `esp_camera_init failed` / PID=0x00 → 八成接线/供电:核对接线表、检查共地、缩短飞线、必要时降 `CAM_XCLK_HZ`;转 `esp-monitor-triage` 排查,不带病往下。

- [ ] **Step 9: 提交**

```bash
git add components/camera main/main.c main/CMakeLists.txt
git commit -m "feat(brain): camera 组件点亮 esp32-camera(OV2640/OV5640 自动探测, JPEG VGA 住 PSRAM)"
```

---

## Task 2: `/capture` 单张抓拍下载(80 口)

产出:80 口 HTTP server 加 `/capture?name=<类名>` 端点,取一帧 JPEG 以 attachment 下发,浏览器存为 `<类名>_<ms>_<seq>.jpg`——用于攒数据集。

**Files:**
- Modify: `components/net/http_srv.c`
- Modify: `components/net/CMakeLists.txt`

- [ ] **Step 1: 改 `components/net/http_srv.c`** —— 顶部 include 区(在 `#include "esp_http_server.h"` 后)加:

```c
#include <string.h>
#include "camera.h"
```

- [ ] **Step 2: 在 `http_srv.c` 的 `status_get` 函数之后、`net_http_start` 之前,加 `capture_get`**

```c
static esp_err_t capture_get(httpd_req_t *req)
{
    // 解析 ?name=<类名>，只保留 [0-9A-Za-z_-]，缺省 "cap"
    char name[32] = "cap";
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1 && qlen < 128) {
        char q[128];
        if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
            char val[32];
            if (httpd_query_key_value(q, "name", val, sizeof(val)) == ESP_OK && val[0]) {
                size_t j = 0;
                for (size_t i = 0; val[i] && j < sizeof(name) - 1; i++) {
                    char c = val[i];
                    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') || c == '_' || c == '-') {
                        name[j++] = c;
                    }
                }
                name[j] = '\0';
                if (j == 0) strcpy(name, "cap");
            }
        }
    }

    camera_fb_t *fb = cam_capture();
    if (!fb) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }

    static uint32_t seq = 0;
    char disp[96];
    snprintf(disp, sizeof(disp),
             "attachment; filename=\"%s_%" PRIu32 "_%" PRIu32 ".jpg\"",
             name, esp_log_timestamp(), seq++);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    esp_err_t r = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    cam_return(fb);   // 与 cam_capture 配对
    return r;
}
```

- [ ] **Step 3: 在 `net_http_start` 内注册 `/capture`** —— 在 `httpd_register_uri_handler(server, &status);` 之后加:

```c
    httpd_uri_t capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_get };
    httpd_register_uri_handler(server, &capture);
```

- [ ] **Step 4: 改 `components/net/CMakeLists.txt` 的 PRIV_REQUIRES 加 camera**

```cmake
idf_component_register(SRCS "wifi_ap.c" "http_srv.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera)
```

- [ ] **Step 5: 构建**

Run: `powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build`
Expected: `Project build complete.` 无 error。

- [ ] **Step 6: 烧录 + 实测(需相机接好;当场确认 flash)**

Run: `$env:ESPPORT='COM7'; powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 flash`(确认)→ COM11 抓日志。
手机/PC 连 WiFi `chargereborn`(密码 `12345678`)→ 浏览器开 `http://192.168.4.1/capture?name=test`
Expected: 浏览器下载到一张名为 `test_<数字>_0.jpg` 的图,能打开、是相机当前画面。
若 500 / 下载为空 → 看 monitor 是否 `esp_camera_fb_get returned NULL`,回 Task 1 查取帧。

- [ ] **Step 7: 提交**

```bash
git add components/net/http_srv.c components/net/CMakeLists.txt
git commit -m "feat(brain): /capture 单张抓拍下载(类名命名, 采数据用)"
```

---

## Task 3: `/stream` 实时取景(81 口 MJPEG)+ 采集页

产出:独立 81 口 httpd 推 MJPEG 实时流(不占满 80 口 worker,抓拍可与取景并行);`/` 升级为采集页:实时画面 + 类名输入 + 抓拍按钮。

**Files:**
- Create: `components/net/stream_srv.c`
- Modify: `components/net/include/net.h`
- Modify: `components/net/http_srv.c`
- Modify: `components/net/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 建 `components/net/stream_srv.c`**

```c
#include "net.h"
#include "camera.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "stream_srv";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_get(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char part[64];
    while (true) {
        camera_fb_t *fb = cam_capture();
        if (!fb) { res = ESP_FAIL; break; }

        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            int hl = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
            res = (hl > 0 && hl < (int)sizeof(part))   // 防 snprintf 截断
                ? httpd_resp_send_chunk(req, part, hl) : ESP_FAIL;
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        cam_return(fb);     // 与 cam_capture 配对
        if (res != ESP_OK) break;   // 客户端断开 → 退出循环
    }
    ESP_LOGI(TAG, "stream client closed");
    return res;
}

void net_stream_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port   = 32769;     // 与 80 服务默认 ctrl_port(32768) 错开
    config.stack_size  = 8192;      // 流 handler + 相机,留余量防栈溢出
    config.lru_purge_enable = true; // 单观看者采集够用;多观看者需调大 max_open_sockets(LRU 可能踢掉活跃流)
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "stream httpd_start failed");
        return;
    }
    httpd_uri_t stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_get };
    httpd_register_uri_handler(server, &stream);
    ESP_LOGI(TAG, "stream server up -> http://192.168.4.1:81/stream");
}
```

- [ ] **Step 2: 改 `components/net/include/net.h`** —— 在 `void net_http_start(void);` 之后加:

```c
void net_stream_start(void);   // 启动 81 口 MJPEG 取景流
```

- [ ] **Step 3: 改 `components/net/http_srv.c` 的 `root_get`** —— 把 `html` 字符串整体替换为采集页:

```c
    const char *html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ChargeReborn 采集</title></head>"
        "<body style=\"font-family:sans-serif;text-align:center\">"
        "<h3>ChargeReborn 数据采集</h3>"
        "<div>类名 <input id=n value=\"18650\" size=10> "
        "<button onclick=\"location='/capture?name='+encodeURIComponent("
        "document.getElementById('n').value)\">抓拍</button></div>"
        "<p><img id=v style=\"max-width:96vw\"></p>"
        "<script>document.getElementById('v').src="
        "'http://'+location.hostname+':81/stream';</script>"
        "</body></html>";
```

- [ ] **Step 4: 改 `components/net/CMakeLists.txt` 的 SRCS 加 stream_srv.c**

```cmake
idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera)
```

- [ ] **Step 5: 改 `main/main.c`** —— 在 `net_http_start();` 之后加:

```c
    net_stream_start();
```

- [ ] **Step 6: 构建**

Run: `powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build`
Expected: `Project build complete.` 无 error。

- [ ] **Step 7: 烧录 + 实测(需相机接好;当场确认 flash)**

Run: `$env:ESPPORT='COM7'; powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 flash`(确认)→ COM11 抓日志。
Expected monitor:
```
I (xxx) http_srv: http server up -> http://192.168.4.1/
I (xxx) stream_srv: stream server up -> http://192.168.4.1:81/stream
```
手机/PC 连 `chargereborn` → 开 `http://192.168.4.1/`:见**实时画面**(对焦/构图)→ 填类名 → 点「抓拍」→ 下载一张图。
看 monitor:无 WDT/栈溢出/堆告警,`/stream` 持续推流不崩;断开浏览器见 `stream client closed`。
若画面卡/撕裂 → 降 `CAM_XCLK_HZ`→10MHz 或 `frame_size`→`FRAMESIZE_QVGA` 重烧。

- [ ] **Step 8: 提交**

```bash
git add components/net/stream_srv.c components/net/include/net.h components/net/http_srv.c components/net/CMakeLists.txt main/main.c
git commit -m "feat(brain): /stream 81口MJPEG实时取景 + 采集页(取景+类名+抓拍)"
```

---

## Task 4: 落地后沉淀(锁定接线表入档 + 经验)

产出:把**实测可用**的相机接线表写入 `docs/ai/BOARD.md`(下次换板/重接的唯一真相),并把 bring-up 踩到的坑沉淀。

**Files:**
- Modify: `docs/ai/BOARD.md`

- [ ] **Step 1: 在 `docs/ai/BOARD.md` 末尾追加「相机接线表(实测)」小节** —— 用 Task 1 `camera.h` 里**实测点亮成功**的 `CAM_PIN_*` 值填下表(若 bring-up 时改过引脚,以改后为准):

```markdown
## 相机接线表(OV2640/OV5640 DVP，排针→DevKitC，2026-06-16 实测)
| 相机信号 | GPIO | 相机信号 | GPIO |
|---|---|---|---|
| XCLK | 15 | D7 | 16 |
| PCLK | 13 | D6 | 17 |
| VSYNC | 6 | D5 | 18 |
| HREF | 7 | D4 | 12 |
| SIOD(SDA) | 4 | D3 | 11 |
| SIOC(SCL) | 5 | D2 | 10 |
| PWDN | NC(-1) | D1 | 9 |
| RESET | NC(-1) | D0 | 8 |
| VCC | 3V3 | GND | GND(共地) |
> XCLK 16MHz(启用 S3 EDMA);飞线不稳降 10MHz。真相以 `components/camera/include/camera.h` 的 `CAM_PIN_*` 为准。
```

- [ ] **Step 2: 提交**

```bash
git add docs/ai/BOARD.md
git commit -m "docs(board): 记录相机 DVP 接线表(实测点亮)"
```

- [ ] **Step 3:(可选)`/learn` 沉淀** —— 若 bring-up 遇到并解决了崩溃/接线坑(如 brownout、SCCB 字段名、栈溢出),用 `/learn` 写入 `docs/ai/CRASH_SIGNATURES.md`。

---

## 自检(对照 spec)

- **spec §2「做」camera 组件取帧** → Task 1 ✅
- **spec §2「做」/capture** → Task 2 ✅;**/stream** → Task 3 ✅
- **spec §3 契约**(cam_init/cam_capture/cam_return;net 从 camera 拉帧;main 仅编排;cam_init 失败不崩)→ Task 1-3 ✅
- **spec §4 OV2640 先行 / 同固件两颗通吃**(SCCB 自动探测 + 打印 PID;格式/分辨率与传感器无关)→ Task 1 ✅
- **spec §5 接线表 + 避禁用脚** → Task 1 `camera.h` + Task 4 入档 ✅
- **spec §6 采集 UX**(实时取景 + 类名 + 抓拍下载 + VGA)→ Task 2/3 ✅
- **spec §7 softAP 默认** → 复用既有 `net_softap_start`,不改 ✅
- **spec §8 错误处理**(不 abort;PSRAM/fb_count2/LATEST/JPEG;capture 配对 return;brownout 提示)→ Task 1-3 ✅
- **spec §9 验证**(各任务 build 绿 + flash + monitor + 手机实测)→ 各 Task Step ✅
- **无占位符**:各步均含完整代码/命令/预期输出 ✅
- **签名一致**:`cam_init`/`cam_capture`/`cam_return`/`net_stream_start` 在 header 与调用处一致;`/capture` `name` 参数与采集页一致 ✅

## 不在本计划(后续,各自单独处理)

- 板上 AI 推理(ESPDet-Pico)+ `.espdl` 部署、`partitions.csv` 扩 factory + model 分区(spec §6 后续环;AI app >1MB 时走 `/esp-partition`)。
- STA 模式、批量自动连拍、板载存图、OV5640 锁定为最终相机(spec §11)。
