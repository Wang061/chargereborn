# Phase 2 — 完整 IK 自动抓取路径 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Brain(ESP32-S3) 侧实现几何正确的逆运动学 + 单应性绝对定位 + 抓取-切割状态机，用已验证的裸协议捆绑帧驱动 KM1 机械臂完成"识别→抓取→切割→放回→回观察位"闭环。

**Architecture:** 三层组件——`kinematics`(纯 IK 数学，host 可测) / `armctrl`(FreeRTOS 抓取状态机=业务层) / `armlink`(选目标+传输层，新增捆绑帧编码)。定位用固定观察位姿 + NVS 持久化的单应性矩阵 `px→台面mm`，替代旧方案的开环偏移猜测。安全走 G0-G4 分级干跑，运行时开关默认全关。

**Tech Stack:** ESP-IDF 5.5.4 / ESP32-S3；C（IK/armctrl/armlink，纯 C 核心 host 用 mingw gcc 5.3 单测）；Python 3.13（PC 端单应性标定辅助 + IK 参考实现对拍）；NVS（标定持久化）；现有 `components/ai`（ESPDet 检测，输出 `ai_result_t`）。

## Global Constraints

（每个任务的要求都隐含包含本节，值从 spec / 项目规则逐字复制）

- `IDF_VERSION = 5.5.4`，`IDF_TARGET = esp32s3`（**不得改**）。构建走 `mcp__idf-bridge__build` 或 `scripts/idf.ps1`，**不裸跑 idf.py**。
- `FLASH_BAUD = 921600`（失败回退 460800），`MONITOR_BAUD = 115200`。
- **flash / set-target / erase / fullclean 必须当场确认**（guard.py 兜底；SAFETY.md 铁律）。本计划先 build 不 flash；涉及 flash 的步骤显式标注"需用户确认"。
- **裸协议捆绑帧**是唯一驱动方式：`{#000Pxxxx Tyyyy!#001Pxxxx Tyyyy!#002Pxxxx Tyyyy!#003Pxxxx Tyyyy!}`。**禁用 `$KMS:`**（真机固件未实现，CRASH_SIGNATURES 已记）。
- 舵机脉宽硬限位 **[500, 2500] us**；越界即钳位并告警。舵机 270°，映射 `1500 ± 2000·angle/270`。
- 危险 GPIO 回避：35/36/37(PSRAM)、0/3/45/46(strapping)、19/20(USB-JTAG)。armlink UART TX 已定 GPIO1。
- httpd handler `stack_size = 8192`（已在位，勿回退到 4096——CRASH_SIGNATURES 栈溢出）。
- IK **底座角公式必须** `atan2(x,y)*180/π`（**绝不用** OpenMV 的 `*270/π`——放大 1.5x bug，低夹取率根因之一）。
- 组件化 + 分层：新功能进 `components/<name>/`，纯算法与硬件/业务分离。统一 `static const char *TAG`；禁 magic number（用 `#define`/`const` 注单位）。
- 运行时开关（`s_auto_send`、抓取循环、安全级）默认全关；未标定拒绝进自动模式。

---

## 任务总览与 7 天里程碑映射

| 任务 | 交付物 | 里程碑 |
|---|---|---|
| T1 | SAFETY.md 填实测值 + 审 | D1(7/3) |
| T2 | `components/kinematics` 纯 IK（host TDD） | D1 |
| T3 | armlink 捆绑帧编码器（host TDD） | D1 |
| T4 | 上板 IK 自检（启动对拍 golden，不符拒绝自动） | D1 build 绿 |
| T5 | `components/armcal` NVS 标定存储 | D2(7/4) |
| T6 | px→mm 单应性应用（host TDD） | D2 |
| T7 | `/arm_calib` 端点 + PC 端标定脚本 | D2 + G0/G1 硬件 |
| T8 | `components/armctrl` 骨架 + 安全级联锁 + `/arm_grade`/`/arm_run` | D3(7/5) |
| T9 | armctrl 多帧位姿平均 + 抖动门 | D3 |
| T10 | armctrl 抓取序列（接近/悬停/下降/夹/抬） | D4(7/6) G3 |
| T11 | armctrl 切割 + 放回 + 完整循环 | D5(7/7) G4 |
| T12 | 收敛自动路径 + 集成 + 文档/记忆 | D6-7(7/8-9) |

---

## Task 1: SAFETY.md 填实测值 + 审核（安全门，无舵机代码）

**Files:**
- Modify: `docs/ai/SAFETY.md`（填 §舵机约束 / §上电急停 的 TODO）

**Interfaces:**
- Produces: 已填写的硬件安全基线，被后续所有涉及舵机上电的任务（T7 起）作为前置门。

- [ ] **Step 1: 用说明书实测值替换 SAFETY.md 的 TODO 表**

把 `docs/ai/SAFETY.md` 的"舵机/执行器约束"表改为（值来自 KM1 说明书 p17/p18 + reference/esp32.ino 实测）：

```markdown
## 舵机 / 执行器约束（KM1 实测/说明书值）
| 项 | 值 |
|---|---|
| 舵机型号 / 路数 | TBS-K20 / TBD-K20，6 路（#000 底座旋转 / #001 大臂 / #002 小臂 / #003 腕俯仰 / #004 腕旋转 / #005 夹爪） |
| 每路安全 PWM 脉宽范围 (us) | 硬限位 500–2500（软限位见各关节，越界钳位） |
| 各关节角度软限位 | IK 内建：theta5∈[0,180]、theta4∈[-135,135]、theta3∈[-90,90]（超出即判不可达，不发帧） |
| 电源电压 / 总电流预算 (A) | 6–8.4V，额定 ≥3A（多轴峰值不得同时；单轴顺序动或限速兜底） |
| 切割/开口机构触发条件与互锁 | 仅 G4 级 + 已装刀 + 已抓取到电池 + 到达刀口安全位后才下探接触 z |

## 上电 / 急停
- 上电顺序：先逻辑（USB 供 Brain）后动力（舵机独立 6-8.4V 电源），断电反序。
- 急停方式：物理电源开关断动力电（首选）；软件 `$DST!`（总线停）+ 停止抓取循环。
- 复位后默认安全位：腕中位(#004=1500)、开爪(#005=开)、抬到 carry_z，避免突然甩动。
```

- [ ] **Step 2: 提交并请用户审核**

```bash
cd /d/WJ/jixiebi/WORKplace
git add docs/ai/SAFETY.md
git commit -m "docs(safety): 填 KM1 舵机限位/电流/急停实测值(Phase2 前置门)"
```

Run: 无（文档任务）。Expected: commit 成功。**此任务完成后请用户口头确认 SAFETY 表无误，再进后续涉及舵机上电的任务。**

---

## Task 2: `components/kinematics` 纯逆运动学（host TDD）

**Files:**
- Create: `components/kinematics/kinematics.c`
- Create: `components/kinematics/include/kinematics.h`
- Create: `components/kinematics/CMakeLists.txt`
- Test: `components/kinematics/test/test_kinematics.c`（host gcc 手动编译，不进 IDF 构建）

**Interfaces:**
- Produces:
  - `void kin_setup(const float link_mm[4], float out_links[4])` — 存连杆（mm，无 x10 放大），拷进 out_links。
  - `int kin_solve(const float links[4], float x, float y, float z, float alpha_deg, int out_pwm[4])` — 单 alpha 解算。返回 0=成功，1-7=不可达错误码。out_pwm 仅成功时有效。
  - `int kin_move_best(const float links[4], float x, float y, float z, int out_pwm[4])` — 扫 alpha∈[0,-135] 取最负可达解。返回 0=成功、-1=无解或 y<0。
  - 单位：x,y,z 为 mm；out_pwm 为 us（500-2500 域，本函数不额外钳位，越界由调用方处理，但正常解在域内）。

- [ ] **Step 1: 写头文件**

