# 设计：正式提交收尾 — 冗余清除 + 卡尔曼目标跟踪 + 状态机终态化

> **生成**：2026-07-06 深夜（07-07 凌晨定稿）
> **前置**：`2026-07-02-phase2-ik-autograsp-design.md`（Phase2 架构）、`docs/ai/ARM_PIPELINE.md`（运行手册）、
> `docs/ai/CONTEST_REQUIREMENTS.md`（投稿要求）、`logs/monitor/20260706_204004_g1_retry2.log`（根因证据）。
> **硬约束**：投稿截止 **2026-07-09 18:00**；单应性 H 已标定入 NVS —— **armcal_t 结构与 ARMCAL_MAGIC 不许动**（动了 NVS blob 失配，H 丢失需重标）。
> **用户已拍板**：方案 A（原地精修 + armlink 内建跟踪器）；演示形态 = 单轮含切割 + 连续开关 + 急停；
> dashboard 固件侧只埋钩子、WS 实现交队友（本轮交付计划书）；host 测试 / `/capture` / `reference/` 保留、`/arm_test` 删；
> 分区收缩放最后一次定稿烧录。

---

## 0. 一句话

删光 bring-up 脚手架（G0-G4 分级、$KMS 死协议、两个备用模型组件），把识别层升级为**恒位置卡尔曼跟踪器**（一次治掉：运动帧污染、置信度闪烁、类别翻转框跳、底部误检四种病），状态机收敛为"单轮含切割 + 连续开关 + 急停"的正式演示形态，预埋 dashboard 事件钩子并交队友对接计划书，最后收缩分区表定稿提交。

## 1. 背景：2026-07-06 晚 G1 重试失败的根因（已诊断，本轮根治）

当晚三次抓取尝试全部卡死在 `位姿不稳,重试` 循环（中心抖动 y 向 44.5 / 82.5 / 123.5px，x 向仅 7-17px）。机制四条：

1. **第 0 帧吃旧帧**：`acquire_pose` 第 0 帧只要缓存 valid 就收、不查新鲜度。相机在臂上，臂刚回观察位时缓存里是**回位途中拍的帧**（俯仰视角差大 → y 爆抖）。日志铁证：运动中检出 y≈317 的框、停稳后真框 y≈182，差 123px 与门限报数吻合。
2. **发布延迟漏网**：推理 ~220-290ms，运动结束前抓取的帧在结束后才写进缓存，`frame_id 变化`检查挡不住，第 1-2 帧同样可能被污染。
3. **量化模型置信度闪烁**：同一颗电池 score 0.12↔0.88 跳变；ai 层出框门限 0.40（`espdet4_detect.hpp default_score_thr`）导致低分帧目标整帧消失 → 有效帧率骤降。
4. **类别翻转 + 底部误检**：AA↔9V 翻转时框宽差 ~70px（几何跳变）；画面底部持续 0.12-0.27 分宽扁误检（疑似夹爪/台边入镜）。"每帧独立选最高分"策略在分数接近时脆弱。

门限 4px→12px（commit 7b0777a）是治标：12px 挡不住 123px 运动帧，反而放进更多噪声。
**附**：coredump 分区为空（0xffff，从未 panic）；检测实际 ~3.5Hz（main 日志 1Hz 是限速造成的错觉）；底盘健康。

## 2. 范围

**做**：
1. 删除清单（§3）全量执行，每批删完立即 build。
2. `armlink` 内建纯 C 目标跟踪器（§4）+ host 单测对拍（当晚日志场景做成永久测试向量）。
3. armctrl 状态机终态化（§5）：去 G 化、单轮含切、连续开关、急停（$DST!）、连续模式防重抓。
4. net 端点收敛（§6）；armctrl 事件钩子 + 统计计数（§7）。
5. 《dashboard 对接计划书》`docs/ai/DASHBOARD_INTEGRATION.md`（§7）。
6. 文档终态化 + merge master + 分区收缩（最后）+ tag（§9）。

