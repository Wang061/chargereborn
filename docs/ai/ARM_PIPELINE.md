# 机械臂自动抓取-切割 · 复现与运行手册 (ARM_PIPELINE)

> ChargeReborn Phase 2：Brain(ESP32-S3) 用视觉检测 18650 → 逆运动学解算 → 裸协议帧驱动 KM1 机械臂，
> 完成 识别→抓取→切割→放回→回观察位 的闭环。本文是上板复现与操作手册；物理量待 D2+ 硬件 session 实测填表。
> 相关：设计 spec `docs/superpowers/specs/2026-07-02-phase2-ik-autograsp-design.md`、安全 `docs/ai/SAFETY.md`、坑 `docs/ai/CRASH_SIGNATURES.md`。

## 1. 架构与数据流

三层组件，职责单一、可分别 host 单测：

| 层 | 组件 | 职责 | 依赖 |
|---|---|---|---|
| 运动学 | `components/kinematics` | 纯 IK（law-of-cosines 平面 2 连杆 + 底座旋转 + 腕俯仰），无 ESP 依赖 | 无 |
| 状态机 | `components/armctrl` | 观察→定位→抓→切→放 时序 + 安全联锁 + 运动原语 | kinematics / armcal / armlink_frame / armlink |
| 传输 | `components/armlink`(含 `armlink_frame`) | 选最佳电池目标 + 缓存 + 帧编码 + UART sink | armlink_uart |
| 标定 | `components/armcal` | 单应性 px→mm、参数集 NVS 持久化 | nvs |

**数据流（单一驱动路径）**：

```
相机→ai 检测(每帧) → armlink_update_from_ai(选最佳电池框, 只更新 s_last 缓存)
                                   │
            armctrl_task ─ go_observe() 回观察位
                         ─ acquire_pose() 连续 5 帧, 中心抖动>4px 或 角度跨度>12° 判失败重采
                         ─ homography_apply(H) px→台面mm + homography_angle 长轴角→世界角
                         ─ kin_move_best() IK 解 4 舵机 PWM(扫 Alpha 0→-135 取最负可达解)
                         ─ armlink_encode_arm_frame() 裸协议捆绑帧
                                   │
                         armlink_uart_send() → UART1(TX=GPIO1) → KM1 RX2(GPIO41)
```

- **捆绑帧格式**（4 主关节一帧）：`{#000PxxxxTxxxx!#001...!#002...!#003...!}`
  = #000 底座旋转 / #001 大臂 / #002 小臂 / #003 腕俯仰；腕旋转 #004、夹爪 #005 走单舵机帧 `{#0xxPxxxxTxxxx!}`。
- **唯一驱动者 = armctrl 状态机**。Phase 2 起 `armlink_update_from_ai` 只更新目标缓存，**不再直接发帧**（旧的"每帧自动发坐标"路径已中和）。`armlink_send_test_frame`（`/arm_test`）仍保留作单舵机手动联调。
- **⚠ `$KMS:x,y,z,t!` 自解算在真机 KM1 固件上未实现**（COM4 直连实测 sscanf 不匹配、两端无回应，见 `CRASH_SIGNATURES.md`）。**唯一可动的驱动 = 裸协议 `{#idxPpwmTms!}`**；IK 在 Brain 侧算完直接下发各舵机 PWM，不依赖 KM1 自解算。

## 2. 安全模型

### 2.1 四重联锁（全绿才动手）

自动模式（`armctrl_task` 主循环）执行任何运动前，四个条件缺一不可：

1. **编译期** `CONFIG_ARMLINK_UART_ENABLE` —— 未开则所有 `armctrl_move_*` 是空操作（`ESP_ERR_INVALID_STATE`），固件不驱动任何舵机。
2. **运行时 grade**（`/arm_grade?g=`，`s_grade` 0–4，默认 **0**）—— 分级放行动作范围，未达对应级不放行下一级。
3. **运行时 run**（`/arm_run?on=`，`s_run`，默认 **off**）—— 抓取循环总开关；每轮结束自动 `s_run=false`（单轮，防连续乱跑）。
4. **IK 自检 + 标定 valid** —— 启动时 `kin_selftest()` 对拍内嵌 golden PWM，失败 `s_ik_ok=false` 禁用自动模式；`armcal_load` 无有效标定则 `s_cal.valid=false`，**未标定自动模式直接被拒**（守卫 `if (!s_run || !s_ik_ok || !s_cal.valid) continue;`）。

