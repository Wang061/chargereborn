# Brain 固件地基(Track A)实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在新 ESP32-S3-WROOM-1-N16R8 板上,用组件化骨架替换 hello_world,跑通 PSRAM 运行时自检 + WiFi softAP + 最小 HTTP server——为边缘 AI 视觉主轴打地基(不需摄像头)。

**Architecture:** 在 `WORKplace` 工程内增量长出 Brain:`main/` 只做编排,硬件细节进 `components/bsp`,网络进 `components/net`。每个任务产出可独立 build/boot 验证的成果。

**Tech Stack:** ESP-IDF 5.5.4 · ESP32-S3 · esp_psram · esp_wifi(softAP)· esp_http_server · led_strip(可选)

配套 spec:`docs/superpowers/specs/2026-06-13-chargereborn-brain-phase1-vision-spine-design.md`

---

## 前置约定(每个任务都适用)

- **构建/烧录/监视只走 idf-bridge**:`mcp__idf-bridge__build` / `mcp__idf-bridge__flash`(需当场确认)/ `mcp__idf-bridge__monitor_start` + `monitor_read`。不裸跑 idf.py。
- **ESP API 以 build 为准**:下方代码是结构与意图;落地若 build 报 API 不符,用 `espressif-documentation` MCP 核对官方签名后 `esp-build-fix` 最小修正(项目铁律:不凭记忆定稿 ESP 代码)。
- **"测试"在嵌入式的含义**:改代码 → `idf-bridge build` 绿;改启动路径/运行行为 → flash + monitor 看到预期日志(无 boot loop)。每个任务以 monitor 实测为准,不把"看起来对"当通过。
- **版本锁**:IDF 5.5.4 / target esp32s3,全程不改。
- **flash 节流**:每个任务必 build 绿 + commit;flash+monitor 在有可观测行为的任务末尾做(Task 1/2/4 是硬件验证检查点;Task 3 可与 Task 4 合并烧一次)。

## 文件结构(本计划新建/修改)

```
WORKplace/
├── CMakeLists.txt                    # 修改:project(hello_world) → project(chargereborn_brain)
├── main/
│   ├── CMakeLists.txt                # 修改:注册 main.c,REQUIRES bsp net
│   ├── main.c                        # 新建(替换 hello_world_main.c):编排 + 心跳
│   └── hello_world_main.c            # 删除
└── components/
    ├── bsp/
    │   ├── CMakeLists.txt            # 新建
    │   ├── include/bsp.h             # 新建:sysinfo / psram 自检 /(可选)LED
    │   └── bsp.c                     # 新建
    └── net/
        ├── CMakeLists.txt            # 新建
        ├── include/net.h             # 新建:net_softap_start / net_http_start
        ├── wifi_ap.c                 # 新建:softAP
        └── http_srv.c                # 新建:HTTP server
```

---

## Task 1: 用组件化骨架替换 hello_world(最小可启动)

产出:删掉 hello_world,起 `bsp` 组件(启动横幅 sysinfo)+ `main` 心跳循环,板子能正常 boot 并每秒打印存活。

**Files:**
- Create: `components/bsp/include/bsp.h`
- Create: `components/bsp/bsp.c`
- Create: `components/bsp/CMakeLists.txt`
- Create: `main/main.c`
- Modify: `main/CMakeLists.txt`
- Modify: `CMakeLists.txt`(根)
- Delete: `main/hello_world_main.c`

- [ ] **Step 1: 建 `components/bsp/include/bsp.h`**

```c
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

// 启动横幅:打印芯片 / flash / 堆信息
void bsp_print_sysinfo(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 建 `components/bsp/bsp.c`**

```c
#include "bsp.h"
#include <inttypes.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

static const char *TAG = "bsp";

void bsp_print_sysinfo(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "==== ChargeReborn Brain boot ====");
    ESP_LOGI(TAG, "chip=%s cores=%d rev=v%d.%d",
             CONFIG_IDF_TARGET, info.cores,
             info.revision / 100, info.revision % 100);
    ESP_LOGI(TAG, "flash=%" PRIu32 "MB", flash_size / (1024U * 1024U));
    ESP_LOGI(TAG, "heap(internal) free=%" PRIu32 "B",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
```

- [ ] **Step 3: 建 `components/bsp/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spi_flash)
```

- [ ] **Step 4: 建 `main/main.c`**

```c
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "bsp.h"

static const char *TAG = "main";