**不做**（两份调研裁决后明确排除，防华而不实）：
- **Brain 侧轨迹插值/缓动**——KM1 `move_time` 自带匀速插值（LX 总线舵机族语义，调研证实）；实测出现可见抖动才考虑梯形/min-jerk（备胎：EFeru/MotionGenerator 解析式梯形、quintic 闭式解 5 行）。
- **UART 协议 CRC / IS_MOVING 忙碌查询**（pymycobot 范式虽好）——链路单向 TX-only、KM1 固件不可改；盲等 `move_ms+SETTLE_MS` 是唯一可行且已验证方案。
- **PID 视觉伺服兜底**（TSO_project 路线）——H 已标定且过残差自检，绝对定位为主线。
- **SORT 式恒速(CV) KF / 多目标匈牙利关联**——低帧率+静止目标下速度状态是纯害处：检测噪声灌进速度项、丢检滑行往错误方向外推几帧就漂出门限（OC-SORT 论文公认失效模式）。单目标门限关联足够。
- WebRTC；WS 服务实现（队友按计划书做，视频用协议里的 `video_frame` base64 回退，dashboard 已内置显示逻辑）。

## 3. 删除清单（精确到符号，已全仓 grep 核实引用闭合）

| 位置 | 删 | 保 |
|---|---|---|
| `components/battery_yolo/` | 整目录（YOLOv8n 实测 5.8s/帧已否决；git 历史留档） | — |
| `components/battery_detect/` | 整目录（旧单类，被 detect4 全面取代） | — |
| `components/ai` | Kconfig `AI_DETECTOR` 三选一 choice、ai.cpp 中 espdet/yolo `#if` 分支、CMake `REQUIRES` 收敛为 `battery_detect4` | detect4 路径、结构张量角度估计、跨类去重、边缘框过滤 |
| `components/armlink` | `armlink_encode_kms`、Kconfig `ARMLINK_PROTO` choice、`s_auto_send`+`armlink_set/get_auto_send`、`armlink_send_test_frame`+static `armlink_send_wrist_pwm`（仅被 test_frame 引用，删除集闭合） | `armlink_frame` 编码器、`armlink_uart`、选目标逻辑（升级进跟踪器 §4） |
| `components/armctrl` | `s_grade`+`armctrl_set/get_grade`、G0 干跑分支、`×1.5` 慢速、armctrl_task 里 G3/G4 分支（统一为完整循环） | **"未标定绝不发字节"原语层联锁、`kin_selftest` 门、切割失败保持夹持撤离、run 默认 off** |
| `components/net` | `arm_test_get`、`arm_auto_get`、`arm_grade_get` 三个 handler + URI 注册 + root 页 G 级下拉 | 其余端点 + root 页（按钮改造 §6） |
| `sdkconfig.defaults` | 旧模型三行（`CONFIG_ESPDET_DETECT_MODEL_IN_FLASH_RODATA` / `CONFIG_FLASH_ESPDET_PICO_224_224_BATTERY` / `CONFIG_ESPDET_PICO_224_224_BATTERY`）、`CONFIG_ARMLINK_PROTO_KMS` | 其余；补 detect4 显式三行（rodata 路线，注释沿用） |
| 文档 | ARM_PIPELINE 的 G 级语义段落（重写为 运行/急停 语义） | 标定手册、坑清单（历史事实保留） |
| **用户拍板保留** | — | host 测试×3 + 新增 `test_track`、`/capture` 采集页、`reference/`、`scripts/` |

自查线：删完 `grep -r "s_grade\|KMS\|auto_send\|battery_yolo\|ESPDET_PICO_224_224_BATTERY\b"` 代码零残留（文档叙述除外）。
**注**：备用模型本就不在当前 app 二进制里（rodata 686KB 只含 detect4 486KB，size 实测），此步收益是仓库/配置/菜单复杂度，芯片层收益在 §9 分区收缩。

