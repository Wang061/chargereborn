# 危险电池警报 + 语音播报 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 电池被判定为危险时，Brain 通知 KM1 本地蜂鸣器报警（保底）并尽力转发给 ASRPRO 语音模块播报（锦上添花），两条腿互不依赖。

**Architecture:** 新增指令词 `#Danger!`，复用 2026-07-08 已建好的全部基础设施——Brain 经已启用的 armlink UART(GPIO1) 发送 → KM1 现有 `loop_uart()` 分发点识别后本地蜂鸣 + 转发到共享总线 → ASRPRO（若已配置）播报。零新增组件、零新增接线、零新增 Kconfig。

**Tech Stack:** ESP-IDF 5.5.4（Brain, C）+ Arduino/arduino-cli（KM1, `steward_km1.ino`）+ 天问Block（ASRPRO，专有图形化工具，Claude 无法操作）。

## Global Constraints

- `IDF_VERSION=5.5.4`、`IDF_TARGET=esp32s3`（版本锁，不得违反）。
- 不新增 Kconfig 开关——armlink UART 已经启用在驱动真实机械臂，多发一种字符串不引入新安全面。
- 警报/播报三层（KM1 蜂鸣器、KM1→ASRPRO 转发、ASRPRO 播报）互相独立，任何一层失败不能阻断"投危险篮"这个物理安全动作，也不能阻断其他层。
- 不修改 `components/battery_policy`（判危险逻辑本身）、不改 `BATTERY_POLICY_DEMO_FORCE_DANGER` 的值（保持开启，用户已确认）。
- KM1 侧改动只应用到 `D:\WJ\jixiebi\steward_km1\steward_km1.ino`；只读参考 `reference/esp32.ino` 禁止修改。
- arduino-cli 改完源码内容后编译必须加 `--clean`（2026-07-08 踩过的坑：不加会复用旧缓存，日志/体积看不出内容没真正更新）。
- 任何 `flash` 操作必须当场向用户确认（项目铁律）。
- 前置 spec：`docs/superpowers/specs/2026-07-09-danger-battery-alarm-voice-report-design.md`。
- **今天(2026-07-09)是投稿截止日**：Task 3（ASRPRO）确定性低、完全在用户手中，设计上必须允许它今天跑不通也不影响 Task 1+2 独立交付"警报"这个需求。

---

## Task 1: Brain 判定危险时发送 `#Danger!`

**Files:**
- Modify: `components/armctrl/armctrl.c`（`armctrl_task()` 内 `dangerous` 分支，约第 431-433 行；行号以改动前最新读取为准，按 `ESP_LOGW(TAG, "危险电池: 跳过切割, 直接投危险篮");` 这行定位而非行号）

**Interfaces:**
- Consumes（既有代码，`components/armlink/include/armlink_uart.h`，已是 `armctrl.c` 现有 include）：`int armlink_uart_send(const char *s, int len);`（`len<=0` 时自动 `strlen`；返回写出字节数或负数）
- 不产出新符号，供 Task 4 端到端测试时观察其效果。

- [ ] **Step 1: 定位并修改危险分支**

原内容（`components/armctrl/armctrl.c`）：

```c
        } else {
            ESP_LOGW(TAG, "危险电池: 跳过切割, 直接投危险篮");
            if (place_to_bin(DANGER_BIN_X_MM, DANGER_BIN_Y_MM, BIN_RELEASE_Z_MM, "危险") != ESP_OK) {
```

替换为：

```c
        } else {
            ESP_LOGW(TAG, "危险电池: 跳过切割, 直接投危险篮");
            // 通知 KM1 报警(本地蜂鸣器保底 + 尽力转发给语音模块播报，见
            // docs/superpowers/specs/2026-07-09-danger-battery-alarm-voice-report-design.md)。
            // 复用已启用的 armlink UART(GPIO1)，失败只记日志，绝不阻断下面的安全投放动作。
            if (armlink_uart_send("#Danger!", 0) < 0) {
                ESP_LOGW(TAG, "危险警报帧发送失败(armlink UART 未启用或写入出错), 继续走安全流程不受影响");
            }
            if (place_to_bin(DANGER_BIN_X_MM, DANGER_BIN_Y_MM, BIN_RELEASE_Z_MM, "危险") != ESP_OK) {
```

不改动 `place_to_bin` 调用本身、不改 `if`/`else` 结构、不改判定 `dangerous` 的上游逻辑。

- [ ] **Step 2: build**

