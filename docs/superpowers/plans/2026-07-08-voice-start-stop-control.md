# 语音开始/停止接入机械臂 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 语音模块喊"开始处理"/"停止"能触发/停止 Brain 的连续抓取状态机，经 KM1 转发，不新建业务逻辑、只换触发入口。

**Architecture:** 语音模块(已配置好发 `#Start!`/`#Stop!`) → KM1 Serial1(RX1=GPIO18，独立于现有共享解析器) → KM1 Serial1(TX1=GPIO17，新转发) → Brain 新组件 `voicelink`(UART2, RX=GPIO2) → 调用现有 `armctrl_request_run()`。核心匹配逻辑是纯 C、无 ESP 依赖，Brain 侧和 KM1 侧各自实现同一套算法（互相独立，不共享代码，因为分属两个工具链）。

**Tech Stack:** ESP-IDF 5.5.4（Brain, C）+ Arduino/arduino-cli（KM1, `steward_km1.ino`）+ gcc host 单测（纯 C 匹配逻辑）。

## Global Constraints

- `IDF_VERSION=5.5.4`、`IDF_TARGET=esp32s3`（版本锁，不得违反）。
- 波特率 **115200**：KM1 `Serial1.begin(115200,...)`（已有，不改）、Brain `voicelink` UART2、语音模块自身 Serial1 配置，三方必须一致，否则乱码。
- `voicelink` 的 Kconfig **默认关闭**、RX GPIO **默认 -1 强制显式分配**（仿 `armlink` 既有安全模式）——因为一旦启用，语音信号可直接触发机械臂连续运动。
- 语音"开始"=连续抓取拓（等价 `armctrl_request_run(true, true)`）；语音"停止"=**缓停**（`armctrl_request_run(false, ...)`），**不是**急停；不新增语音急停能力，急停仍只走网页红键 + `$DST:0!`。
- KM1 侧改动只应用到 `D:\WJ\jixiebi\steward_km1\steward_km1.ino`；只读参考 `reference/esp32.ino`（`../4.源代码程序/` 同款拷贝）**禁止修改**。
- 任何 `flash` 操作必须当场向用户确认（项目铁律 + `.claude/hooks/guard.py` 拦截）。
- 只改与本任务直接相关的文件；不做无关重构。
- 前置 spec：`docs/superpowers/specs/2026-07-08-voice-start-stop-control-design.md`。

---

## Task 1: `voicelink_frame` 纯 C 帧匹配核心 + host 单测

**Files:**
- Create: `components/voicelink/include/voicelink_frame.h`
- Create: `components/voicelink/voicelink_frame.c`
- Create: `components/voicelink/test/test_voicelink_frame.c`

**Interfaces:**
- Produces（供 Task 2 使用）：
  - `typedef enum { VOICELINK_CMD_NONE = 0, VOICELINK_CMD_START, VOICELINK_CMD_STOP } voicelink_cmd_t;`
  - `typedef struct { char buf[16]; int len; } voicelink_frame_state_t;`
  - `void voicelink_frame_reset(voicelink_frame_state_t *st);`
  - `voicelink_cmd_t voicelink_frame_feed(voicelink_frame_state_t *st, char c);`

- [ ] **Step 1: 写头文件**

`components/voicelink/include/voicelink_frame.h`：

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 纯 C 语音帧匹配器：逐字节喂入，识别完整的 "#Start!" / "#Stop!" 帧。
// 无 ESP 依赖，可 host gcc 单测(同 kinematics/armlink_frame/target_track 的约定)。
// KM1 端(steward_km1.ino)用同一套算法的 Arduino C++ 版本转发,两边独立实现,不共享代码
// (分属两个工具链, 见 docs/superpowers/specs/2026-07-08-voice-start-stop-control-design.md §5)。

typedef enum {
    VOICELINK_CMD_NONE = 0,
    VOICELINK_CMD_START,
    VOICELINK_CMD_STOP,
} voicelink_cmd_t;

#define VOICELINK_FRAME_MAX 15   // "#Start!"=7 "#Stop!"=6，留够余量