## 4. 目标跟踪器 `components/armlink/target_track.{c,h}`（纯 C、无 ESP 依赖、静态分配、~150 行核心）

### 4.1 滤波核：恒位置（随机游走）标量 KF

调研结论：**每通道一维标量 KF = "增益自适应 EMA + 协方差记账"**，比裸 EMA 多出的恰是我们需要的：不确定度 P（稳定判据）与新息 ν（关联门限）。每帧每通道 <10 FLOP，S3 上开销可忽略。

- 通道：`cx`、`cy`（像素）；**角度用 (cos2θ, sin2θ) 双通道**（解轴角 mod-180° 回绕，输出 `atan2(s,c)/2`，滤波路径永走短弧）；`w`、`h`（λ 减半，仅供门限归一与显示）。
- 每通道两标量 (x, P)：
  - predict：`P += q·dt`（x 不变；**dt = 实测相邻取帧间隔秒**，不假设固定帧率——实测 3.5Hz 会随 WiFi 负载浮动）
  - update：`S = P + R; ν = z − x; K = P/S; x += K·ν; P *= (1−K);`
- 状态全程 float，仅输出时取整（中途取整会引入 ±0.5px 锯齿）。
- 滤波在 letterbox 反变换后的**最终像素系**进行（与 /detect、单应性同一坐标系）。

### 4.2 参数（起点值 + 上板实标流程）

| 参数 | 起点 | 说明 |
|---|---|---|
| R_pos (cx,cy) | 4 px²（σ≈2px） | **必须上板实标**：静止电池录 ≥100 帧，样本 std² 定 R |
| R_wh | 9 px² | 同上 |
| R_ang | 0.011（单位向量分量域，σ≈3°→2θ σ≈6° 折算） | 同上 |
| λ = q/R | **0.06**（唯一主旋钮） | 稳态增益速查：λ=0.01→K≈0.10（稳/钝）、**0.06→K≈0.22（起点）**、0.25→K≈0.39（快/抖）。嫌抖减半、嫌钝加倍 |
| P0 | 10·R | 首检初始化，前几帧快收敛 |

稳态校验：K=0.22、R=4 时滤后 σ≈√(K·R)≈0.9px——上板实测滤后中心 std 应 ≤1.2px，超了说明 R 标错或有关联污染。
**二期可选（本轮不做）**：NSA 按 score 缩 R（`R̃=max(1−score,0.05)·R`，StrongSORT）——采用时必须同步放大基准 R（R_base=σ²/(1−c̄)），否则比不加还抖。

### 4.3 关联（单轨迹、无匈牙利）

1. **双分数门限**（ByteTrack 思想，治置信度闪烁）：**新建轨迹 score ≥ 0.40**（沿用 `ARMLINK_MIN_SCORE`）；**门内更新放宽到 ≥ 0.25**。配套改动：`espdet4_detect.hpp default_score_thr` 0.40→**0.25**（低分框只喂跟踪器）；`/detect` 叠加只画 ≥0.40 原始框 + 滤波后目标框（演示画面保持干净）。
2. 空间门限（同时满足）：中心距 ≤ `max(0.5·√(w·h), 20px)`；每轴新息 `|ν| ≤ 3·√(P+R)`（滑行后 P 大、门自动放宽，KF 白送的自适应）。
3. 门内多候选取 score 最高；`anisotropy < 0.10`（沿用 `ARMLINK_MIN_ANISOTROPY`）的帧**只更新位置不更新角度**。
4. 门内零候选 → 丢检滑行（§4.4）；被拒候选进"重锁缓冲"（§4.4 挪动判定用）。

### 4.4 滑行 / 丢锁 / 重锁

