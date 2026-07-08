# 设计：危险电池警报 + 语音播报

> **生成**：2026-07-09。
> **前置**：`docs/superpowers/specs/2026-07-08-voice-start-stop-control-design.md`（今天早些时候完成的语音开始/停止设计，本设计复用同一条 KM1 共享总线和转发机制）、`docs/ai/CONTEST_REQUIREMENTS.md`（投稿截止 2026-07-09 18:00，今天就是截止日）。
> **用户已拍板**：
> - 需求 = 判定"危险电池"后（1）发出警报（2）语音播报，且要结合语音模块（ASRPRO/天问Block）。
> - 语音模块能被主动命令播报（非只能被动回复人类说话）——已用官方文档 + 用户提供的真实 ASRPRO 源码交叉确认。
> - 演示强制危险开关 `BATTERY_POLICY_DEMO_FORCE_DANGER` 保持开启——即每一轮完整抓取都会触发一次警报/播报（不要求现场必须准备一颗真的坏电池样本才能演示这个功能）。
> - 方案 A：蜂鸣器警报保底 + 语音播报锦上添花，两者互不依赖。
> - ASRPRO 侧代码基于用户提供的真实源码（天问Block 生成的 `ASR_CODE()`/`PROCEDURE()`/`hardware_init()` 等）直接改，而非凭空猜测 API。

---

## 1. 一句话

新增一个指令词 `#Danger!`，Brain 在 `armctrl` 判定电池为危险时经**已经在用**的 armlink UART（GPIO1）发出；KM1 在**今天已经建好**的 `loop_uart()` 分发点识别这个词后，本地触发蜂鸣器（保底警报，零新增风险）并转发到共享总线（`Serial2.print`，同样是今天已建好的机制）；ASRPRO 端新增一段监听逻辑，收到 `#Danger!` 后调用 `speak_playid()` 播报预先注册好的语音（锦上添花，不保证今天能验证通过，但不影响蜂鸣器和电池分拣本身）。全程不新增组件、不新增接线、不新增 Kconfig 开关——完全复用今天为"语音开始/停止"功能搭好的基础设施。

## 2. 背景：现有的"危险电池"处理是什么样

`components/battery_policy` 已经存在判危险的逻辑：`battery_risk_eval_for_class()` 在分类结果是 `AI_CLASS_BAD_AA`（坏的 AA 电池）时判定 `BATTERY_RISK_DANGEROUS`；另有一个演示开关 `BATTERY_POLICY_DEMO_FORCE_DANGER`（当前值=1）会无条件强制每一颗电池都判定为危险，用户已确认这个演示开关本次保持开启不动。

`armctrl.c` 的抓取状态机里，这个判定发生在**抓起电池之后、决定切割还是投危险篮之前**（`s_holding = true` 之后紧接着调用 `battery_risk_eval_for_class`）。判定为危险时，现有行为是跳过切割、直接投进"危险篮"（与"正常篮"物理上是两个不同投放点），配一行 `ESP_LOGW` 日志——**没有任何主动警报**（无蜂鸣、无灯光、无播报、无 dashboard 事件）。

KM1 板上原厂就有一颗物理蜂鸣器（原理图 `BUZZER1`，经 `IO11_BEEP` 驱动，原厂固件里 `beep_on()`/`beep_off()` 两个宏已在多处使用），零新增硬件成本。

## 3. 范围

**做**：
1. Brain (`components/armctrl/armctrl.c`)：判定 `dangerous=true` 处新增一行 `armlink_uart_send("#Danger!", 0)`。`armlink_uart.h` 已经是该文件的现有 include，零新增依赖。
2. KM1 (`steward_km1.ino` 的 `loop_uart()`)：今天已建的 `case 2:` 判断再扩一个分支——匹配到 `#Danger!` 时，本地跑 3 次蜂鸣（`beep_on()/delay/beep_off()`循环，复用原厂宏），并 `Serial2.print()` 转发到共享总线（今天已验证过的转发机制）。
3. ASRPRO（天问Block 生成的源码，用户提供的真实文件）：
   - 顶部新增一条 `//{playid:20001,voice:检测到危险电池，请远离}` 语音注册（照抄文件里已有的欢迎词/退出语音注册写法）。
   - `PROCEDURE()`（文件里唯一空着、结构上像主循环的钩子）里新增：`Serial1.available()` 轮询 → 收到完整字符串等于 `"#Danger!"` → `speak_playid(20001)`。
4. 分层验证计划（§6）。

**不做**（明确排除）：
- 不碰急停逻辑、不碰"危险电池投递到危险篮"这个现有物理动作本身——本设计只是在这个决策点旁加一个警报副作用，不改变决策结果或物理路径。
- 不新增 Kconfig 开关——`armlink` UART 今天已经是启用状态在真实驱动机械臂，多发一种字符串不引入新的安全面。
- 不处理"如果 ASRPRO 播报到一半又收到下一次 `#Danger!`"这种重叠播报的行为——ASRPRO 在这种情况下具体怎么反应未知，且以当前"每轮抓取周期触发一次"的频率（以秒计的间隔）不太可能撞上，留给实测观察，不预先设计兜底。
- 不修改 `battery_policy` 组件本身或 `BATTERY_POLICY_DEMO_FORCE_DANGER` 的值——用户已确认保持现状。

## 4. 架构与数据流