void app_main(void)
{
    bsp_print_sysinfo();

    uint32_t sec = 0;
    while (1) {
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 5: 改 `main/CMakeLists.txt` 为**

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES bsp)
```

- [ ] **Step 6: 改根 `CMakeLists.txt` 的工程名**

把第 7 行 `project(hello_world)` 改成:

```cmake
project(chargereborn_brain)
```

- [ ] **Step 7: 删除 `main/hello_world_main.c`**

```bash
git rm main/hello_world_main.c
```

- [ ] **Step 8: 构建**

Run: `mcp__idf-bridge__build`
Expected: `ok=true`(绿)。若报工程名缓存相关错 → `idf.py fullclean`(需当场确认)后重 build。

- [ ] **Step 9: 烧录 + 监视(需你确认 flash)**

Run: `mcp__idf-bridge__flash` → 确认后烧录;再 `mcp__idf-bridge__monitor_start` + `mcp__idf-bridge__monitor_read`
Expected(无 boot loop,持续每秒一行):
```
I (xxx) bsp: ==== ChargeReborn Brain boot ====
I (xxx) bsp: chip=esp32s3 cores=2 rev=v0.x
I (xxx) bsp: flash=16MB
I (xxx) bsp: heap(internal) free=xxxxxxB
I (xxx) main: alive 0s heap=xxxxxxB
I (xxx) main: alive 1s heap=xxxxxxB
```

- [ ] **Step 10: 提交**

```bash
git add CMakeLists.txt main/CMakeLists.txt main/main.c components/bsp
git commit -m "feat(brain): 组件化骨架替换 hello_world(bsp sysinfo + 心跳)"
```

---

## Task 2: PSRAM 运行时自检

产出:`bsp` 增加 PSRAM 自检(确认 Octal 8MB 真能 init + 能分配 + 读写正确)——相机帧与 AI 张量都靠它。

**Files:**
- Modify: `components/bsp/include/bsp.h`
- Modify: `components/bsp/bsp.c`
- Modify: `components/bsp/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 在 `components/bsp/include/bsp.h` 的 `bsp_print_sysinfo` 声明后加**

```c
// PSRAM 运行时自检:已初始化 + 1MB 分配 + 写读回一致 → 返回 true
bool bsp_psram_selftest(void);
```

- [ ] **Step 2: 在 `components/bsp/bsp.c` 顶部 include 区加**

```c
#include "esp_psram.h"
```

- [ ] **Step 3: 在 `components/bsp/bsp.c` 末尾追加函数**

```c
bool bsp_psram_selftest(void)
{
    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "PSRAM NOT initialized! check SPIRAM/OCT in sdkconfig");
        return false;
    }
    size_t total = esp_psram_get_size();
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM total=%uB (%uMB) free=%uB",
             (unsigned)total, (unsigned)(total / (1024U * 1024U)), (unsigned)psram_free);

    const size_t test_sz = 1024U * 1024U;   // 1MB
    uint8_t *buf = (uint8_t *)heap_caps_malloc(test_sz, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "PSRAM malloc %uB failed", (unsigned)test_sz);
        return false;
    }
    for (size_t i = 0; i < test_sz; i += 4096) buf[i] = (uint8_t)(i & 0xFF);
    bool ok = true;
    for (size_t i = 0; i < test_sz; i += 4096) {
        if (buf[i] != (uint8_t)(i & 0xFF)) { ok = false; break; }
    }
    heap_caps_free(buf);
    ESP_LOGI(TAG, "PSRAM alloc+rw test: %s", ok ? "OK" : "FAIL");
    return ok;
}
```

- [ ] **Step 4: 改 `components/bsp/CMakeLists.txt` 的 PRIV_REQUIRES 加 esp_psram**

```cmake
idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spi_flash esp_psram)
```

- [ ] **Step 5: 在 `main/main.c` 的 `bsp_print_sysinfo();` 后加一行**

```c
    bsp_psram_selftest();
```

- [ ] **Step 6: 构建**

Run: `mcp__idf-bridge__build`
Expected: `ok=true`(绿)。

- [ ] **Step 7: 烧录 + 监视(需你确认 flash)**

Run: `mcp__idf-bridge__flash` → `mcp__idf-bridge__monitor_start` + `monitor_read`
Expected(total 约 8388608B = 8MB):
```
I (xxx) bsp: PSRAM total=8388608B (8MB) free=xxxxxxxB
I (xxx) bsp: PSRAM alloc+rw test: OK
```
若 `PSRAM NOT initialized` 或 `FAIL` → 停下转 `sdkconfig-change-review` / `esp-monitor-triage` 排查(优先怀疑 OCT 模式/时钟),不要带病往下走。

- [ ] **Step 8: 提交**

```bash
git add components/bsp main/main.c
git commit -m "feat(brain): PSRAM 运行时自检(Octal 8MB alloc+rw)"
```

---

## Task 3: WiFi softAP

产出:`net` 组件起 softAP(SSID `chargereborn`),手机能搜到并连上——为后续 HTTP 图传/采数据铺路。

**Files:**
- Create: `components/net/include/net.h`
- Create: `components/net/wifi_ap.c`
- Create: `components/net/CMakeLists.txt`
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: 建 `components/net/include/net.h`**

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void net_softap_start(void);   // 启动 WiFi softAP
void net_http_start(void);     // 启动 HTTP server(Task 4 实现)

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 建 `components/net/wifi_ap.c`**

```c
#include "net.h"
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_ap";

#define AP_SSID      "chargereborn"
#define AP_PASS      "12345678"
#define AP_CHANNEL   1
#define AP_MAX_CONN  4

static void ap_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "station " MACSTR " joined aid=%d", MAC2STR(e->mac), e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "station " MACSTR " left aid=%d", MAC2STR(e->mac), e->aid);
    }
}

void net_softap_start(void)
{
    esp_err_t ret = nvs_flash_init();          // WiFi 校准数据需 NVS
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL, NULL));

    wifi_config_t wc = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "softAP up SSID=%s pass=%s ch=%d -> http://192.168.4.1/",
             AP_SSID, AP_PASS, AP_CHANNEL);
}
```

- [ ] **Step 3: 建 `components/net/CMakeLists.txt`**

```cmake
idf_component_register(SRCS "wifi_ap.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash)
```

- [ ] **Step 4: 改 `main/CMakeLists.txt` 的 REQUIRES 加 net**

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES bsp net)
```

- [ ] **Step 5: 在 `main/main.c` 加 include 与调用**

文件顶部 include 区加:
```c
#include "net.h"
```
在 `bsp_psram_selftest();` 之后加:
```c
    net_softap_start();