- 滑行：恒位置保持 + P 膨胀（predict 照跑），输出带 `coasting` 标志——**armctrl 不得据滑行值开始新动作**。
- **max_age = 1.2s（按时间不按帧数）**：超时 → LOST，清状态回捕获。
- `min_hits = 3` 连续命中才 CONFIRMED，确认前不输出目标（**不学 SORT 的启动例外**——对臂控是坑）。
- 被挪动重锁：连续 3 帧门外但彼此聚集（两两中心距 <20px）→ 硬重置到新位置，重走 min_hits。
- 初始化排除区：`track_set_exclusions(pts[], n)`（连续模式防重抓，§5）——落在任一排除点 60px 半径内的候选**不得用于新建轨迹**（已确认轨迹的门内更新不受影响）。

### 4.5 运动联锁（根治 §1 时序 bug，本设计安全核心）

- `ai_result_t` 增加 `int64_t capture_ts_us`——**camera 拿到 fb 的时刻**打戳（不是推理完成时刻）。
- 跟踪器维护 `gate_ts`：**`capture_ts_us < gate_ts` 的检测一律不进滤波器**（连"运动结束前拍、结束后发布"的漏网帧一起治掉）。
- API：`track_suspend()`——armctrl 抓取序列第一个运动原语起挂起；`track_resume()`——armctrl 回观察位停稳后调用，= **硬重置**（清估计、UNCONFIRMED、`gate_ts=now`）。重锁成本 ~1s（3 帧 @3.5Hz），换零陈旧状态风险——用户"确保一定对"要求下，宁可慢一秒。

### 4.6 稳定判据（armctrl 侧 `acquire_pose` 重写，**函数签名不变**）

STABLE 判定（全部满足）：CONFIRMED ∧ 滑窗 5 帧 miss≤1（当前帧必须命中）∧ 每帧 `|ν_cx|,|ν_cy| ≤ 3px` ∧ `√P_cx,√P_cy ≤ 2px` ∧ 窗口滤后中心极差 ≤4px ∧ 角度极差 ≤5°。
滞回：连续 5 帧满足才置 STABLE、连续 2 帧不满足才退出（防标志颤振）。
`acquire_pose` = 等 STABLE（超时 8s 返回 false）→ 读滤波估计。旧"5 帧极差 12px/12°"逻辑整体退役。
（NIS/χ² 学术版判据不用——gate 后数据有选择偏置、表值偏乐观；像素阈值好调好查，上板日志实标。）

### 4.7 host 测试 `components/armlink/test/test_track.c`（"确保一定对"的落点）

测试向量直接取自 2026-07-06 晚真实日志 + 合成：
1. **运动帧拒收**：capture_ts 早于 gate 的 y=317 帧不得进滤波（当晚 123px 案例复现）。
2. **类别翻转框跳**：AA[285,152,450,212]↔9V[282,145,391,208] 交替，滤后中心跳变 ≤3px。
3. **置信度闪烁**：score 0.73→0.12→0.73（0.12 帧按丢检），轨迹不断、STABLE 不颤振。
4. **误检抗性**：底部 [8,417,277,471]@0.27 持续存在，不得劫持轨迹（门限拒之）。
5. 稳态收敛：σ=2px 高斯噪声 30 帧，滤后 std ≤1.2px、STABLE ≤10 帧建立。
6. 滑行→1.2s 超时丢锁→重捕获；目标挪 100px → 3 帧硬重锁。
7. 角度回绕：178°↔2° 交替，滤波输出走短弧、不得穿越 90°。

### 4.8 armlink 集成

`armlink_update_from_ai` 重写为：逐框喂 `track_update(boxes, count, capture_ts)` → `s_last` 从跟踪器输出派生（含 coasting/hits/STABLE 元数据），`frame_id` 语义保留。`/arm_target` 与 armctrl 的读取接口不变。

## 5. 状态机终态（armctrl）

```
IDLE ─(run)→ OBSERVE(go_observe + track_resume) → ACQUIRE(等 STABLE, 超时 8s)
  → LOCATE(单应 px→mm + homography_angle 世界角) → PICK(开爪→悬停→腕对齐→预降→下降→夹→抬→腕回中)
  → CUT(移刀口→切 cut_times 次) → PLACE_BACK(放回) → HOME(回观察位)
  → s_continuous ? OBSERVE : (s_run=false → IDLE)
```