`components/kinematics/include/kinematics.h`:

```c
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// 纯逆运动学。无 ESP 依赖，可 host 单测。单位: mm / us / deg。
// 连杆: links[0..3] = L0(底座高) L1(大臂) L2(小臂) L3(腕到爪尖)，mm。

// 存连杆到 out_links（简单拷贝，保留接口对称性）。
void kin_setup(const float link_mm[4], float out_links[4]);

// 单 Alpha(爪-平面夹角,deg)解算 4 舵机 PWM。
// 返回: 0=成功; 1=z过低; 2=超伸展; 3=bbb域外; 4=theta5域外; 5=aaa域外; 6=theta4域外; 7=theta3域外。
int kin_solve(const float links[4], float x, float y, float z, float alpha_deg, int out_pwm[4]);

// 扫 Alpha 0→-135° 取最负可达解(3号舵机与水平最大夹角)。
// 返回: 0=成功(out_pwm 有效); -1=y<0 或全程无解。
int kin_move_best(const float links[4], float x, float y, float z, int out_pwm[4]);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写失败测试（golden 向量来自修正公式的 Python 参考）**

`components/kinematics/test/test_kinematics.c`:

```c
#include "kinematics.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); fails++; } } while(0)

// 测试用固定连杆(仅验证数学,与真机实测连杆无关): L0=100 L1=105 L2=75 L3=180 mm
static const float TL[4] = {100.0f, 105.0f, 75.0f, 180.0f};