`s_auto_send`（`/arm_auto`）默认关，Phase 2 语义降级为"是否允许 armctrl 自动跑"的意向位（实际驱动权已在状态机）。

### 2.2 安全分级 G0–G4（逐级通过条件）

来源：`SAFETY.md` + 设计 spec §8。**未达对应级不放行下一级动作**；G1 干跑必须**用户在场**（可随时断电）。

| 级 | 内容 | 通过条件（验收线） |
|---|---|---|
| **G0** | 不接舵机电，仅日志/逻辑分析仪核对捆绑帧字节序 | 帧字节与预期一致 |
| **G1** | 接电、**无刀无电池**、慢速（`move_ms×1.5`）、单关节小幅 → IK 已知点，卷尺量末端 | 定位误差 ≤ 5mm；连杆常量 + 腕角 K 复核完成 |
| **G2** | 标定后抓**替代物**（纸卷/泡沫圆柱），10 次 | ≥ 8/10 成功夹起 |
| **G3** | 抓真 18650（**不切割**：抓起→放回原位），10 次 | ≥ 8/10 |
| **G4** | 装刀，全流程含切割 | 完整循环 demo 可拍、无撞台面、无越界 PWM |

代码里 `s_grade <= 1` 触发慢速；`s_grade < 4` 走 G3 抓放验证分支、`s_grade == 4` 才进 `cut_sequence()`。

### 2.3 失败回退：绝不在刀口旁松爪

- **切割失败** → **保持夹持撤离**：先尽力撤到 `blade_safe_z`（返回值有意忽略，撤离优先），再 `go_observe_ex(false)` **不开爪** 回观察位，半切开的电池夹着等人工取回。
- **抓取/放回失败** → `go_observe()` 正常开爪回观察位、`s_run=false`。
- 复位默认安全位：腕中位 #004=1500、开爪 #005=开、抬到 `carry_z`。急停 = 物理断动力电（首选）/ 软件停循环。

## 3. 标定流程（操作手册级）

### 3.1 观察位

- 定义：正前方 (`observe_x`,`observe_y`,`observe_z`) 高处俯视电池台面（起点 0,130,120 mm）。
- 要求：每轮**从同一方向进入并停稳**（先中转点 `(0, observe_y, carry_z)` 再到观察位，消舵机回差）；`acquire_pose` 连读 5 帧，抖动超门限（中心 4px / 角度 12°）就不动手、重采。

### 3.2 单应性标定（px→台面 mm）

1. **垫高 9mm**：标定点（电池）垫到 **18650 中轴面**高度（≈9mm），使标定平面与实际抓取时电池中轴共面，**消视差**——否则俯视投影有系统偏移。
2. 板子进 SoftAP（`192.168.4.1`），`/detect` 出框。
3. 跑 `scripts/calibrate_homography.py`（`.venv-tools`：`pip install opencv-python numpy requests`）：
   - 交互式，按提示把电池摆到台面 8 个已知 mm 点（`KNOWN_MM`，按实际工作台改，≥6 点最小二乘更稳），逐点回车采集像素中心（取最高分框）。
   - `cv2.findHomography` 求 H → **残差自检**：逐点打印 `est(mm) err(mm)`，残差应 ≤ 几 mm，超了检查垫高/摆位/点数。
   - 确认后 `POST /arm_calib`（body = `H0,H1,...,H8` 九个浮点）→ 板子 `armcal_save` 写 NVS，置 `valid=true`。
4. `GET /arm_calib` 可回查当前 H 与观察位、`valid` 状态。**未标定（valid=false）自动模式拒绝启动**。

## 4. 待实测参数表（物理量，D2+ 硬件 session 填）

下表值只能上板实测，非 TODO 偷懒——现值为起点/继承值，"状态"列标由哪个 G 级产出后覆盖 NVS。