typedef struct {
    char buf[VOICELINK_FRAME_MAX + 1];
    int  len;   // 0 = 尚未见到帧起始符 '#'
} voicelink_frame_state_t;

// 清零状态，等待下一帧。
void voicelink_frame_reset(voicelink_frame_state_t *st);

// 喂入一个字符。收到完整 "#Start!"/"#Stop!" 时返回对应命令并自动 reset；
// 否则返回 VOICELINK_CMD_NONE。遇到新的 '#' 会无条件重开一帧(丢弃半截帧,
// 噪声容错优先于漏检)。超长垃圾会在越界前放弃当前帧,不会写出界。
voicelink_cmd_t voicelink_frame_feed(voicelink_frame_state_t *st, char c);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写 host 单测（先写测试，此时实现还不存在，预期编译失败）**

`components/voicelink/test/test_voicelink_frame.c`：

```c
#include "voicelink_frame.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

static voicelink_cmd_t feed_str(voicelink_frame_state_t *st, const char *s) {
    voicelink_cmd_t last = VOICELINK_CMD_NONE;
    for (; *s; s++) {
        voicelink_cmd_t r = voicelink_frame_feed(st, *s);
        if (r != VOICELINK_CMD_NONE) last = r;
    }
    return last;
}

int main(void) {
    voicelink_frame_state_t st;

    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#Start!") == VOICELINK_CMD_START, "exact Start");

    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#Stop!") == VOICELINK_CMD_STOP, "exact Stop");

    // 语音模块每次识别固定发送同一帧两遍：验证第二遍照样能识别
    voicelink_frame_reset(&st);
    {
        voicelink_cmd_t r1 = feed_str(&st, "#Start!");
        voicelink_cmd_t r2 = feed_str(&st, "#Start!");
        CHECK(r1 == VOICELINK_CMD_START, "first of double-send");
        CHECK(r2 == VOICELINK_CMD_START, "second of double-send");
    }

    // 半截帧不能污染后续正常帧(新 '#' 必须无条件重开)
    voicelink_frame_reset(&st);
    {
        voicelink_cmd_t mid = feed_str(&st, "#Sto");
        CHECK(mid == VOICELINK_CMD_NONE, "partial frame yields nothing yet");
        voicelink_cmd_t r = feed_str(&st, "#Stop!");
        CHECK(r == VOICELINK_CMD_STOP, "restart after partial");
    }

    // 真实运动帧(#000P1500T1000!)不能被误判成语音命令 —— 关键回归防线
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#000P1500T1000!") == VOICELINK_CMD_NONE, "motion frame ignored");

    // 前导噪声字节被忽略，不影响后续正常帧
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "xyz#Start!") == VOICELINK_CMD_START, "leading noise ignored");

    // 超长垃圾不越界、不崩、不误判
    voicelink_frame_reset(&st);
    CHECK(feed_str(&st, "#ThisIsWayTooLongToBeAValidVoiceLinkFrame!") == VOICELINK_CMD_NONE,
          "oversized frame rejected safely");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: 跑测试确认失败**

Run: `gcc components/voicelink/test/test_voicelink_frame.c -Icomponents/voicelink/include -o /tmp/tvf 2>&1 | head`
Expected: 编译失败（`voicelink_frame.c` 尚不存在，链接器报 `undefined reference to voicelink_frame_reset` 等）。

- [ ] **Step 4: 写最小实现**

`components/voicelink/voicelink_frame.c`：

```c
#include "voicelink_frame.h"
#include <string.h>

void voicelink_frame_reset(voicelink_frame_state_t *st)
{
    st->len = 0;
}