- **急停**：`armctrl_estop()` = 立即 UART 发 `$DST!`（CRASH_SIGNATURES 记录 $DST: 系列真机可动，§8⑤ 专项复验）→ 中止当前序列 → `s_estop` 锁存（`/arm_estop?on=0` 清除后才能再 run）。每个运动原语之间检查锁存位。
- **安全三道保留**：编译门 `CONFIG_ARMLINK_UART_ENABLE`；`kin_selftest` 失败或 `!s_cal.valid` 拒 run（原语层"未标定不发字节"兜底照旧）；run 默认 off、单轮结束自动复位。
- **切割失败保持夹持撤离**（撤到 blade_safe_z → 不开爪回观察位）原样保留。
- **连续模式**：`/arm_run?on=1&cont=1`。防重抓同一颗（放回原位后会再次被检出）：抓取时刻把目标**px 中心**记入本次上电内存"已处理表"（≤8 项，px 域——同一观察位下放回原位的电池 px 不变，零坐标转换）；`track_resume` 前经 `track_set_exclusions()` 下发，跟踪器**初始化**排除已处理点 60px 半径内候选（滤波更新不受影响）；连续 3 次 ACQUIRE 超时 → 自动停。**armcal_t/NVS 布局不动**（H 保命线）。
- 运动节奏维持盲等 `move_ms + SETTLE_MS(200)`（§2 裁决）。

## 6. net 终态

| 端点 | 说明 |
|---|---|
| `/` | 流 + 检测叠加 + **运行 / 连续 / 急停** 按钮 + 标定/IK 自检状态显示 |
| `/status` `/capture` `/arm_target` `/arm_calib`(GET/POST) | 保留不变 |
| `/detect` | 增加滤波后目标框与 track 状态字段（STABLE/coasting/hits）；只画 ≥0.40 原始框 |
| `/arm_run` | `?on=0|1&cont=0|1` |
| `/arm_estop` | `?on=1` 急停锁存；`?on=0` 清除 |
| 删除 | `/arm_test` `/arm_auto` `/arm_grade` |

handler 数 11→10（`max_uri_handlers=16` 余量足）；`stack_size=8192` 不动（CRASH_SIGNATURES 坑）。

## 7. dashboard：本轮埋钩子，队友照计划书实现 WS

**本轮固件预埋**（队友只加一个 `net_ws.c`，不碰核心）：
- `armctrl_cycle_log_t`{seq_id, cls[8], score, t_identified/picked/cut/placed_us, ok} + `armctrl_set_event_cb(cb, arg)`：每轮各阶段打点、完成/失败时回调。
- `armctrl_get_stats(&total, &session)`：累计数存 NVS 独立命名空间 `"stats"`（**不碰 armcal**，每轮写一次磨损可忽略）。
- 急停入口 `armctrl_estop()`（§5 已有）。

**计划书 `docs/ai/DASHBOARD_INTEGRATION.md`**（本轮交付的文档，实现归队友）：
- 固件侧：`CONFIG_HTTPD_WS_SUPPORT=y`，`/ws` 挂现有 httpd(80)；**dashboard 默认地址改 `ws://192.168.4.1/ws`**（app.js 一行，免开第二个 httpd 实例）。
- PROTOCOL.md 六类消息 ↔ 固件数据源逐条映射：`device_status`←1Hz 定时（ai_get_last+armctrl 状态+heap）；`battery_log`←事件回调；`carbon_update`←stats×单颗系数；`control/ESTOP`←`armctrl_estop()` + `command_ack` 回包；`video_frame`←最新 JPEG base64 @2fps（PSRAM 缓冲、`httpd_ws_send_frame_async`、限单客户端）；`trace_query`←RAM 环形日志 ≥16 条。
- 分步验收：PC 端 wscat 假 dashboard 逐消息过 → 真 dashboard 联调 → 与抓取循环并发 30min 无 heap 泄漏（`/status` 曲线平）。
- 风险提示：SoftAP 带宽（base64 膨胀 1.33×，2fps QVGA ~100-200Kbps 可行）；ESP-NOW 通知 Steward 一说不存在（链路是 UART），计划书里纠正话术。