| 参数 | 现值（起点） | 待实测目标 | 由哪级产出 / 状态 |
|---|---|---|---|
| 连杆 L0 | 100 mm | 卷尺实测 | 待实测(G1) |
| 连杆 L1 | 105 mm | 卷尺实测 | 待实测(G1) |
| 连杆 L2 | 75 mm | **两源冲突** `.ino`=75 / OpenMV=88，卷尺裁决 | 待实测(G1) |
| 连杆 L3 | 180 mm | **两源冲突** `.ino`=180 / OpenMV=155，卷尺裁决 | 待实测(G1) |
| 腕 #004 `wrist_k` | 5.6 pwm/deg | 干跑复核 | 待实测(G1) |
| 腕 #004 `wrist_zero_deg` | 0 | 干跑复核（吸收方位角耦合） | 待实测(G1) |
| 腕中位 `wrist_center_pwm` | 1500 us | 复核 | 待实测(G1) |
| 夹爪 #005 开 / 合 | 800 / 1700 us | **用户手上验证**（"能动但不知 PWM"） | 待实测(G1) |
| 抓取高度 pick/approach/carry/place_z | 0 / 70 / 120 / 3 mm | 台面实测微调 | 待实测(G1–G3) |
| 观察位 x/y/z | 0 / 130 / 120 mm | 实测调 | 待实测(G1) |
| 单应性 H | 单位阵（未标定） | 标定采点求解，残差 ≤ 几 mm | 待标定 |
| 刀口 blade_x / blade_y | 145 / 75 mm | 装刀后实测 | 待实测(G4) |
| 刀口 safe_z / contact_z / cut_offset_x / cut_times | 100 / 40 / 12 mm / 2 | 装刀后实测 | 待实测(G4) |
| G1 定位误差 | — | ≤ 5mm（卷尺闭环） | G1 验收线 |
| G2 替代物成功率 | — | ≥ 8/10 | G2 验收线 |
| G3 真 18650 成功率 | — | ≥ 8/10 | G3 验收线 |

## 5. 已知坑（照抄事实，防回归）

- **OpenMV theta6 放大 1.5× bug（已修）**：OpenMV 版底座旋转角 弧度→度 误用 `atan2(x,y)*270/π`（应为 `*180/π`），放大 270/180 = **1.5×**。本项目 `kin_solve` 用 `*180/π`；`kin_selftest` 内嵌 Anchor B（x=120,y=120,z=40 期望 pwm0=1166，bug 会得 1000）**对拍防回归**，失败即拒绝进自动模式。
- **`$KMS:` 真机无效**：KM1 固件未实现 `$KMS:x,y,z,t!` 自解算 → 唯一驱动是裸协议帧（见 §1、CRASH_SIGNATURES）。
- **视差**：标定点必须垫高 9mm 到 18650 中轴面，否则 px→mm 有系统偏移（见 §3.2）。
- **舵机回差**：进观察位/关键位必须**同方向进入 + 中转点**过渡，否则重复定位有回差散布。
- **httpd `max_uri_handlers` 默认 8 不够**：已注册 11 个 URI（root/status/capture/detect/arm_*），现设 **16**；`stack_size` 默认 4096 会被 `detect_get` 的 `buf[1536]+ai_result_t` 撑爆，现设 **8192**（见 CRASH_SIGNATURES）。
- **`-Werror=misleading-indentation`**：工程开了该告警即错，多语句紧跟 `if`/`for` 后缩进必须规范，否则 build 红。

## 6. HTTP 端点速查（SoftAP `192.168.4.1`）

| 端点 | 方法 | 作用 | 返回 |
|---|---|---|---|
| `/detect` | GET | 读最近检测缓存（不触发推理，handler 轻） | `{w,h,infer_ms,n,boxes[...]}` |
| `/arm_target` | GET | 读最近机械臂目标缓存（选中的最佳电池位姿） | 目标 JSON |
| `/arm_calib` | GET | 查当前 H + 观察位 + valid | `{valid,H[9],observe[3]}` |
| `/arm_calib` | POST | body=`H0,...,H8` 九浮点，写 NVS 置 valid | `{saved:bool}` |
| `/arm_grade` | GET | `?g=0..4` 设安全级；无参查询 | `{grade:N}` |
| `/arm_run` | GET | `?on=1|0` 启停一轮抓取循环 | `{running:bool}` |
| `/arm_test` | GET | 手动发裸腕舵机 #004 测试序列（中位→摆→回中，含 ~1.4s 阻塞） | `{sent:bool,err}` |
| `/arm_auto` | GET | `?on=1|0` 自动发送意向位；无参查询 | `{auto_send:bool}` |

操作页（`/` root）已加 **G 级下拉 + 抓取启动/停止** 按钮，直连 `/arm_grade`、`/arm_run`。