voicelink_cmd_t voicelink_frame_feed(voicelink_frame_state_t *st, char c)
{
    if (c == '#') {
        st->len = 1;
        st->buf[0] = '#';
        return VOICELINK_CMD_NONE;
    }
    if (st->len == 0) {
        return VOICELINK_CMD_NONE;   // 还没见到帧起始符，忽略
    }
    if (c == '!') {
        if (st->len < VOICELINK_FRAME_MAX) {
            st->buf[st->len++] = '!';
            st->buf[st->len] = '\0';
        } else {
            st->len = 0;
            return VOICELINK_CMD_NONE;   // 超长，判定不是我们要的帧
        }
        voicelink_cmd_t cmd = VOICELINK_CMD_NONE;
        if (strcmp(st->buf, "#Start!") == 0) cmd = VOICELINK_CMD_START;
        else if (strcmp(st->buf, "#Stop!") == 0) cmd = VOICELINK_CMD_STOP;
        st->len = 0;   // 一帧结束，重置等下一帧
        return cmd;
    }
    if (st->len < VOICELINK_FRAME_MAX) {
        st->buf[st->len++] = c;
    } else {
        st->len = 0;   // 超界放弃这一帧，等下一个 '#' 重来
    }
    return VOICELINK_CMD_NONE;
}
```

- [ ] **Step 5: 跑测试确认通过**

Run: `gcc components/voicelink/test/test_voicelink_frame.c components/voicelink/voicelink_frame.c -Icomponents/voicelink/include -o /tmp/tvf && /tmp/tvf`
Expected: `ALL PASS`

- [ ] **Step 6: Commit**

```bash
git add components/voicelink/include/voicelink_frame.h components/voicelink/voicelink_frame.c components/voicelink/test/test_voicelink_frame.c
git commit -m "feat(voicelink): add pure-C #Start!/#Stop! frame matcher + host test

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: `voicelink` ESP-IDF 组件（Kconfig/CMake/UART 任务）+ 接入 main，默认关闭下 build 验证

**Files:**
- Create: `components/voicelink/include/voicelink.h`
- Create: `components/voicelink/voicelink.c`
- Create: `components/voicelink/Kconfig`
- Create: `components/voicelink/CMakeLists.txt`
- Modify: `main/main.c:12`（新增 include）、`main/main.c:65`（新增 init 调用）
- Modify: `main/CMakeLists.txt:3`（REQUIRES 加 `voicelink`）

**Interfaces:**
- Consumes（Task 1 产出）：`voicelink_frame_state_t`、`voicelink_cmd_t`、`voicelink_frame_reset()`、`voicelink_frame_feed()`
- Consumes（既有代码，`components/armctrl/include/armctrl.h`）：`void armctrl_request_run(bool on, bool cont);`、`bool armctrl_is_continuous(void);`
- Produces（供 Task 3 使用）：`esp_err_t voicelink_init(void);`；Kconfig 符号 `CONFIG_VOICELINK_ENABLE`、`CONFIG_VOICELINK_UART_PORT_NUM`、`CONFIG_VOICELINK_UART_RX_GPIO`、`CONFIG_VOICELINK_UART_BAUD`

- [ ] **Step 1: 写 Kconfig**

`components/voicelink/Kconfig`：

```
menu "voicelink (语音开始/停止桩)"

    config VOICELINK_ENABLE
        bool "启用语音链路 UART 接收 (会直接触发连续抓取!)"
        default n
        help
            默认关。开启后 voicelink 监听 UART，收到 KM1 转发的 #Start!/#Stop!
            会调用 armctrl_request_run() 直接触发/停止连续抓取。启用前必须
            完成 docs/ai/SAFETY.md 确认并分配 RX 引脚。

    config VOICELINK_UART_PORT_NUM
        int "UART 端口号"
        depends on VOICELINK_ENABLE
        default 2
        help
            用 UART2，避开 UART0(主 console)、UART1(armlink 占用)。

    config VOICELINK_UART_RX_GPIO
        int "RX GPIO (-1=未分配, 拒绝初始化)"
        depends on VOICELINK_ENABLE
        default -1
        help
            接收脚。默认 -1 强制显式分配；避开 35/36/37(Octal PSRAM)、
            0/3/45/46(strapping)、19/20(USB-JTAG)、26-32(SPI flash)、
            43/44(UART0 console)、摄像头占用的 4-13/15-18/21、armlink 占用的 1。
            推荐 GPIO2（已核对 ESP32-S3 官方引脚表，P2 级无限制通用脚）。

    config VOICELINK_UART_BAUD
        int "波特率"
        depends on VOICELINK_ENABLE
        default 115200
        help
            须与语音模块自身 Serial1 配置、以及 KM1 Serial1.begin() 的
            115200 一致，否则乱码。

endmenu
```