用 `mcp__idf-bridge__build` 跑一次全量 build。
Expected: build 通过（绿），无新增警告。

- [ ] **Step 3: flash（当场确认）**

告知用户：这次 flash 会让 Brain 在下次判定危险电池时（当前 `BATTERY_POLICY_DEMO_FORCE_DANGER=1`，意味着每一轮完整抓取都会触发）经 GPIO1 多发一条 `#Danger!` 字符串——这条线本来就在驱动机械臂，新增内容不改变现有运动帧行为。确认后执行 `mcp__idf-bridge__flash`。

- [ ] **Step 4: boot 干净性检查**

`mcp__idf-bridge__monitor_start` 起监视，`monitor_read` 确认启动日志正常（`IK 自检通过`、`ai init` 成功、`alive %us heap=%uB` 循环稳定输出），无新增崩溃/警告/栈溢出迹象。这一步不要求真的触发一次危险判定（那需要真实抓取周期，留给 Task 4 端到端测试一并验证），只确认新代码没有破坏正常启动与空闲运行。

- [ ] **Step 5: Commit**

```bash
git add components/armctrl/armctrl.c
git commit -m "feat(armctrl): send #Danger! over armlink UART on dangerous verdict

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Task 2: KM1 蜂鸣器警报 + 转发（用户用 arduino-cli 编译烧录）

> **前置**：改动目标是 `D:\WJ\jixiebi\steward_km1\steward_km1.ino`。这是今天已经改过一次的同一个 `loop_uart()` 函数，本任务在其基础上再加一个分支，不影响已有的 `#Start!`/`#Stop!` 转发逻辑。

**Files:**
- Modify: `D:\WJ\jixiebi\steward_km1\steward_km1.ino`（`loop_uart()` 函数，`case 2:` 分支）

**Interfaces:**
- 不影响本仓库任何符号；这段代码独立运行在 KM1 自己的芯片上。

- [ ] **Step 1: 定位并替换 `loop_uart()` 的 `case 2:` 分支**

原内容（今天已改过一次后的当前状态）：

```c
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
```

替换为：

```c
            case 2:
                // 语音链路：原理图 Open-CESPV1.2 "语音接口"与"openmv接口"(Brain 所接)
                // 经二极管共享同一条 RX2，语音帧和运动帧混在同一条总线上进来，
                // 都会先经过上面的 uart_data_parse 攒成 "#...!" 帧。这里精确匹配
                // 语音模块发的 "#Start!"/"#Stop!"，命中就原样从 TX2 转发给 Brain
                // (Brain 新接的 GPIO2 监听这条线)，不当运动帧处理；其余 # 帧
                // (含真实运动帧 #dddPddddTdddd!)行为完全不变，走 parse_action。
                if (strcmp(uart_receive_buf, "#Start!") == 0 || strcmp(uart_receive_buf, "#Stop!") == 0) {
                    Serial2.print(uart_receive_buf);
                } else if (strcmp(uart_receive_buf, "#Danger!") == 0) {
                    // 危险电池警报：Brain 判定后经共享总线发来。本地蜂鸣器报警是
                    // 保底(beep_on/beep_off 是原厂已有宏)，同时转发给语音模块播报
                    // 是锦上添花——两者并列执行、互不依赖，任何一个环节失败不影响
                    // 另一个。三连蜂鸣比现有单次确认音更容易听出区别。
                    for (int i = 0; i < 3; i++) {
                        beep_on();
                        delay(150);
                        beep_off();
                        delay(150);
                    }
                    Serial2.print(uart_receive_buf);
                } else {
                    parse_action(uart_receive_buf);
                }
                break;
```

不改动 `uart_data_parse`、`handleSerial1`、`handleSerial2`、`case 3`/`case 4` 分支任何一行。

- [ ] **Step 2: 用户用 arduino-cli 清缓存重编译**

```powershell
& "D:\WJ\tools\arduino-cli\arduino-cli.exe" compile --fqbn esp32:esp32:esp32s3 --clean "D:\WJ\jixiebi\steward_km1\steward_km1.ino"
```

Expected: 编译成功，体积相对 2026-07-08 记录（400855 字节/30%，那次含 `#Start!`/`#Stop!` 分支）只应有几十字节的增量（新增一个 `for` 循环 + 3 次宏调用 + 一次 `strcmp`），不应有明显跳变。**务必带 `--clean`**——不加可能复用旧缓存，误以为烧的是新内容。

- [ ] **Step 3: 用户烧录**