```
armctrl_task() 判定 risk.level==DANGEROUS（现有代码，位置不变）
                    │
                    ▼
        armlink_uart_send("#Danger!", 0)
        （复用今天已启用的 armlink UART1/GPIO1，
         这条线本来就在发运动帧，现在多发一种帧）
                    │
                    ▼
KM1 共享 RX2(GPIO41) 收到，走现有 uart_data_parse() 攒帧
                    │
        loop_uart() case 2 分发（今天已加的判断点，再加一个分支）
                    │
        strcmp(uart_receive_buf, "#Danger!") == 0 ?
                    │
    ┌───────────────┼────────────────┐
    ▼                                 ▼
KM1 本地触发蜂鸣器            Serial2.print("#Danger!")
beep_on()→delay→beep_off()   （复用今天已加的转发机制，
×3(原厂宏，现成)               TX2 扇出到"语音接口"+"openmv接口"）
    │                                 │
警报响了(保底)              ASRPRO 的 Serial1 收到
                            (若 PROCEDURE() 已改好)
                                      │
                          speak_playid(20001) 播报
                          "检测到危险电池，请远离"
                          (加分项，独立于蜂鸣器)
```

关键点：蜂鸣器和转发是**并列动作**，不是链式依赖——即使 ASRPRO 那边没配好/没反应，蜂鸣器该响还是响；即使蜂鸣器意外没响，也不影响后面投危险篮这个物理安全动作本身。三层互不阻塞。

## 5. 组件与改动、分工

| 位置 | 改动内容 | 谁做 | 确定性 |
|---|---|---|---|
| `components/armctrl/armctrl.c` | `dangerous=true` 处新增一行 `armlink_uart_send("#Danger!", 0)`，失败只记日志不阻断后续流程 | Claude（idf-bridge build；flash 当场确认） | 高——零新增依赖，位置就是现有判定点旁边 |
| `steward_km1.ino` 的 `loop_uart()` | `case 2:` 新增 `#Danger!` 分支：本地 3 连蜂鸣 + `Serial2.print()` 转发 | Claude 写代码，用户 arduino-cli 编译烧录 | 高——复用今天已验证的转发模式 + 原厂现成的蜂鸣宏 |
| ASRPRO 源码（天问Block） | 顶部注册新语音 `//{playid:20001,...}`；`PROCEDURE()` 里加监听 `#Danger!` → `speak_playid(20001)` | Claude 给出代码，用户在天问Block 里落地、编译、烧录、验证 | **低——两处未验证**：①`PROCEDURE()` 是否真的被反复调用（结构推断，非文档确认）；②手改代码在天问Block 里是否会被图形化界面重新生成覆盖 |

## 6. 错误处理与安全

- **警报永远不能反过来卡住安全处置**：`armlink_uart_send` 失败（返回负数）只记警告日志，不影响"投危险篮"这个物理动作继续执行。
- **三层互不依赖**：蜂鸣器（KM1 本地）、转发（KM1→ASRPRO）、播报（ASRPRO 本地）各自独立执行，任何一层失败不影响其他层。
- **频率**：判定发生在每轮完整抓取周期内最多一次（不是逐帧触发），配合 `DEMO_FORCE_DANGER=1` 意味着每轮抓取都会响一次，但间隔以秒计（一次完整抓取-切割-放回耗时数秒起），不会出现指令高频拥塞总线或 ASRPRO 收到指令时还在处理上一条的密集冲突场景。
- **已知未验证风险明确标注**（不假装已解决）：ASRPRO 侧的 `PROCEDURE()` 轮询假设、以及天问Block 手改代码是否会被覆盖，两者都需要用户在天问Block 环境里实测才能确认，本设计不依赖它们today一定成功——蜂鸣器这条腿独立保证"警报"需求的交付。

## 7. 测试计划（分层验证，与今天早些时候语音开始/停止的验证方式同一套路）

1. **ASRPRO 独立测**（不接 KM1/Brain）：用户在天问Block 里改完 `PROCEDURE()` 并烧录后，用 USB-TTL 直接向 ASRPRO 的 `Serial1` 对应物理脚发送字符串 `"#Danger!"`，观察是否播报"检测到危险电池，请远离"。目的：排除 ASRPRO 侧代码本身的问题，不引入 KM1/Brain 的不确定性。
2. **KM1 转发+蜂鸣单测**（不接 ASRPRO 或接上都行，不影响这一步）：用 USB-TTL 向 KM1 共享 RX2 节点发送 `"#Danger!"`，观察蜂鸣器是否响 3 声、`Serial2` 上是否原样转发。目的：排除 KM1 侧代码的问题。
3. **端到端联调**：Brain、KM1、ASRPRO 全部按实际接线通电，真跑一轮完整"识别→抓取→（危险开关强制）判危险→投危险篮"，确认蜂鸣器响、（若前两步都通过）ASRPRO 播报、网页/日志能看到 `#Danger!` 被正确处理。
4. **回归检查**：确认这次改动没有影响今天已经跑通的语音开始/停止链路（`#Start!`/`#Stop!` 依旧正常）、没有影响正常（非强制危险）抓取周期的切割+正常篮投放路径。

## 8. 开放项（写代码前需最终确认，不阻塞设计本身）

- ASRPRO 侧：`PROCEDURE()` 是否真的是"反复调用的主循环"钩子——需要用户在天问Block 的编程手册里确认，或直接刷进去实测。
- ASRPRO 侧：天问Block 环境里手改这份源码文件，后续会不会被图形化积木界面重新生成覆盖——需要用户确认自己工程当前的编辑方式（纯代码模式 vs 积木生成模式）。
- `speak_playid(20001)` 里的 `20001` 是占位 ID，需要用户在天问Block 图形化工具里实际注册新语音后，换成工具分配的真实 playid。