- [ ] **Step 2: 写公开头文件**

`components/voicelink/include/voicelink.h`：

```c
#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// 语音开始/停止桩：监听 UART，收到 KM1 转发的 #Start!/#Stop! 帧后
// 调用 armctrl_request_run()。默认关闭，见 Kconfig CONFIG_VOICELINK_ENABLE。
esp_err_t voicelink_init(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: 写实现（仿 `components/armlink/armlink_uart.c` 的 `#if CONFIG_..._ENABLE` 空桩模式）**

`components/voicelink/voicelink.c`：

```c
#include "voicelink.h"
#include "voicelink_frame.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "voicelink";

#if CONFIG_VOICELINK_ENABLE

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "armctrl.h"

#define VOICELINK_UART_RX_BUF 256   // 字节；单向接收，帧很短(<16B)，给最小缓冲

// 语音是低频离散事件（人说一句话才来一帧），栈 3072：逐字节读 + 两次 strcmp
// 短串比较，无深调用链，留有余量；优先级 3：与 detect_task 同级，不需要抢占
// 摄像头/网络任务。
static void voicelink_task(void *arg)
{
    (void)arg;
    voicelink_frame_state_t st;
    voicelink_frame_reset(&st);
    uint8_t byte;
    while (1) {
        int n = uart_read_bytes(CONFIG_VOICELINK_UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(200));
        if (n <= 0) continue;
        voicelink_cmd_t cmd = voicelink_frame_feed(&st, (char)byte);
        if (cmd == VOICELINK_CMD_START) {
            ESP_LOGI(TAG, "收到 #Start! -> armctrl_request_run(true,true)");
            armctrl_request_run(true, true);
        } else if (cmd == VOICELINK_CMD_STOP) {
            ESP_LOGI(TAG, "收到 #Stop! -> armctrl_request_run(false,...)");
            armctrl_request_run(false, armctrl_is_continuous());
        }
    }
}

esp_err_t voicelink_init(void)
{
    if (CONFIG_VOICELINK_UART_RX_GPIO < 0) {
        ESP_LOGE(TAG, "RX GPIO 未配置(-1)，拒绝初始化（防误触发连续抓取）");
        return ESP_ERR_INVALID_STATE;
    }
    uart_config_t cfg = {
        .baud_rate  = CONFIG_VOICELINK_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_param_config(CONFIG_VOICELINK_UART_PORT_NUM, &cfg);
    if (e != ESP_OK) return e;
    // TX = -1 (UART_PIN_NO_CHANGE)：只收不发
    e = uart_set_pin(CONFIG_VOICELINK_UART_PORT_NUM, UART_PIN_NO_CHANGE,
                     CONFIG_VOICELINK_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) return e;
    e = uart_driver_install(CONFIG_VOICELINK_UART_PORT_NUM, VOICELINK_UART_RX_BUF, 0, 0, NULL, 0);
    if (e != ESP_OK) return e;

    BaseType_t ok = xTaskCreate(voicelink_task, "voicelink", 3072, NULL, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "voicelink_task 创建失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGW(TAG, "UART%d 启用 rx=%d @%d baud — 语音可直接触发连续抓取!",
             CONFIG_VOICELINK_UART_PORT_NUM, CONFIG_VOICELINK_UART_RX_GPIO, CONFIG_VOICELINK_UART_BAUD);
    return ESP_OK;
}

#else  /* 禁用：空桩，不引 driver/uart 符号，零运行期风险 */

esp_err_t voicelink_init(void) { return ESP_OK; }

#endif
```

- [ ] **Step 4: 写 CMakeLists.txt**

`components/voicelink/CMakeLists.txt`：

```
idf_component_register(
    SRCS "voicelink.c" "voicelink_frame.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver armctrl
)
```

- [ ] **Step 5: 接入 main.c**

Modify `main/main.c` — 在第 12 行 `#include "armctrl.h"` 后新增一行：