```powershell
& "D:\WJ\tools\arduino-cli\arduino-cli.exe" upload -p COM12 --fqbn esp32:esp32:esp32s3 "D:\WJ\jixiebi\steward_km1\steward_km1.ino"
```

（`COM12` 是 2026-07-08 实测的 KM1 端口号，端口号可能随插拔变化，烧录前确认当前实际口号——参考 `Get-CimInstance Win32_PnPEntity -Filter "PNPClass='Ports'"` 认设备名而非号。）
Expected: 全部 "Hash of data verified"，无报错。

- [ ] **Step 4: 独立验证蜂鸣器+转发（不需要 Brain 或 ASRPRO 在线）**

用 USB-TTL 以 115200 波特率向 KM1 共享 RX2 节点（接"语音接口"或"openmv接口"任一 4 针座的对应数据脚均可，二极管保证不会电气冲突）发送字符串 `"#Danger!"`。

Expected: 蜂鸣器响 3 声（每声约 100ms 音+150ms 停顿间隔）；同时 TX2(GPIO42) 上能测到原样转发出的 `"#Danger!"`（若手边有示波器/另一路 USB-TTL 监听可确认，没有的话听到蜂鸣器响即视为这一步通过，转发部分留给 Task 4 端到端一并确认）。

---

## Task 3: ASRPRO 语音播报（用户在天问Block 环境全权操作，Claude 只给代码）

> **重要边界声明**：本任务的每一步都由用户在天问Block 图形化工具里完成——Claude 没有任何工具能操作、编译、烧录或验证 ASRPRO。下面的代码基于用户今天提供的真实 ASRPRO 源码（`ASR_CODE()`/`PROCEDURE()`/`hardware_init()`）编写，但有两处**未验证的假设**（见 Step 1 末尾），今天这个任务能否跑通完全不确定，**不影响 Task 1+2 已经独立交付的"蜂鸣器警报"需求**。

**Files:**
- Modify: 用户的 ASRPRO 天问Block 工程源码（用户今天粘贴给 Claude 的那份文件；Claude 未持有该文件路径，需用户自行定位并回填）

**Interfaces:**
- 不影响本仓库任何符号，也不影响 Brain/KM1 侧代码；ASRPRO 独立运行。

- [ ] **Step 1: 用户在源码顶部新增语音注册**

在文件顶部现有的两条 `//{playid:...}` 注册行（欢迎词/退出语音）下方新增一行，写法完全照抄：

```c
//{playid:20001,voice:检测到危险电池，请远离}
```

- [ ] **Step 2: 用户在 `PROCEDURE()` 里新增监听逻辑**

原内容：

```c
void PROCEDURE(){
}
```

改为：

```c
void PROCEDURE(){
  if (Serial1.available()) {
    String cmd = Serial1.readString();
    cmd.trim();
    if (cmd == "#Danger!") {
      speak_playid(20001);
    }
  }
}
```

**两处未验证的假设，用户需自行确认或调整**：
1. `PROCEDURE()` 是否真的会被反复调用——这是从代码结构（声明了、被引用了、目前是空的，很像"主循环"角色）推断的，不是查到的官方文档证实。若不成立，需要在天问Block 里改用官方文档指出的正确"串口接收"钩子（比如某些教程里出现过的 `SERIAL1_CODE()` 命名，具体以用户工程实际支持的钩子类型为准）。
2. `speak_playid()` 函数名——已用网络检索交叉确认（多个独立来源一致指向这个名字+`//{playid:...}` 注册配套使用），置信度较高，但不是从用户自己的工程文档里直接确认的。

- [ ] **Step 3: 用户确认代码编辑方式后编译烧录**

在天问Block 里确认当前工程能否直接编辑这份源码文件后编译（而不是必须回到纯积木界面重新拖拽），若能则编译并烧录到 ASRPRO；若不能（图形化界面会用积木重新生成覆盖手改代码），则需要用户在积木面板里找到等价的"添加语音"+"串口收到字符串后执行"模块手动搭建同样的逻辑，Claude 无法替代这一步。

- [ ] **Step 4: 独立验证（不需要 KM1 或 Brain 在线）**

用 USB-TTL 直接向 ASRPRO 的 `Serial1` 对应物理脚（`hardware_init()` 里 `setPinFun(2,FORTH_FUNCTION); setPinFun(3,FORTH_FUNCTION);` 映射的那两个脚）发送字符串 `"#Danger!"`。