int main(void) {
    int pwm[4];
    int r;

    // Anchor A: x=0 正前方 -> 底座居中 pwm0=1500
    r = kin_move_best(TL, 0, 200, 50, pwm);
    CHECK(r == 0, "A reachable");
    CHECK(pwm[0] == 1500 && pwm[1] == 1259 && pwm[2] == 1776 && pwm[3] == 861, "A pwm golden");

    // Anchor B: x=y=120 (底座45°) -> pwm0=1166 (若=1000 说明 theta6 用了错误的 *270/pi)
    r = kin_move_best(TL, 120, 120, 40, pwm);
    CHECK(r == 0, "B reachable");
    CHECK(pwm[0] == 1166 && pwm[1] == 1300 && pwm[2] == 1855 && pwm[3] == 840, "B pwm golden");
    CHECK(pwm[0] != 1000, "B not the *270/pi bug");

    // Anchor C: 太远不可达
    r = kin_move_best(TL, 0, 900, 50, pwm);
    CHECK(r == -1, "C unreachable");

    // Anchor D: y<0 拒绝
    r = kin_move_best(TL, 0, -50, 50, pwm);
    CHECK(r == -1, "D y<0 rejected");

    // solve 指定 alpha=-45 (B 点)
    r = kin_solve(TL, 120, 120, 40, -45.0f, pwm);
    CHECK(r == 0, "B45 solve ok");
    CHECK(pwm[0] == 1166 && pwm[1] == 1597 && pwm[2] == 2470 && pwm[3] == 1372, "B45 pwm golden");

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: 运行测试确认失败（实现未写）**

Run:
```bash
cd /d/WJ/jixiebi/WORKplace
gcc components/kinematics/test/test_kinematics.c components/kinematics/kinematics.c -Icomponents/kinematics/include -lm -o /tmp/tk 2>&1 | head
```
Expected: 编译失败（`kinematics.c` 不存在 / 函数未定义）。

- [ ] **Step 4: 写最小实现（修正版几何 IK）**

`components/kinematics/kinematics.c`:

```c
#include "kinematics.h"
#include <math.h>

#define KIN_PI 3.14159265358979f

void kin_setup(const float link_mm[4], float out_links[4])
{
    for (int i = 0; i < 4; i++) out_links[i] = link_mm[i];
}

int kin_solve(const float links[4], float x, float y, float z, float alpha_deg, int out_pwm[4])
{
    float l0 = links[0], l1 = links[1], l2 = links[2], l3 = links[3];
    float theta3, theta4, theta5, theta6;
    float aaa, bbb, ccc, zf;

    // 底座旋转: 弧度->度用 180/pi (绝不用 270/pi)
    theta6 = (x == 0.0f) ? 0.0f : atan2f(x, y) * 180.0f / KIN_PI;

    float yy = sqrtf(x * x + y * y);
    yy = yy - l3 * cosf(alpha_deg * KIN_PI / 180.0f);
    float zz = z - l0 - l3 * sinf(alpha_deg * KIN_PI / 180.0f);
    if (zz < -l0) return 1;
    if (sqrtf(yy * yy + zz * zz) > (l1 + l2)) return 2;

    ccc = acosf(yy / sqrtf(yy * yy + zz * zz));
    bbb = (yy * yy + zz * zz + l1 * l1 - l2 * l2) / (2.0f * l1 * sqrtf(yy * yy + zz * zz));
    if (bbb > 1.0f || bbb < -1.0f) return 3;
    zf = (zz < 0.0f) ? -1.0f : 1.0f;
    theta5 = (ccc * zf + acosf(bbb)) * 180.0f / KIN_PI;
    if (theta5 > 180.0f || theta5 < 0.0f) return 4;

    aaa = -(yy * yy + zz * zz - l1 * l1 - l2 * l2) / (2.0f * l1 * l2);
    if (aaa > 1.0f || aaa < -1.0f) return 5;
    theta4 = 180.0f - acosf(aaa) * 180.0f / KIN_PI;
    if (theta4 > 135.0f || theta4 < -135.0f) return 6;

    theta3 = alpha_deg - theta5 + theta4;
    if (theta3 > 90.0f || theta3 < -90.0f) return 7;

    out_pwm[0] = (int)(1500.0f - 2000.0f * theta6 / 270.0f);
    out_pwm[1] = (int)(1500.0f + 2000.0f * (theta5 - 90.0f) / 270.0f);
    out_pwm[2] = (int)(1500.0f + 2000.0f * theta4 / 270.0f);
    out_pwm[3] = (int)(1500.0f + 2000.0f * theta3 / 270.0f);
    return 0;
}

int kin_move_best(const float links[4], float x, float y, float z, int out_pwm[4])
{
    if (y < 0.0f) return -1;
    int best_alpha = 0, found = 0, tmp[4];
    for (int i = 0; i >= -135; i--) {
        if (kin_solve(links, x, y, z, (float)i, tmp) == 0) {
            if (i < best_alpha) best_alpha = i;
            found = 1;
        }
    }
    if (!found) return -1;
    return kin_solve(links, x, y, z, (float)best_alpha, out_pwm);  // 0
}
```

- [ ] **Step 5: 运行测试确认通过**

Run:
```bash
gcc components/kinematics/test/test_kinematics.c components/kinematics/kinematics.c -Icomponents/kinematics/include -lm -o /tmp/tk && /tmp/tk
```
Expected: `ALL PASS`。

- [ ] **Step 6: 写组件 CMakeLists（进 IDF 构建）**

`components/kinematics/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "kinematics.c"
    INCLUDE_DIRS "include"
)
```

- [ ] **Step 7: 提交**

```bash
git add components/kinematics
git commit -m "feat(kin): Brain侧几何逆运动学(修 theta6 放大bug)+host golden测试"
```

---

## Task 3: armlink 捆绑帧编码器（host TDD）

**Files:**
- Create: `components/armlink/armlink_frame.c`（纯 C，无 ESP 依赖，host 可测）
- Create: `components/armlink/include/armlink_frame.h`
- Modify: `components/armlink/CMakeLists.txt`（加入 armlink_frame.c）
- Test: `components/armlink/test/test_frame.c`（host gcc）

**Interfaces:**
- Consumes: 无。
- Produces:
  - `int armlink_clamp_pwm(int pwm)` — 钳到 [500,2500]。
  - `int armlink_encode_arm_frame(const int pwm[4], int move_ms, char *out, size_t n)` — 编码 4 舵机(#000-#003)捆绑帧 `{#000Pxxxx Tyyyy!...}`，各 PWM 自动钳位。返回写入长度或 -1。
  - `int armlink_encode_servo_frame(int idx, int pwm, int move_ms, char *out, size_t n)` — 单舵机帧 `{#0iiPxxxx Tyyyy!}`（腕#004/夹爪#005 用），PWM 钳位。返回长度或 -1。

- [ ] **Step 1: 写头文件**

`components/armlink/include/armlink_frame.h`:

```c
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

int armlink_clamp_pwm(int pwm);
int armlink_encode_arm_frame(const int pwm[4], int move_ms, char *out, size_t n);
int armlink_encode_servo_frame(int idx, int pwm, int move_ms, char *out, size_t n);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写失败测试**

`components/armlink/test/test_frame.c`:

```c
#include "armlink_frame.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

int main(void) {
    char buf[128];
    int n;

    CHECK(armlink_clamp_pwm(400) == 500, "clamp low");
    CHECK(armlink_clamp_pwm(3000) == 2500, "clamp high");
    CHECK(armlink_clamp_pwm(1500) == 1500, "clamp mid");

    int pwm[4] = {1166, 1300, 1855, 840};
    n = armlink_encode_arm_frame(pwm, 1000, buf, sizeof(buf));
    CHECK(n > 0, "arm frame len");
    CHECK(strcmp(buf, "{#000P1166T1000!#001P1300T1000!#002P1855T1000!#003P0840T1000!}") == 0, "arm frame bytes");

    // 越界自动钳位
    int pwm2[4] = {400, 3000, 1500, 1500};
    armlink_encode_arm_frame(pwm2, 800, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#000P0500T0800!#001P2500T0800!#002P1500T0800!#003P1500T0800!}") == 0, "arm frame clamp");

    n = armlink_encode_servo_frame(4, 1500, 600, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#004P1500T0600!}") == 0, "servo frame #004");

    n = armlink_encode_servo_frame(5, 1700, 800, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#005P1700T0800!}") == 0, "servo frame #005");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: 运行确认失败**

Run:
```bash
gcc components/armlink/test/test_frame.c components/armlink/armlink_frame.c -Icomponents/armlink/include -o /tmp/tf 2>&1 | head
```
Expected: 编译失败（armlink_frame.c 不存在）。

- [ ] **Step 4: 写实现**

`components/armlink/armlink_frame.c`:

```c
#include "armlink_frame.h"
#include <stdio.h>

#define ARM_PWM_MIN 500
#define ARM_PWM_MAX 2500

int armlink_clamp_pwm(int pwm)
{
    if (pwm < ARM_PWM_MIN) return ARM_PWM_MIN;
    if (pwm > ARM_PWM_MAX) return ARM_PWM_MAX;
    return pwm;
}

int armlink_encode_arm_frame(const int pwm[4], int move_ms, char *out, size_t n)
{
    if (!pwm || !out || n == 0) return -1;
    return snprintf(out, n,
        "{#000P%04dT%04d!#001P%04dT%04d!#002P%04dT%04d!#003P%04dT%04d!}",
        armlink_clamp_pwm(pwm[0]), move_ms,
        armlink_clamp_pwm(pwm[1]), move_ms,
        armlink_clamp_pwm(pwm[2]), move_ms,
        armlink_clamp_pwm(pwm[3]), move_ms);
}

int armlink_encode_servo_frame(int idx, int pwm, int move_ms, char *out, size_t n)
{
    if (!out || n == 0) return -1;
    return snprintf(out, n, "{#%03dP%04dT%04d!}", idx, armlink_clamp_pwm(pwm), move_ms);
}
```

- [ ] **Step 5: 运行确认通过**

Run:
```bash
gcc components/armlink/test/test_frame.c components/armlink/armlink_frame.c -Icomponents/armlink/include -o /tmp/tf && /tmp/tf
```
Expected: `ALL PASS`。

- [ ] **Step 6: 加入组件构建**

修改 `components/armlink/CMakeLists.txt` 的 SRCS 行为：

```cmake
idf_component_register(
    SRCS "armlink.c" "armlink_uart.c" "armlink_frame.c"
    INCLUDE_DIRS "include"
    REQUIRES ai
    PRIV_REQUIRES driver
)
```

- [ ] **Step 7: 提交**

```bash
git add components/armlink/armlink_frame.c components/armlink/include/armlink_frame.h components/armlink/test components/armlink/CMakeLists.txt
git commit -m "feat(armlink): 4舵机捆绑帧+单舵机帧编码器(PWM钳位)+host测试"
```

---

## Task 4: 上板 IK 自检（启动对拍 golden，不符拒绝自动模式）

**Files:**
- Modify: `components/kinematics/kinematics.c`（加自检函数）
- Modify: `components/kinematics/include/kinematics.h`（声明）
- Modify: `main/main.c`（app_main 调用自检 + 加 kinematics 到 REQUIRES）
- Modify: `main/CMakeLists.txt`（REQUIRES 加 kinematics）

**Interfaces:**
- Consumes: `kin_solve`（Task 2）。
- Produces: `int kin_selftest(void)` — 用固定测试连杆跑 golden 对拍，返回 0=通过、非0=数学损坏（回归了 theta6 bug 等）。供 main 与后续 armctrl 联锁。

- [ ] **Step 1: 加自检声明到 kinematics.h**

在 `kinematics.h` 的 `kin_move_best` 声明后加：

```c
// 用固定测试连杆(100,105,75,180)对拍内嵌 golden PWM，验证 IK 数学未回归(尤其 theta6)。
// 返回 0=通过; 非0=失败(数学损坏，调用方应拒绝进自动模式)。
int kin_selftest(void);
```

- [ ] **Step 2: 加自检实现到 kinematics.c 末尾**

```c
int kin_selftest(void)
{
    const float tl[4] = {100.0f, 105.0f, 75.0f, 180.0f};
    int pwm[4];
    // Anchor A: x=0,y=200,z=50 -> [1500,1259,1776,861]
    if (kin_move_best(tl, 0, 200, 50, pwm) != 0) return 1;
    if (pwm[0] != 1500 || pwm[1] != 1259 || pwm[2] != 1776 || pwm[3] != 861) return 2;
    // Anchor B: x=120,y=120,z=40 -> [1166,1300,1855,840]（pwm0=1000 即 theta6 bug）
    if (kin_move_best(tl, 120, 120, 40, pwm) != 0) return 3;
    if (pwm[0] != 1166 || pwm[1] != 1300 || pwm[2] != 1855 || pwm[3] != 840) return 4;
    // Anchor C: 不可达
    if (kin_move_best(tl, 0, 900, 50, pwm) != -1) return 5;
    return 0;
}
```

- [ ] **Step 3: main.c 调用自检**

在 `main/main.c` 的 `#include "armlink.h"` 后加 `#include "kinematics.h"`；在 `app_main` 的 `bsp_psram_selftest();` 之后加：

```c
    int kst = kin_selftest();
    if (kst != 0) {
        ESP_LOGE(TAG, "IK 自检失败(code=%d)! 运动学数学损坏，禁止自动抓取", kst);
    } else {
        ESP_LOGI(TAG, "IK 自检通过");
    }
```

- [ ] **Step 4: main CMakeLists 加依赖**

修改 `main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES bsp net camera ai armlink kinematics)
```

- [ ] **Step 5: build 验证（不 flash）**

Run: `mcp__idf-bridge__build`
Expected: build 绿；无未定义符号。

- [ ] **Step 6: 提交**

```bash
git add components/kinematics main/main.c main/CMakeLists.txt
git commit -m "feat(kin): 上板启动 IK 自检对拍 golden(防 theta6 回归)"
```

---

## Task 5: `components/armcal` NVS 标定存储

**Files:**
- Create: `components/armcal/armcal.c`
- Create: `components/armcal/include/armcal.h`
- Create: `components/armcal/CMakeLists.txt`

**Interfaces:**
- Consumes: 无（用 nvs_flash）。
- Produces:
  - `armcal_t` 结构（见下，含 H[9]、连杆、观察位、腕角 K/零位、夹爪 PWM、刀口坐标、各高度、`valid`）。
  - `void armcal_defaults(armcal_t *c)` — 填 OpenMV 继承的起点值 + `valid=false`。
  - `esp_err_t armcal_load(armcal_t *out)` — 从 NVS 读；无记录则填 defaults 返回 ESP_ERR_NVS_NOT_FOUND。
  - `esp_err_t armcal_save(const armcal_t *c)` — blob 写入 NVS namespace `armcal` key `cfg`。

- [ ] **Step 1: 写头文件**

`components/armcal/include/armcal.h`:

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// 机械臂标定/参数集（NVS 持久化）。所有 mm/us/deg。
typedef struct {
    float H[9];                 // 单应性 px->台面mm(中轴平面,行主序)
    float link_mm[4];           // L0..L3 卷尺实测(mm)
    float observe_x, observe_y, observe_z;  // 观察位姿(mm)
    int   wrist_center_pwm;     // 腕#004 中位
    float wrist_k;              // 腕 pwm/deg
    int   wrist_zero_deg;       // 腕零位角偏移
    int   gripper_open_pwm, gripper_close_pwm;  // 夹爪#005
    int   gripper_time_ms;
    float pick_z, approach_z, carry_z, place_z; // 抓取高度
    float blade_x, blade_y, blade_safe_z, blade_contact_z, cut_offset_x;
    int   cut_times;
    bool  valid;                // H 已标定?
    uint32_t magic;             // 版本/校验
} armcal_t;

#define ARMCAL_MAGIC 0x41430201u  // 'AC' + ver 02 01

void armcal_defaults(armcal_t *c);
esp_err_t armcal_load(armcal_t *out);
esp_err_t armcal_save(const armcal_t *c);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写实现**

`components/armcal/armcal.c`:

```c
#include "armcal.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "armcal";
#define NS  "armcal"
#define KEY "cfg"

void armcal_defaults(armcal_t *c)
{
    memset(c, 0, sizeof(*c));
    // 单位阵占位(未标定)
    c->H[0] = 1.0f; c->H[4] = 1.0f; c->H[8] = 1.0f;
    // 连杆起点(真机 .ino 值; 卷尺实测后覆盖)
    c->link_mm[0] = 100.0f; c->link_mm[1] = 105.0f;
    c->link_mm[2] = 75.0f;  c->link_mm[3] = 180.0f;
    // 观察位(起点,须实测调; y 正前方, z 高处俯视)
    c->observe_x = 0.0f; c->observe_y = 130.0f; c->observe_z = 120.0f;
    // 腕#004(OpenMV 继承)
    c->wrist_center_pwm = 1500; c->wrist_k = 5.6f; c->wrist_zero_deg = 0;
    // 夹爪#005(OpenMV 继承, G1 用户验证)
    c->gripper_open_pwm = 800; c->gripper_close_pwm = 1700; c->gripper_time_ms = 800;
    // 高度(OpenMV 继承起点)
    c->pick_z = 0.0f; c->approach_z = 70.0f; c->carry_z = 120.0f; c->place_z = 3.0f;
    // 刀口(OpenMV 继承起点)
    c->blade_x = 145.0f; c->blade_y = 75.0f; c->blade_safe_z = 100.0f;
    c->blade_contact_z = 40.0f; c->cut_offset_x = 12.0f; c->cut_times = 2;
    c->valid = false;
    c->magic = ARMCAL_MAGIC;
}

esp_err_t armcal_load(armcal_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    armcal_defaults(out);
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READONLY, &h);
    if (e != ESP_OK) return ESP_ERR_NVS_NOT_FOUND;
    armcal_t tmp;
    size_t sz = sizeof(tmp);
    e = nvs_get_blob(h, KEY, &tmp, &sz);
    nvs_close(h);
    if (e != ESP_OK || sz != sizeof(tmp) || tmp.magic != ARMCAL_MAGIC) {
        ESP_LOGW(TAG, "无有效标定,用默认(valid=false)");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = tmp;
    ESP_LOGI(TAG, "标定已加载 valid=%d", out->valid);
    return ESP_OK;
}

esp_err_t armcal_save(const armcal_t *c)
{
    if (!c) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_blob(h, KEY, c, sizeof(*c));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "标定已保存 e=%d valid=%d", e, c->valid);
    return e;
}
```

- [ ] **Step 3: 写 CMakeLists**

`components/armcal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "armcal.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash
)
```

- [ ] **Step 4: build 验证**

Run: `mcp__idf-bridge__build`（先把 armcal 加进某个 REQUIRES 才会编译——本步临时加到 main 的 REQUIRES 验证）。修改 `main/CMakeLists.txt` REQUIRES 追加 `armcal`，build。
Expected: build 绿。

- [ ] **Step 5: 提交**

```bash
git add components/armcal main/CMakeLists.txt
git commit -m "feat(armcal): 机械臂标定NVS存储(H/连杆/观察位/腕/夹爪/刀口)"
```

---

## Task 6: px→mm 单应性应用（host TDD）

**Files:**
- Create: `components/armcal/homography.c`（纯 C，host 可测）
- Modify: `components/armcal/include/armcal.h`（加声明）
- Modify: `components/armcal/CMakeLists.txt`（加 homography.c）
- Test: `components/armcal/test/test_homography.c`（host gcc）

**Interfaces:**
- Consumes: 无。
- Produces:
  - `void homography_apply(const float H[9], float px, float py, float *mm_x, float *mm_y)` — 单应性变换像素→mm。
  - `float homography_angle(const float H[9], float img_angle_deg)` — 图像长轴角→世界角（用 H 的 2x2 线性部分变换方向向量后取 atan2）。

- [ ] **Step 1: 加声明到 armcal.h**（在 `armcal_save` 声明后）

```c
// 单应性像素->台面mm。H 行主序 9 元。
void homography_apply(const float H[9], float px, float py, float *mm_x, float *mm_y);
// 图像长轴角(deg)->世界角(deg): 用 H 的 2x2 线性部分变换方向向量。
float homography_angle(const float H[9], float img_angle_deg);
```

- [ ] **Step 2: 写失败测试**

`components/armcal/test/test_homography.c`:

```c
#include "armcal.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)
#define NEAR(a,b) (fabs((a)-(b)) < 0.01)

int main(void) {
    float mx, my;
    // 单位阵: 点映射到自身
    float I[9] = {1,0,0, 0,1,0, 0,0,1};
    homography_apply(I, 100, 80, &mx, &my);
    CHECK(NEAR(mx,100) && NEAR(my,80), "identity");

    // 缩放+平移: mx=0.5*px+10, my=0.5*py+5
    float S[9] = {0.5f,0,10, 0,0.5f,5, 0,0,1};
    homography_apply(S, 100, 80, &mx, &my);
    CHECK(NEAR(mx,60) && NEAR(my,45), "scale+translate");

    // 角度: 单位阵下角度不变
    CHECK(NEAR(homography_angle(I, 30), 30), "angle identity");
    // 纯缩放(各向同性)角度不变
    CHECK(NEAR(homography_angle(S, 30), 30), "angle isotropic scale");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: 运行确认失败**

Run:
```bash
gcc components/armcal/test/test_homography.c components/armcal/homography.c -Icomponents/armcal/include -lm -o /tmp/th 2>&1 | head
```
Expected: 编译失败（homography.c 不存在）。

- [ ] **Step 4: 写实现**

`components/armcal/homography.c`:

```c
#include "armcal.h"
#include <math.h>

void homography_apply(const float H[9], float px, float py, float *mm_x, float *mm_y)
{
    float w = H[6] * px + H[7] * py + H[8];
    if (w == 0.0f) w = 1e-6f;
    if (mm_x) *mm_x = (H[0] * px + H[1] * py + H[2]) / w;
    if (mm_y) *mm_y = (H[3] * px + H[4] * py + H[5]) / w;
}

float homography_angle(const float H[9], float img_angle_deg)
{
    // 方向向量(cos,sin)过 2x2 线性部分,取变换后角度
    float rad = img_angle_deg * (float)M_PI / 180.0f;
    float dx = cosf(rad), dy = sinf(rad);
    float wx = H[0] * dx + H[1] * dy;
    float wy = H[3] * dx + H[4] * dy;
    float a = atan2f(wy, wx) * 180.0f / (float)M_PI;
    while (a >= 180.0f) a -= 180.0f;
    while (a < 0.0f) a += 180.0f;
    return a;
}
```

- [ ] **Step 5: 运行确认通过**

Run:
```bash
gcc components/armcal/test/test_homography.c components/armcal/homography.c -Icomponents/armcal/include -lm -o /tmp/th && /tmp/th
```
Expected: `ALL PASS`。

- [ ] **Step 6: 加入构建**

修改 `components/armcal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "armcal.c" "homography.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash
)
```

- [ ] **Step 7: 提交**

```bash
git add components/armcal
git commit -m "feat(armcal): 单应性 px->mm 应用 + 角度变换(host测试)"
```

---

## Task 7: `/arm_calib` 端点 + PC 端标定脚本

**Files:**
- Modify: `components/net/http_srv.c`（加 `/arm_calib` handler + 注册）
- Modify: `components/net/CMakeLists.txt`（PRIV_REQUIRES 加 armcal）
- Create: `scripts/calibrate_homography.py`（PC 端：读 /detect 采点 + 求 H + POST /arm_calib）

**Interfaces:**
- Consumes: `armcal_load`/`armcal_save`（Task 5）、`homography_apply`（Task 6）。
- Produces: `/arm_calib`（GET 查询当前标定 JSON；`POST` body 为 H 的 9 个 float + 观察位，写 NVS 并置 valid=true）。

- [ ] **Step 1: http_srv.c 加 arm_calib handler**

在 `#include "armlink.h"` 后加 `#include "armcal.h"`。在 `arm_auto_get` 之后加：

```c
// 标定: GET 查询当前; POST body="H0,H1,...,H8" 写入并置 valid。
static esp_err_t arm_calib_get(httpd_req_t *req)
{
    armcal_t c;
    armcal_load(&c);

    if (req->method == HTTP_POST) {
        char body[256];
        int total = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
        int got = 0;
        while (got < total) {
            int r = httpd_req_recv(req, body + got, total - got);
            if (r <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
            got += r;
        }
        body[got] = '\0';
        float h[9];
        int nparsed = sscanf(body, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
            &h[0],&h[1],&h[2],&h[3],&h[4],&h[5],&h[6],&h[7],&h[8]);
        if (nparsed != 9) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need 9 floats"); return ESP_FAIL; }
        for (int i = 0; i < 9; i++) c.H[i] = h[i];
        c.valid = true;
        esp_err_t e = armcal_save(&c);
        char ob[64];
        int n = snprintf(ob, sizeof(ob), "{\"saved\":%s}", e == ESP_OK ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, ob, n);
    }

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
        "{\"valid\":%s,\"H\":[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f],"
        "\"observe\":[%.1f,%.1f,%.1f]}",
        c.valid ? "true" : "false",
        c.H[0],c.H[1],c.H[2],c.H[3],c.H[4],c.H[5],c.H[6],c.H[7],c.H[8],
        c.observe_x, c.observe_y, c.observe_z);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}
```

在 `net_http_start` 的 arm_auto 注册后加（注册 GET+POST 两个方法）：

```c
    httpd_uri_t calib_g = { .uri = "/arm_calib", .method = HTTP_GET,  .handler = arm_calib_get };
    httpd_register_uri_handler(server, &calib_g);
    httpd_uri_t calib_p = { .uri = "/arm_calib", .method = HTTP_POST, .handler = arm_calib_get };
    httpd_register_uri_handler(server, &calib_p);
```

- [ ] **Step 2: net CMakeLists 加 armcal 依赖**

修改 `components/net/CMakeLists.txt` 的 PRIV_REQUIRES，追加 `armcal`：

```cmake
idf_component_register(SRCS "wifi_ap.c" "http_srv.c" "stream_srv.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server camera ai armlink armcal)
```

- [ ] **Step 3: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿。

- [ ] **Step 4: 写 PC 端标定脚本**

`scripts/calibrate_homography.py`:

```python
"""单应性标定: 在台面已知mm位置摆标定点(垫高9mm到18650中轴面),
逐点从板子 /detect 读像素中心, 求 H(px->mm), POST 到 /arm_calib 写入 NVS。
用法: 交互式, 按提示在每个已知点放一颗电池, 回车采集。
依赖: pip install opencv-python numpy requests (在 .venv-tools 里)。"""
import sys, time, json
import numpy as np
import requests

BASE = "http://192.168.4.1"
# 台面已知点(mm), 与实际摆放一致; >=6 点最小二乘更稳。按你工作台改。
KNOWN_MM = [
    (-60, 80), (0, 80), (60, 80),
    (-60, 130), (0, 130), (60, 130),
    (-40, 105), (40, 105),
]

def read_center():
    r = requests.get(BASE + "/detect", timeout=3).json()
    if r.get("n", 0) < 1:
        return None
    b = max(r["boxes"], key=lambda x: x["s"])  # 最高分框
    return ((b["x1"] + b["x2"]) / 2.0, (b["y1"] + b["y2"]) / 2.0)

def main():
    px_pts, mm_pts = [], []
    for (mx, my) in KNOWN_MM:
        input(f"把电池放到台面 ({mx},{my})mm 处(垫高9mm), 回车采集...")
        c = None
        for _ in range(10):
            c = read_center()
            if c: break
            time.sleep(0.2)
        if not c:
            print("  未检测到, 跳过该点"); continue
        print(f"  像素中心 {c}")
        px_pts.append(c); mm_pts.append((mx, my))
    if len(px_pts) < 4:
        print("有效点 < 4, 无法求单应性"); sys.exit(1)
    import cv2
    H, _ = cv2.findHomography(np.array(px_pts, np.float32), np.array(mm_pts, np.float32))
    Hs = ",".join("%.8f" % v for v in H.flatten())
    print("H =", Hs)
    # 残差自检
    for (px, mm) in zip(px_pts, mm_pts):
        v = H @ np.array([px[0], px[1], 1.0])
        est = (v[0]/v[2], v[1]/v[2])
        print(f"  {mm} -> est({est[0]:.1f},{est[1]:.1f}) err {((est[0]-mm[0])**2+(est[1]-mm[1])**2)**0.5:.1f}mm")
    if input("写入板子 NVS? (y/N) ").lower() == "y":
        resp = requests.post(BASE + "/arm_calib", data=Hs, timeout=3)
        print("saved:", resp.text)

if __name__ == "__main__":
    main()
```

- [ ] **Step 5: 提交（代码部分；硬件标定在 G1 后做）**

```bash
git add components/net/http_srv.c components/net/CMakeLists.txt scripts/calibrate_homography.py
git commit -m "feat(net): /arm_calib 端点 + PC端单应性标定脚本"
```

> **硬件步骤（D2，需用户在场，flash 当场确认）**：build→flash→连 SoftAP→摆标定点跑 `calibrate_homography.py`→写 NVS。残差应 ≤ 几 mm。此步不产生 commit（标定值在 NVS）。

---

## Task 8: `components/armctrl` 骨架 + 安全级联锁 + `/arm_grade` `/arm_run`

**Files:**
- Create: `components/armctrl/armctrl.c`
- Create: `components/armctrl/include/armctrl.h`
- Create: `components/armctrl/CMakeLists.txt`
- Modify: `components/net/http_srv.c`（加 `/arm_grade` `/arm_run` + 注册）
- Modify: `components/net/CMakeLists.txt`（REQUIRES 加 armctrl）
- Modify: `main/main.c` + `main/CMakeLists.txt`（init + REQUIRES）

**Interfaces:**
- Consumes: `armcal_load`（T5）、`kin_move_best`/`kin_selftest`（T2/T4）、`armlink_encode_arm_frame`/`armlink_encode_servo_frame`（T3）、`armlink_uart_send`（现有）、`armlink_get_last_target`（现有）。
- Produces:
  - `esp_err_t armctrl_init(void)` — 加载标定、IK 自检、建任务（挂起态）。
  - `void armctrl_set_grade(int g)` / `int armctrl_get_grade(void)` — 安全级 0-4，默认 0。
  - `void armctrl_request_run(bool on)` / `bool armctrl_is_running(void)` — 启停一轮循环，默认关。
  - `esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms)` — 解 IK + 发捆绑帧 + 阻塞等到位。返回 ESP_OK / ESP_ERR_INVALID_ARG(不可达)。
  - `void armctrl_move_servo(int idx, int pwm, int move_ms)` — 单舵机(腕/夹爪)帧 + 等到位。

- [ ] **Step 1: 写头文件**

`components/armctrl/include/armctrl.h`:

```c
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t armctrl_init(void);
void armctrl_set_grade(int g);       // 0..4
int  armctrl_get_grade(void);
void armctrl_request_run(bool on);
bool armctrl_is_running(void);

// 低层运动原语(供状态机与联调)。move_ms 运动时间。
esp_err_t armctrl_move_arm(float x, float y, float z, int move_ms);   // 解IK+发捆绑帧+等到位
void armctrl_move_servo(int idx, int pwm, int move_ms);               // 单舵机帧+等到位

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: 写实现骨架（含联锁 + 运动原语 + 回观察位；状态机 run 先留 T9-T11 填）**

`components/armctrl/armctrl.c`:

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

static const char *TAG = "armctrl";

static armcal_t s_cal;
static volatile int  s_grade = 0;       // 0..4, 默认最保守
static volatile bool s_run = false;     // 抓取循环开关, 默认关
static bool s_ik_ok = false;

#define SETTLE_MS 200   // 步间稳定余量(ms), >= 保证舵机到位

void armctrl_set_grade(int g) { if (g < 0) g = 0; if (g > 4) g = 4; s_grade = g; ESP_LOGW(TAG, "grade=%d", g); }
int  armctrl_get_grade(void) { return s_grade; }
void armctrl_request_run(bool on) { s_run = on; ESP_LOGW(TAG, "run=%d", on); }
bool armctrl_is_running(void) { return s_run; }

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
    armlink_uart_send(frame, len);
    ESP_LOGI(TAG, "arm->(%.0f,%.0f,%.0f) %s", x, y, z, frame);
    vTaskDelay(pdMS_TO_TICKS(mt + SETTLE_MS));
    return ESP_OK;
#else
    ESP_LOGW(TAG, "UART 未启用, 忽略 move_arm");
    return ESP_ERR_INVALID_STATE;
#endif
}

void armctrl_move_servo(int idx, int pwm, int move_ms)
{
#if CONFIG_ARMLINK_UART_ENABLE
    char frame[32];
    int len = armlink_encode_servo_frame(idx, pwm, move_ms, frame, sizeof(frame));
    if (len > 0) armlink_uart_send(frame, len);
    ESP_LOGI(TAG, "servo #%03d -> %d", idx, pwm);
    vTaskDelay(pdMS_TO_TICKS(move_ms + SETTLE_MS));
#endif
}

// 回观察位(先中转点消回差, 再到观察位), 腕中位 + 开爪。
static void go_observe(void)
{
    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 开爪
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 600);                     // 腕中位
    armctrl_move_arm(0, s_cal.observe_y, s_cal.carry_z, 1200);             // 中转(正前高处)
    armctrl_move_arm(s_cal.observe_x, s_cal.observe_y, s_cal.observe_z, 1200);
}

// 状态机主体(T9-T11 逐步填); 本任务只做: 未就绪守卫 + 回观察位 + 占位。
static void armctrl_task(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_run || !s_ik_ok || !s_cal.valid) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        // T9-T11 在此填: 观察→定位→抓→切→放→回观察; 本步先回观察位并停。
        go_observe();
        ESP_LOGI(TAG, "(骨架)已回观察位; 抓取序列待 T9-T11 填");
        s_run = false;   // 骨架跑一次即停
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t armctrl_init(void)
{
    esp_err_t e = armcal_load(&s_cal);   // 无标定则 valid=false
    if (e != ESP_OK) ESP_LOGW(TAG, "标定未就绪(valid=false), 自动模式将被拒绝");
    s_ik_ok = (kin_selftest() == 0);
    if (!s_ik_ok) ESP_LOGE(TAG, "IK 自检失败, 自动模式禁用");
    xTaskCreate(armctrl_task, "armctrl", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "init ok (grade=0,run=off,ik=%d,cal=%d)", s_ik_ok, s_cal.valid);
    return ESP_OK;
}
```

- [ ] **Step 3: 写 CMakeLists**

`components/armctrl/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "armctrl.c"
    INCLUDE_DIRS "include"
    REQUIRES armcal kinematics armlink
    PRIV_REQUIRES driver
)
```

- [ ] **Step 4: net 加 /arm_grade /arm_run 端点**

`components/net/http_srv.c` 加 `#include "armctrl.h"`；在 arm_calib handler 后加：

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

在 net_http_start 的 calib 注册后加：

```c
    httpd_uri_t grade = { .uri = "/arm_grade", .method = HTTP_GET, .handler = arm_grade_get };
    httpd_register_uri_handler(server, &grade);
    httpd_uri_t run = { .uri = "/arm_run", .method = HTTP_GET, .handler = arm_run_get };
    httpd_register_uri_handler(server, &run);
```

- [ ] **Step 5: net + main 依赖 + init**

`components/net/CMakeLists.txt` PRIV_REQUIRES 追加 `armctrl`。
`main/CMakeLists.txt` REQUIRES 追加 `armctrl armcal kinematics`（若前面未全加）。
`main/main.c`：加 `#include "armctrl.h"`；在 `armlink_init();` 后加 `armctrl_init();`。

- [ ] **Step 6: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿。

- [ ] **Step 7: 提交**

```bash
git add components/armctrl components/net main/main.c main/CMakeLists.txt
git commit -m "feat(armctrl): 状态机骨架+安全级联锁+运动原语+/arm_grade /arm_run"
```

> **硬件步骤（D2-D3，需用户在场）**：G0（不接舵机电，逻辑分析仪/串口看捆绑帧字节）→ G1（接电无刀无电池，`/arm_grade?g=1`，命令已知点用 `armctrl_move_arm`，卷尺量末端，误差 ≤5mm；实测连杆写回 armcal 默认或经标定；夹爪 PWM 用户验证）。

---

## Task 9: armctrl 多帧位姿平均 + 抖动门

**Files:**
- Modify: `components/armctrl/armctrl.c`（加位姿采集函数 + 用到状态机）

**Interfaces:**
- Consumes: `armlink_get_last_target`（现有，返回 `arm_target_t` 像素中心+角度）。
- Produces: `static bool acquire_pose(float *px, float *py, float *ang_deg)` — 连续 N 帧读目标缓存，中心/角度抖动超门限返回 false，否则输出均值。

- [ ] **Step 1: 加位姿采集到 armctrl.c（在 go_observe 之前）**

```c
#define POSE_FRAMES 5
#define POSE_INTERVAL_MS 60
#define POSE_CENTER_RANGE_PX 4.0f
#define POSE_ANGLE_RANGE_DEG 12.0f

// 连续读 N 帧目标缓存, 抖动超门限判失败, 否则输出中心/角度均值。
static bool acquire_pose(float *out_px, float *out_py, float *out_ang)
{
    float cx[POSE_FRAMES], cy[POSE_FRAMES], ang[POSE_FRAMES];
    for (int i = 0; i < POSE_FRAMES; i++) {
        arm_target_t t;
        armlink_get_last_target(&t);
        if (!t.valid) { ESP_LOGW(TAG, "pose: 第%d帧无目标", i); return false; }
        cx[i] = t.center_x_px; cy[i] = t.center_y_px; ang[i] = t.angle_deg;
        vTaskDelay(pdMS_TO_TICKS(POSE_INTERVAL_MS));
    }
    float minx = cx[0], maxx = cx[0], miny = cy[0], maxy = cy[0];
    float mina = ang[0], maxa = ang[0], sx = 0, sy = 0, sa = 0;
    for (int i = 0; i < POSE_FRAMES; i++) {
        if (cx[i] < minx) minx = cx[i]; if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i]; if (cy[i] > maxy) maxy = cy[i];
        if (ang[i] < mina) mina = ang[i]; if (ang[i] > maxa) maxa = ang[i];
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

- [ ] **Step 2: build 验证（函数暂未被调用会告警 unused，先接入状态机再验；本步接入）**

在 `armctrl_task` 的 `go_observe();` 后、`s_run=false;` 前，替换占位为：

```c
        go_observe();
        float px, py, ang;
        if (!acquire_pose(&px, &py, &ang)) {
            ESP_LOGW(TAG, "位姿不稳, 重试"); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        float mm_x, mm_y;
        homography_apply(s_cal.H, px, py, &mm_x, &mm_y);
        float world_ang = homography_angle(s_cal.H, ang);
        ESP_LOGI(TAG, "定位: px(%.1f,%.1f)->mm(%.1f,%.1f) angW=%.1f", px, py, mm_x, mm_y, world_ang);
        // T10 在此填抓取序列; 本步先打印定位并停。
        s_run = false;
```

加 `#include` 无需改（armcal.h 已含 homography 声明，armctrl.c 已 include armcal.h）。

- [ ] **Step 3: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿。

- [ ] **Step 4: 提交**

```bash
git add components/armctrl/armctrl.c
git commit -m "feat(armctrl): 多帧位姿平均+抖动门+单应性定位接入状态机"
```

---

## Task 10: armctrl 抓取序列（接近/悬停/下降/夹/抬）

**Files:**
- Modify: `components/armctrl/armctrl.c`（加 pick_sequence + 腕角对齐 + 接入状态机）

**Interfaces:**
- Consumes: `armctrl_move_arm`/`armctrl_move_servo`（T8）、标定 `s_cal`（T5）、定位结果（T9）。
- Produces: `static esp_err_t pick_sequence(float mm_x, float mm_y, float world_ang)` — 悬停→预降→腕对齐→下降→夹→抬。返回 ESP_OK / 失败。

- [ ] **Step 1: 加腕角映射 + 抓取序列（在 acquire_pose 之后）**

```c
// 世界长轴角 -> 腕#004 PWM。抓取时腕轴需对齐电池长轴(垂直于长轴夹取)。
// 注: spec §4.4 的"减去抓取点方位角(底座旋转)"耦合在此折进经验 wrist_zero_deg —
// 与 OpenMV 参考(get_grip_angle_deg 用经验偏移,不显式减方位角)一致,G1 标定 wrist_zero_deg 时一并吸收。
static int wrist_pwm_for_angle(float world_ang_deg)
{
    // 归一化到 [-90,90]
    float a = world_ang_deg + s_cal.wrist_zero_deg;
    while (a >= 90.0f) a -= 180.0f;
    while (a < -90.0f) a += 180.0f;
    int pwm = s_cal.wrist_center_pwm + (int)(a * s_cal.wrist_k);
    return armlink_clamp_pwm(pwm);
}

// 抓取序列: 正上方悬停(可急停) -> 预降 -> 腕对齐 -> 最终下降 -> 夹 -> 抬到carry。
static esp_err_t pick_sequence(float mm_x, float mm_y, float world_ang)
{
    float pre_z = s_cal.pick_z + 20.0f;   // 预抓高度(pick 上方 20mm)
    if (pre_z > s_cal.approach_z) pre_z = s_cal.approach_z;

    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);   // 确保开爪

    // 1. 目标正上方安全高度悬停(G<=2 时此处人可急停确认)
    if (armctrl_move_arm(mm_x, mm_y, s_cal.approach_z, 1500) != ESP_OK) return ESP_FAIL;
    // 2. 腕对齐长轴
    armctrl_move_servo(4, wrist_pwm_for_angle(world_ang), 800);
    // 3. 预降
    if (armctrl_move_arm(mm_x, mm_y, pre_z, 1200) != ESP_OK) return ESP_FAIL;
    // 4. 最终下降到抓取高度
    if (armctrl_move_arm(mm_x, mm_y, s_cal.pick_z, 1400) != ESP_OK) return ESP_FAIL;
    // 5. 夹爪闭合
    armctrl_move_servo(5, s_cal.gripper_close_pwm, s_cal.gripper_time_ms);
    // 6. 抬起到搬运高度
    if (armctrl_move_arm(mm_x, mm_y, s_cal.carry_z, 1400) != ESP_OK) return ESP_FAIL;
    // 7. 腕回中位(搬运姿态)
    armctrl_move_servo(4, s_cal.wrist_center_pwm, 800);
    ESP_LOGI(TAG, "抓取序列完成");
    return ESP_OK;
}
```

- [ ] **Step 2: 接入状态机（G3 到此为止，不切割）**

在 `armctrl_task` 里，把 T9 的 `// T10 在此填...` 段替换为：

```c
        if (pick_sequence(mm_x, mm_y, world_ang) != ESP_OK) {
            ESP_LOGW(TAG, "抓取失败, 回观察位");
            go_observe(); s_run = false; continue;
        }
        if (s_grade < 4) {
            // G3: 抓起后直接放回原位验证抓取(不切割)
            armctrl_move_arm(mm_x, mm_y, s_cal.place_z, 1400);
            armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);
            go_observe();
            s_run = false;
            continue;
        }
        // T11 在此填 G4 切割+放回; 本步 G4 暂同 G3。
        go_observe();
        s_run = false;
```

- [ ] **Step 3: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿。

- [ ] **Step 4: 提交**

```bash
git add components/armctrl/armctrl.c
git commit -m "feat(armctrl): 抓取序列(悬停/预降/腕对齐/下降/夹/抬)+G3放回验证"
```

> **硬件步骤（D3-D4，需用户在场）**：G2 抓替代物(纸卷/泡沫)≥8/10 → G3 抓真 18650 ≥8/10。不达线回看标定/连杆/腕角 K 调参。

---

## Task 11: armctrl 切割 + 放回 + 完整循环（G4）

**Files:**
- Modify: `components/armctrl/armctrl.c`（加 cut_sequence + place_back + 完整状态机）

**Interfaces:**
- Consumes: `armctrl_move_arm`（T8）、标定刀口坐标（T5）、抓取完成态（T10）。
- Produces: `static esp_err_t cut_sequence(void)`、`static esp_err_t place_back(float mm_x, float mm_y)`。

- [ ] **Step 1: 加切割 + 放回序列（在 pick_sequence 之后）**

```c
// 切割: 移到刀口安全位 -> 下探接触 -> 往复切 cut_times 次 -> 回安全位。
static esp_err_t cut_sequence(void)
{
    float sx = s_cal.blade_x - s_cal.cut_offset_x;
    float ex = s_cal.blade_x + s_cal.cut_offset_x;
    float by = s_cal.blade_y;
    if (armctrl_move_arm(s_cal.blade_x, by, s_cal.blade_safe_z, 1600) != ESP_OK) return ESP_FAIL;
    if (armctrl_move_arm(sx, by, s_cal.blade_contact_z, 1400) != ESP_OK) return ESP_FAIL;
    for (int i = 0; i < s_cal.cut_times; i++) {
        if (armctrl_move_arm(ex, by, s_cal.blade_contact_z, 1000) != ESP_OK) return ESP_FAIL;
        if (i != s_cal.cut_times - 1) {
            if (armctrl_move_arm(sx, by, s_cal.blade_contact_z, 1000) != ESP_OK) return ESP_FAIL;
        }
    }
    if (armctrl_move_arm(s_cal.blade_x, by, s_cal.blade_safe_z, 1600) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "切割完成");
    return ESP_OK;
}

// 放回: 移到放置点上方 -> 下降 -> 开爪 -> 抬起。
static esp_err_t place_back(float mm_x, float mm_y)
{
    if (armctrl_move_arm(mm_x, mm_y, s_cal.carry_z, 1600) != ESP_OK) return ESP_FAIL;
    if (armctrl_move_arm(mm_x, mm_y, s_cal.place_z, 1400) != ESP_OK) return ESP_FAIL;
    armctrl_move_servo(5, s_cal.gripper_open_pwm, s_cal.gripper_time_ms);
    if (armctrl_move_arm(mm_x, mm_y, s_cal.approach_z, 1400) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "放回完成");
    return ESP_OK;
}
```

- [ ] **Step 2: 完成 G4 完整循环**

把 T10 的 `// T11 在此填 G4...` 段（含其后 `go_observe(); s_run=false;`）替换为：

```c
        // G4: 抓起 -> 移刀口切割 -> 放回 -> 回观察位
        if (cut_sequence() != ESP_OK) {
            ESP_LOGW(TAG, "切割失败, 保持夹持回观察位");
            go_observe(); s_run = false; continue;
        }
        if (place_back(mm_x, mm_y) != ESP_OK) {
            ESP_LOGW(TAG, "放回失败");
        }
        go_observe();
        ESP_LOGI(TAG, "完整循环完成");
        s_run = false;   // 单轮; 连续分拣是 bonus, 改为 continue 即连续
```

- [ ] **Step 3: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿。

- [ ] **Step 4: 提交**

```bash
git add components/armctrl/armctrl.c
git commit -m "feat(armctrl): 切割+放回+G4完整抓取切割循环"
```

> **硬件步骤（D5，需用户在场，装刀）**：G4 装刀后全流程。刀口坐标 `blade_x/y` 按实际刀具夹具位置经 `/arm_calib` 或直接改 armcal 默认后重新 flash 校准。完整循环无撞台面即 demo 可拍。

---

## Task 12: 收敛自动路径 + 集成 + 文档/记忆

**Files:**
- Modify: `components/armlink/armlink.c`（`armlink_update_from_ai` 停掉直接发送，只更新目标缓存供 armctrl 消费）
- Modify: `components/net/http_srv.c`（root_get 页面加 grade/run/calib 按钮，可选）
- Modify: `docs/ai/MODEL_PIPELINE.md` 或新增 `docs/ai/ARM_PIPELINE.md`（Phase2 复现记录）
- Modify: `docs/ai/CRASH_SIGNATURES.md`（若联调遇新坑，经 /learn）
- Modify: 记忆 `brain-vision-progress`

**Interfaces:**
- Consumes: 全部前序任务。
- Produces: 单一驱动路径（armctrl 状态机是唯一发运动帧者），armlink 回归纯"选目标+缓存+编码原语"。

- [ ] **Step 1: 中和 armlink 旧自动发送路径**

`components/armlink/armlink.c` 的 `armlink_update_from_ai` 里，`#if CONFIG_ARMLINK_UART_ENABLE ... #endif` 那段自动发送（`if (s_auto_send && t.valid){...armlink_uart_send...}`）**整段删除**——armctrl 现在是唯一驱动者，armlink 只更新 `s_last` 缓存供 `armlink_get_last_target` 读。保留 `s_auto_send`/`armlink_set_auto_send`/`armlink_get_auto_send`（`/arm_auto` 仍可用作"是否允许 armctrl 自动跑"的语义，或后续删；本步保留不发即可）。

具体：删除 armlink.c 中 `armlink_update_from_ai` 函数体末尾这段：
```c
#if CONFIG_ARMLINK_UART_ENABLE
    if (s_auto_send && t.valid) {
        char cmd[64];
        int len =
#if CONFIG_ARMLINK_PROTO_WRIST_SERVO
            armlink_encode_wrist_servo(&t, cmd, sizeof(cmd));
#else
            armlink_encode_kms(&t, cmd, sizeof(cmd));
#endif
        if (len > 0) armlink_uart_send(cmd, len);
    }
#endif
```
替换为注释：
```c
    // 驱动权移交 armctrl 状态机(Phase2): 此处只更新目标缓存, 不再直接发帧。
```

- [ ] **Step 2: build 验证**

Run: `mcp__idf-bridge__build`
Expected: build 绿（`armlink_encode_kms`/`armlink_encode_wrist_servo`/`armlink_send_test_frame` 仍保留，`/arm_test` 仍可用作单舵机联调）。

- [ ] **Step 3: 写 Phase2 复现文档**

Create `docs/ai/ARM_PIPELINE.md`，内容含：架构（三层组件图）、标定流程（观察位→calibrate_homography.py→NVS）、连杆实测值、腕角 K/零位实测值、夹爪 PWM 实测值、刀口坐标、安全级 G0-G4 验收数据（成功率）、已知坑（theta6 bug、$KMS: 无效、视差垫高 9mm、回差）。

- [ ] **Step 4: 更新记忆**

更新 `C:\Users\WJ0706\.claude\projects\D--WJ-jixiebi-WORKplace\memory\brain-vision-progress.md`：Phase 2 完成状态（IK 上板、标定值、G 级验收成功率、下一步）。同步 `MEMORY.md` 索引行。

- [ ] **Step 5: 提交**

```bash
git add components/armlink/armlink.c components/net/http_srv.c docs/ai/ARM_PIPELINE.md docs/ai/CRASH_SIGNATURES.md
git commit -m "feat(phase2): 驱动权收敛armctrl+Phase2复现文档(ARM_PIPELINE)"
```

---

## 验收（整轮 Done 的定义）

- [ ] `kinematics` host 测试 ALL PASS；上板 IK 自检通过（`kin_selftest`=0）。
- [ ] `armlink_frame` host 测试 ALL PASS；G0 捆绑帧字节与预期一致。
- [ ] `armcal` NVS 存取正常；`homography` host 测试 ALL PASS。
- [ ] 单应性标定残差 ≤ 几 mm；G1 定位误差 ≤5mm（卷尺实测，连杆确认）。
- [ ] G2 替代物 ≥8/10；G3 真 18650 ≥8/10（成功率记入 ARM_PIPELINE）。
- [ ] G4 完整循环（识别→抓→切→放→回观察位）无撞台面、无越界 PWM，可连续跑，demo 可拍。
- [ ] build 全绿；每次 flash 当场确认；驱动权单一（armctrl）。
- [ ] ARM_PIPELINE.md + 记忆 `brain-vision-progress` 更新；新坑经 /learn 入 CRASH_SIGNATURES。

## 后手（本轮不做，记录备查）
- 切割后视觉复检（臂带相机看刀口，检测置信度下降判切成）——`go_observe` 机制已可复用去"看刀口位"。
- 多电池连续分拣（状态机末 `s_run=false` 改 `continue` 即可）；ESP-NOW 双向 + Steward 传感器联动；HMI/语音/APP；扩类/异常二分类。