```c
#include "armctrl.h"
#include "voicelink.h"
```

再在第 64-66 行（`armlink_init(); armctrl_init(); xTaskCreate(...)`）之间插入：

```c
        armlink_init();   // 机械臂目标产出器（UART 默认关，不驱动真臂）
        armctrl_init();
        voicelink_init();   // 语音开始/停止桩（UART 默认关，见 Kconfig CONFIG_VOICELINK_ENABLE）
        xTaskCreate(detect_task, "detect", 8192, NULL, 3, NULL);
```

- [ ] **Step 6: 接入 main/CMakeLists.txt**

Modify `main/CMakeLists.txt` 第 3 行，在 REQUIRES 列表末尾加 `voicelink`：

```
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES bsp net camera ai armlink kinematics armcal armctrl voicelink)
```

- [ ] **Step 7: build 验证（默认关闭，零回归）**

用 `mcp__idf-bridge__build`（或 `/esp-build`）跑一次全量 build。
Expected: build 通过（绿），无新增警告；因为 `CONFIG_VOICELINK_ENABLE` 默认 `n`，`voicelink.c` 编译进去的是最后那段空桩函数，不引 `driver/uart.h`/`armctrl.h` 符号，对现有功能零运行期影响。

- [ ] **Step 8: Commit**

```bash
git add components/voicelink/include/voicelink.h components/voicelink/voicelink.c components/voicelink/Kconfig components/voicelink/CMakeLists.txt main/main.c main/CMakeLists.txt
git commit -m "feat(voicelink): add UART task + wire into main (default OFF)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 3: 启用 voicelink，Brain 单机验证（USB-TTL 注入 GPIO2，不接 KM1）

> **前置确认**：开始本任务前，请确认 Brain 板子上 **GPIO2** 实际有排针可接 USB-TTL（设计阶段是代码排除法选的，未见实物核实）。若该脚不可用，换一个空闲脚（避开 spec §5 列出的占用/保留脚），本任务后续步骤同理替换。

**Files:**
- Modify: `sdkconfig`（menuconfig 生成，不手工编辑）

**Interfaces:**
- Consumes：Task 2 产出的 `voicelink_init()` 及三个 Kconfig 符号。

- [ ] **Step 1: 启用 Kconfig（仿 armlink 已有真实配置的写法）**

用 idf-bridge 对应的 menuconfig 流程，或直接确认这三行写入 `sdkconfig`（`voicelink (语音开始/停止桩)` 菜单下）：

```
CONFIG_VOICELINK_ENABLE=y
CONFIG_VOICELINK_UART_PORT_NUM=2
CONFIG_VOICELINK_UART_RX_GPIO=2
CONFIG_VOICELINK_UART_BAUD=115200
```

- [ ] **Step 2: build**

`mcp__idf-bridge__build`。
Expected: build 通过（绿）。此时 `voicelink.c` 编译进去的是真实 UART 初始化+任务那段（`#if CONFIG_VOICELINK_ENABLE` 分支）。

- [ ] **Step 3: flash（当场确认）**

明确告知用户：这次 flash 会让 Brain 真正监听 GPIO2 上的语音信号，虽然还没接语音模块/KM1，但已具备"收到匹配帧就调用 armctrl_request_run() 触发连续抓取"的能力——**若此时 Brain 已接臂上电，注入测试字节前请先确认臂周围安全**（armctrl 仍受未标定/急停锁存等既有前置条件保护，不会跳过 IK 自检）。用户确认后执行 `mcp__idf-bridge__flash`。

- [ ] **Step 4: 用 USB-TTL 注入测试字节，验证 Brain 侧解析正确**

用户接好 USB-TTL 到 GPIO2(RX)/GND，告知本机对应的 COM 口。Claude 侧用 PowerShell 直接向该串口写入测试帧（示例，端口号按实际替换）：

```powershell
$p = New-Object System.IO.Ports.SerialPort "COM<X>",115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$p.Open()
$p.Write("#Start!")
Start-Sleep -Milliseconds 200
$p.Write("#Stop!")
$p.Close()
```