Expected: ASRPRO 播报"检测到危险电池，请远离"。若没反应，先确认 Step 1 的 playid 是否被工具正确生成注册（天问Block 生成模型后可在 `天问Block\asrpro\voice\mp3` 目录下查看所有已注册语音文件，确认新语音在列）。

---

## Task 4: 端到端联调 + 回归检查（阶段性成功即算完成）

> **前置**：Task 1（Brain）已 build+flash+boot 干净；Task 2（KM1）已独立验证蜂鸣器响；Task 3（ASRPRO）能做到哪一步算哪一步——**即使 Task 3 完全没跑通，本任务依然要做，只是"语音播报"这一项标记为未达成，不影响整体验收**。

**Files:** 无代码改动，纯真机联调。

- [ ] **Step 1: 确认三方接线**

语音模块 → KM1"语音接口"；Brain GPIO1(armlink) 与 GPIO2(voicelink) → KM1"openmv接口"（今天早些时候已接好，此处只需确认未松动）。

- [ ] **Step 2: 真实抓取周期观察**

触发一轮完整抓取（网页 `/arm_run?on=1&cont=1` 或今天已跑通的语音"开始处理"皆可）。由于 `BATTERY_POLICY_DEMO_FORCE_DANGER=1`，本轮抓取判定阶段必然触发危险分支。观察：
- Brain monitor 日志：`危险电池: 跳过切割, 直接投危险篮`，且**没有**出现"危险警报帧发送失败"的警告（说明 `armlink_uart_send` 成功）。
- KM1：听到 3 声蜂鸣（**这是硬指标，必须通过**）。
- ASRPRO：是否播报"检测到危险电池，请远离"（**若 Task 3 没跑通，这里没反应是预期的，不算本任务失败**）。
- 电池确实被投进危险篮（现有行为，本次改动不应影响这个结果）。

- [ ] **Step 3: 回归检查**

确认本次改动没有影响：
- 今天早些时候跑通的语音开始/停止链路（`#Start!`/`#Stop!` 依旧正常触发/停止连续抓取）。
- 网页按钮控制、急停功能依旧正常。
- 正常抓取流程（切割+正常篮投放路径）的既有代码路径未被触碰（本次改动只加了一个新的 `else if` 分支，不改变原有 `if`/`else` 结构，理论零风险，仍需一次真机确认无异常）。

- [ ] **Step 4: 记录阶段性结果**

无论 Task 3 是否跑通，本任务视为完成的标准是：Step 2 里的"硬指标"（Brain 正确发送 + KM1 蜂鸣器响）全部通过。若 ASRPRO 播报当天没能验证成功，在实施报告/交接说明里明确记录"警报功能已交付，语音播报待续"，不要含糊其辞地写"已完成"掩盖这一点。

---

## Self-Review 记录

- **Spec 覆盖**：spec §3"做"的 4 条（Brain armctrl 改动、KM1 loop_uart 改动、ASRPRO 源码改动、分层验证）分别对应 Task 1、Task 2、Task 3、Task 2/3/4 里各自的独立验证步骤。spec §6 错误处理里"警报不能阻断安全流程"由 Task1-Step1 的 `armlink_uart_send` 失败仅记日志体现；"三层互不依赖"由 Task4-Step2 的观察项分别列出、互不作为对方的通过前提体现。spec §8 三个开放项（`PROCEDURE()` 假设、天问Block 覆盖风险、`20001` 占位 ID）均在 Task3-Step1/Step3 里显式列出，未被跳过。
- **占位符扫描**：全文无 TBD/TODO/"补充错误处理"类占位；所有代码块均为完整可运行内容（Task 3 因确定性低而写了"若不成立需要..."的条件性指引，这是明确标注的不确定性，不是偷懒的占位符）。
- **类型一致性**：`armlink_uart_send(const char*, int)` 签名在 Task1 与既有 `armlink_uart.h` 一致；KM1 侧 `#Danger!` 字符串字面量在 Task1(Brain发送)/Task2(KM1匹配)/Task3(ASRPRO匹配) 三处逐字一致；`speak_playid(20001)` 与 Step1 注册的 `playid:20001` 一致。
- **范围与责任边界**：Task 1 全部由 Claude 完成并验证；Task 2 代码由 Claude 写、编译烧录验证由用户做；Task 3 全部由用户操作，Claude 仅提供代码建议——三个任务的"谁做"在文件头部即声明清楚，避免执行时误以为 Claude 能替用户操作天问Block。