```

- [ ] **Step 6: 构建**

Run: `mcp__idf-bridge__build`
Expected: `ok=true`(绿)。可与 Task 4 合并后一起烧录验证以省 flash 次数。

- [ ] **Step 7: 提交**

```bash
git add components/net main/main.c main/CMakeLists.txt
git commit -m "feat(brain): WiFi softAP(chargereborn)"
```

---

## Task 4: 最小 HTTP server(占位端点 + /status)

产出:softAP 上起 HTTP server,手机连上后浏览器能看到存活页 + `/status` 返回 JSON(uptime/堆/PSRAM)——这是后续 MJPEG 图传的骨架。

**Files:**
- Create: `components/net/http_srv.c`
- Modify: `components/net/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 建 `components/net/http_srv.c`**

```c
#include "net.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"

static const char *TAG = "http_srv";

static esp_err_t root_get(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>ChargeReborn Brain</title></head><body>"
        "<h1>ChargeReborn Brain alive</h1>"
        "<p>edge-AI vision spine - phase 1</p>"
        "<p><a href=\"/status\">/status</a></p></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, html);
}

static esp_err_t status_get(httpd_req_t *req)
{
    char buf[160];
    int n = snprintf(buf, sizeof(buf),
        "{\"uptime_s\":%" PRIu32 ",\"heap_free\":%" PRIu32 ",\"psram_free\":%u}",
        (uint32_t)(esp_log_timestamp() / 1000U),
        (uint32_t)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

void net_http_start(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }
    httpd_uri_t root   = { .uri = "/",       .method = HTTP_GET, .handler = root_get };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &status);
    ESP_LOGI(TAG, "http server up -> http://192.168.4.1/");
}
```

- [ ] **Step 2: 改 `components/net/CMakeLists.txt` 加源文件与依赖**

```cmake
idf_component_register(SRCS "wifi_ap.c" "http_srv.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server)
```

- [ ] **Step 3: 在 `main/main.c` 的 `net_softap_start();` 后加**

```c
    net_http_start();
```

- [ ] **Step 4: 构建**

Run: `mcp__idf-bridge__build`
Expected: `ok=true`(绿)。

- [ ] **Step 5: 烧录 + 监视 + 手机实测(需你确认 flash)**

Run: `mcp__idf-bridge__flash` → `mcp__idf-bridge__monitor_start` + `monitor_read`
Expected monitor:
```
I (xxx) wifi_ap: softAP up SSID=chargereborn pass=12345678 ch=1 -> http://192.168.4.1/
I (xxx) http_srv: http server up -> http://192.168.4.1/
```
手机实测:连 WiFi `chargereborn`(密码 12345678)→ 浏览器开 `http://192.168.4.1/` 见存活页;`http://192.168.4.1/status` 返回 JSON(连接时 monitor 打印 `station ... joined`)。