同时用 `mcp__idf-bridge__monitor_start` 起监视，`monitor_read` 确认日志出现：

```
I (....) voicelink: 收到 #Start! -> armctrl_request_run(true,true)
...
I (....) voicelink: 收到 #Stop! -> armctrl_request_run(false,...)
```

并用网页 `/arm_run`（`GET` 查询，无参数）确认 `running`/`cont` 字段随之切换。

Expected: 日志与网页状态均正确反映两次注入。若无反应，先查波特率/接线/GPIO2 是否确为 RX（不是误接 TX）。

**注**：`sdkconfig` 本身是 `.gitignore` 掉的本地生成文件（不是 `sdkconfig.defaults`），本任务不需要、也不应该 commit 它——这只是本地验证用的临时开关。真正要长期生效、写入仓库的配置在 Task 5 最后一步统一处理。

---

## Task 4: KM1 `loop_uart()` 分发点转发逻辑（用户用 arduino-cli 编译烧录）

> **2026-07-08 修正**：原计划假设语音模块接 KM1 空闲的 Serial1(RX1=GPIO18)，转发走 TX1(GPIO17)。核对原理图（`D:\WJ\jixiebi\5.软件工具\1.原理图\OpenCESP.pdf`，KM1=Open-CESP V1.2）+ 用户实际接线后确认：**该板 Serial1 无排针引出，物理不可达**；语音模块实际接的是 KM1"语音接口"，和 Brain 所接的"openmv接口"经二极管共享同一条 RX2(GPIO41)，语音帧和运动帧走同一条总线、同一套 `uart_data_parse()` 管线。转发点相应改为 `loop_uart()` 的 `case 2:` 分发处，转发出口改为 TX2(GPIO42)（用户已接到 Brain GPIO2）。详见 spec §2/§4 修正说明。
>
> **前置确认**：改动目标是 `D:\WJ\jixiebi\steward_km1\steward_km1.ino`（用户自己的可编辑工程），**不是** `reference/esp32.ino` 或 `../4.源代码程序/` 下任何文件（只读参考）。下面给的是精确替换内容，Claude 可以直接用 Edit 工具改这个文件（它在本仓库之外，不受"只读参考"限制），但**编译和烧录由用户用 arduino-cli 自己完成**。

**Files:**
- Modify: `D:\WJ\jixiebi\steward_km1\steward_km1.ino`（`loop_uart()` 函数，约第 732 行附近；行号以改动前最新读取为准，若之前有其它改动导致行号偏移，按函数名定位而非行号）

**Interfaces:**
- 不影响本仓库任何符号；这段代码独立运行在 KM1 自己的芯片上。

- [ ] **Step 1: 定位并替换 `loop_uart()`**

原内容（`steward_km1.ino`）：

```c
void loop_uart() {
    if (uart_get_ok) {
        switch (uart_mode) {
            case 1: parse_cmd(uart_receive_buf); break;
            case 2:
            case 3: parse_action(uart_receive_buf); break;
            case 4: save_action(uart_receive_buf); break;
        }

        uart_get_ok = false;
        uart_mode = 0;
        uart_receive_str = "";
    }
}
```

替换为：

```c
void loop_uart() {
    if (uart_get_ok) {
        switch (uart_mode) {
            case 1: parse_cmd(uart_receive_buf); break;
            case 2:
                // 语音链路：原理图 Open-CESPV1.2 "语音接口"与"openmv接口"(Brain 所接)
                // 经二极管共享同一条 RX2，语音帧和运动帧混在同一条总线上进来，
                // 都会先经过上面的 uart_data_parse 攒成 "#...!" 帧。这里精确匹配
                // 语音模块发的 "#Start!"/"#Stop!"，命中就原样从 TX2 转发给 Brain
                // (Brain 新接的 GPIO2 监听这条线)，不当运动帧处理；其余 # 帧
                // (含真实运动帧 #dddPddddTdddd!)行为完全不变，走 parse_action。
                if (strcmp(uart_receive_buf, "#Start!") == 0 || strcmp(uart_receive_buf, "#Stop!") == 0) {
                    Serial2.print(uart_receive_buf);
                } else {
                    parse_action(uart_receive_buf);
                }
                break;
            case 3: parse_action(uart_receive_buf); break;
            case 4: save_action(uart_receive_buf); break;
        }

        uart_get_ok = false;
        uart_mode = 0;
        uart_receive_str = "";
    }
}
```