## 8. 验证（实测为准，不靠"看起来行"）

1. **host**：既有 3 组测试 + `test_track`（§4.7 全部 7 类向量）全绿。
2. **build 绿** + size 记录（预期 .text 略减、rodata 不变）。
3. 上板（每次 flash 当场确认）：
   ① 静止电池录 ≥100 帧 → 实标 R_pos/R_wh/R_ang → 复核滤后 std ≤1.2px；
   ② **重放当晚失败场景**（run → 回观察位 → 采位姿）→ STABLE 一次建立、零"位姿不稳"循环；
   ③ 真电池单轮无刀：抓→抬→放回 ≥8/10；
   ④ 装刀含切割完整单轮；
   ⑤ 连续模式 2-3 颗 + **急停实测**（$DST! 真机响应；不符则急停降级为软停 + 断动力电话术）。
4. 回归防线：`kin_selftest` golden 不动；当晚日志场景永久留在 host 测试。

## 9. 提交收尾顺序（严格按序）

1. 代码清理 + 跟踪器 + 状态机 + 钩子（§3-§7）→ host 绿 + build 绿。
2. 上板验证 §8（**③ 过线 = 演示底线**；④⑤ 加分项）。
3. 文档终态化：README、ARM_PIPELINE（去 G 语义）、`docs/作品简介.md` 核对、DASHBOARD_INTEGRATION.md。
4. merge → master。
5. **最后**：partitions.csv 收缩（factory 8MB→4MB、删 3MB `espdet_det` 空分区、保 coredump、**nvs offset 不动 → H 存活**）→ 全片重烧 + boot check + `GET /arm_calib` 回读 valid=true → tag。

## 10. 风险与回退

| 风险 | 缓解 |
|---|---|
| ai 门限降 0.25 放进更多误检 | 只喂跟踪器不上显示；空间门限 + min_hits 挡；实测误锁再回调 0.30 |
| 跟踪器参数水土不服 | λ 单旋钮 + §8① 30 分钟实标流程；host 向量先行兜底 |
| $DST! 真机行为与记录不符 | §8⑤ 专项实测；不符则降级软停 |
| 分区收缩烧录翻车 | 放最后；boot check + 标定回读；回退 = 旧 partitions.csv 重烧即恢复 |
| 删除误伤隐性引用 | 每删一批立即 build；grep 零残留自查（§3 已预核引用闭合） |
| 时间不够 | §9 步 2 的 ③ 为底线；④⑤ 可降级为报告叙述 |

## 附：调研来源（供设计报告引用）

- **滤波方案**：恒位置标量 KF + 双阈值 + 时间制滑行 + 滞回判据。依据：SORT 源码（github.com/abewley/sort，GPL——仅参数参考不抄码）、OC-SORT（arxiv 2203.14360，低帧率 CV 失效分析）、ByteTrack 双阈值、StrongSORT/NSA（arxiv 2202.11983，二期可选）、TinyEKF（MIT，二期 EKF 备胎）。核心手写 ~150 行，无许可风险。
- **抓取流程同构参考**：HiWonder ArmPi `ColorTracking.py`（稳定窗+均值+全抓取序列，印证 armctrl 设计；其无超时/重试的弱点我们已补）；协议范式 pymycobot IS_MOVING/CRC16（单向链路裁决不采）、HiWonder xArm1S 控制板协议（一帧多舵机+统一时长——KM1 捆绑帧同族，已在用）。
- **生态位**："ESP32 单板 NN 检测→机械臂抓取"无像样开源先例（最接近 peterbalch ESP32cam-robot-arm，传统视觉）——本项目即该生态位首个完整实现，报告创新点可引。