- [ ] **Step 6: 提交**

```bash
git add components/net main/main.c
git commit -m "feat(brain): 最小 HTTP server(/ 存活页 + /status JSON)"
```

---

## Task 5(可选): RGB 状态灯心跳

产出:点亮 DevKitC-1 板载 WS2812,心跳里翻转颜色——给现场/演示一个直观"活着"信号。**需联网拉 led_strip 托管组件。**

**Files:**
- Create: `components/bsp/idf_component.yml`
- Modify: `components/bsp/include/bsp.h`
- Modify: `components/bsp/bsp.c`
- Modify: `components/bsp/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: 建 `components/bsp/idf_component.yml`**

```yaml
dependencies:
  espressif/led_strip: "^3.0.0"
```

- [ ] **Step 2: 在 `components/bsp/include/bsp.h` 加声明**

```c
#include <stdint.h>
void bsp_led_init(void);
void bsp_led_set(uint8_t r, uint8_t g, uint8_t b);
```

- [ ] **Step 3: 在 `components/bsp/bsp.c` 加 include + 实现**

include 区加:
```c
#include "led_strip.h"
```
文件末尾加:
```c
// DevKitC-1 板载 WS2812 在 GPIO48;部分批次为 GPIO38,不亮就改这里
#define BSP_RGB_GPIO 48
static led_strip_handle_t s_led;

void bsp_led_init(void)
{
    led_strip_config_t sc = { .strip_gpio_num = BSP_RGB_GPIO, .max_leds = 1 };
    led_strip_rmt_config_t rc = { .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&sc, &rc, &s_led) != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed");
        s_led = NULL;
        return;
    }
    led_strip_clear(s_led);
}

void bsp_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led) return;
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
}
```

- [ ] **Step 4: 改 `components/bsp/CMakeLists.txt` 的 PRIV_REQUIRES 加 led_strip**

```cmake
idf_component_register(SRCS "bsp.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES spi_flash esp_psram led_strip)
```

- [ ] **Step 5: 改 `main/main.c`:init + 心跳翻转颜色**

在 `bsp_psram_selftest();` 后加:
```c
    bsp_led_init();
```
把心跳 `while(1)` 循环体改为:
```c
        ESP_LOGI(TAG, "alive %" PRIu32 "s heap=%" PRIu32 "B",
                 sec, (uint32_t)esp_get_free_heap_size());
        bsp_led_set((sec % 2) ? 8 : 0, (sec % 2) ? 0 : 8, 0);  // 红/绿交替(低亮度)
        sec++;
        vTaskDelay(pdMS_TO_TICKS(1000));
```

- [ ] **Step 6: 构建**

Run: `mcp__idf-bridge__build`
Expected: `ok=true`(绿;首次会拉取 led_strip 托管组件,需联网)。

- [ ] **Step 7: 烧录 + 目视(需你确认 flash)**

Run: `mcp__idf-bridge__flash`,然后看板子或用 `/esp-snap` 拍照。
Expected: 板载 RGB 灯每秒红/绿交替。不亮 → 把 `BSP_RGB_GPIO` 改成 38 重试(批次差异)。

- [ ] **Step 8: 提交**

```bash
git add components/bsp main/main.c
git commit -m "feat(brain): RGB 状态灯心跳(WS2812)"
```

---

## 自检(对照 spec)

- **spec §5 Track A 第1步(骨架/sysinfo/心跳)** → Task 1 ✅
- **spec §5 Track A 第2步(PSRAM 运行时实测)** → Task 2 ✅
- **spec §5 Track A 第3步(WiFi softAP + HTTP)** → Task 3 + Task 4 ✅
- **spec §5 Track A "状态 LED 心跳"** → Task 5(可选)✅
- **spec §11 默认(本周 1-3 + WiFi softAP)** → 覆盖;`partitions.csv` 的 model 分区与 camera/ai 实体代码按 spec 归属 AI 收敛期,不在本计划 ✅
- **无占位符**:各步均含完整代码/命令/预期输出 ✅
- **类型/签名一致**:`bsp_print_sysinfo` / `bsp_psram_selftest` / `bsp_led_init` / `bsp_led_set` / `net_softap_start` / `net_http_start` 在 header 与调用处一致 ✅

## 不在本计划(后续)

- Track B(AI 链路提前排雷:Python 环境 + 官方 cat 例程跑通到 S3)—— 独立子系统,单独成计划。
- 摄像头到货后:camera 组件 + 图传采数据 + 电池模型训练/部署(spec §6)。
- `partitions.csv` factory 扩容 + model 分区(AI app 比 1MB 大时)——AI 收敛期处理,走 `/esp-partition` 校验。