不改动 `uart_data_parse`、`handleSerial1`、`handleSerial2` 任何一行；`case 3`（`{...}` 动作组执行模式）和其余分支行为完全不变。`strcmp` 无需新增头文件——文件里 `memset`（同属 `<string.h>`）已经在用，确认可用。

- [ ] **Step 2: 用户自行编译（同以往编译该工程的命令）**

请用户在自己的 arduino-cli 环境里，用平时编译 `steward_km1.ino` 的同一条命令重新编译一次（FQBN/参数保持不变），确认编译通过、体积/内存占用与之前相近（`compile.log` 里之前是 "400783 bytes (30%)" / "26420 bytes (8%)"，新增这一小段代码后应该只有几十字节的差异，不应有明显跳变）。

Expected: 编译成功，无报错，体积无异常跳变。

- [ ] **Step 3: 验证 KM1 转发（建议先让 Brain 断电/拔线，避免共享总线上同时有真实运动帧干扰判断）**

烧录到 KM1 后（用户自己烧），用 USB-TTL 以 115200 波特率向共享 RX2 节点注入（接"语音接口"或"openmv接口"任一 4 针座的对应数据脚均可，二极管保证不会电气冲突），发送 `#Start!`，同时监听 TX2(GPIO42) 是否原样转发出来。

Expected: TX2 上原样收到 `#Start!`；再测 `#Stop!` 同理。这一步只验证 KM1 转发逻辑本身没写错，排除掉一个环节的不确定性再往下走；端到端联调（Task 5）时才需要 Brain 同时在线、真实体验共享总线上的交织情况。

---

## Task 5: 端到端接线 + 真实语音测试 + 回归检查

> **前置确认**：本任务开始前确认 (a) 语音模块图形化配置里 Serial1 波特率已设为 115200（对应 spec 开放项 1）；(b) Task 3 的 Brain 侧、Task 4 的 KM1 侧已分别验证通过。

**Files:** 无代码改动，纯接线 + 真机验证。

- [ ] **Step 1: 接线确认**

用户已完成接线（2026-07-08 确认）：语音模块 → KM1"语音接口"（经二极管入共享 RX2）；Brain GPIO2 ← KM1"openmv接口"TX2(GPIO42)。开始本步骤前确认这两处接线仍然牢固（尤其 Brain GPIO2 那根是新接的）。

- [ ] **Step 2: 端到端语音测试**

对着语音模块说"小电"（等待其回复"我在"），再说"开始处理"，观察：
- Brain monitor 日志出现 `voicelink: 收到 #Start!`；
- 网页 `/arm_run` 查询 `running:true, cont:true`；
- 机械臂开始连续抓取动作（若已标定、IK 自检通过）。

再说"小电" + "停止"，观察：
- Brain 日志出现 `voicelink: 收到 #Stop!`；
- 网页 `running` 变回 `false`；
- 机械臂完成当前动作后停止（缓停，不是急停）。

- [ ] **Step 3: 回归检查**

确认接入语音链路后：
- 网页 `arun(1)`/`arun(0)` 按钮控制依旧正常；
- 网页急停 `aestop()` 依旧能立即停止并锁存；
- Brain→KM1 正常连续抓取（真实运动帧路径）未受 KM1 侧 `loop_uart()` 改动影响——改动只在 `case 2:` 分支加一次内容判断，不匹配语音词的帧原样调用 `parse_action`，理论零风险，但仍需真机跑一轮完整"识别→抓取→切割→放回"确认无异常（这是本设计里唯一需要在"语音帧+运动帧真实交织在同一条共享 RX2 总线"条件下验证的场景，见 spec §2 修正说明）。

- [ ] **Step 4: 若一切通过，把配置固化进 `sdkconfig.defaults`（否则 fullclean/新拉仓库默认语音是关的）**

`sdkconfig`（Task 3 手工/menuconfig 改的那份）是 `.gitignore` 掉的本地文件，不会随仓库分发。确认端到端跑通后，仿 `sdkconfig.defaults` 里 `armlink` 那四行的写法，在文件末尾追加（`sdkconfig.defaults` 已有的 `CONFIG_ARMLINK_UART_*` 四行下方即可）：

```
CONFIG_VOICELINK_ENABLE=y
CONFIG_VOICELINK_UART_PORT_NUM=2
CONFIG_VOICELINK_UART_RX_GPIO=2
CONFIG_VOICELINK_UART_BAUD=115200
```

改完跑一次 `mcp__idf-bridge__build` 确认这四行单独也能重新生成一份能跑的 `sdkconfig`（不依赖 Task 3 手工改过的本地状态），然后：

```bash
git add sdkconfig.defaults
git commit -m "feat(voicelink): enable voice start/stop by default (rx=GPIO2, verified end-to-end)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 5: 更新经验沉淀**

用 `/learn` 把本次涉及的关键坑（KM1 共享解析器忙时丢字节的问题、Serial1/Serial2 隔离方案）写入 `docs/ai/CRASH_SIGNATURES.md` 或等价位置，供后续参考。

---

## Self-Review 记录

- **Spec 覆盖**：spec §3"做"的 4 条（KM1 handleSerial1 改造、Brain voicelink 组件、新接线、分步验证）分别对应 Task 4、Task 1-3、Task 5-Step1、Task 3-5 全程。spec §6 错误处理里"急停锁存拒绝开始"/"停止不受锁存影响"/"半截帧不误判"分别由 Task2-armctrl_request_run 复用、Task2-armctrl_is_continuous 保留、Task1 单测里的 partial-frame/motion-frame 用例覆盖。spec §8 三个开放项分别在 Task3 前置确认、Task4 前置确认、Task5 前置确认里显式列为门槛，未被跳过假设。
- **占位符扫描**：全文无 TBD/TODO/"补充错误处理"类占位；所有代码块均为完整可运行内容。
- **类型一致性**：`voicelink_cmd_t`/`voicelink_frame_state_t`/`voicelink_frame_reset`/`voicelink_frame_feed` 在 Task1 定义、Task2 引用，签名逐字一致；`voicelink_init()` 返回类型 `esp_err_t` 在 Task2 头文件与实现、main.c 调用处一致；Kconfig 四个符号名在 Task2/Task3/Task5 一致。
- **自查中发现并修正**：初稿 Task3 误写了"commit sdkconfig"——实际 `sdkconfig` 被 `.gitignore` 排除（只有 `sdkconfig.defaults` 入库，已用 `git check-ignore`/`git ls-files` 核实），已改为 Task3 只做本地验证、Task5 末尾才把配置固化进 `sdkconfig.defaults` 并 commit，同时补一次"仅凭 defaults 重新生成配置也能 build"的校验，避免"本地能跑、入库后跑不出来"的坑。
- **执行中发现并修正（2026-07-08，比自查更晚）**：Task 4 原方案（KM1 Serial1/RX1=18/TX1=17）已按原方案写完代码，但用户反馈"板子上没找到 IO17 的位置"，核对原理图（`Open-CESPV1.2`）后确认该板 Serial1 根本没有排针引出，语音模块实际经"语音接口"与 Brain 所接的"openmv接口"共享同一条 RX2(GPIO41)（二极管 OR，电气安全，但逻辑上同一条总线/同一套解析管线）。已撤销原 `handleSerial1()` 改动，改为在 `loop_uart()` 的 `case 2:` 分发点做内容匹配、经 TX2(GPIO42) 转发（用户已把该脚接到 Brain GPIO2）。spec 与本计划的 Task 4 均已同步更正；Task 1/2（Brain 侧 voicelink 组件）不受影响，未返工。教训：给外部小板子（非标准 DevKit）选引脚时，代码里"看起来空闲"不等于"板子上有排针"，应优先核对原理图或实物，而非仅凭固件代码反推。
