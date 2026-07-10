# 设计：语音"开始/停止"接入机械臂连续抓取

> **生成**：2026-07-08。
> **前置**：`docs/ai/ARM_PIPELINE.md`（armctrl 运行手册）、`docs/ai/BOARD.md`（Brain 引脚/危险 GPIO）、`docs/ai/SAFETY.md`（硬件安全铁律）、`docs/ai/CONTEST_REQUIREMENTS.md`（2026-07-09 18:00 投稿截止）、`D:\WJ\jixiebi\5.软件工具\1.原理图\OpenCESP.pdf`（KM1 = Open-CESP V1.2 原理图，架构修正的依据）。
> **2026-07-08 当日修正**：初版基于"从固件代码反推空闲引脚"，误判语音模块接的是 KM1 空闲的 Serial1(RX1=GPIO18)、转发走 TX1(GPIO17)。核对 KM1 实物接线 + 原理图后发现：**KM1 的"语音接口"和"openmv接口"(Brain 所接) 经二极管共享同一条 RX2(GPIO41)**，语音帧和 Brain 下发的运动帧走同一条物理总线、同一个 `uart_data_parse` 共享状态机；KM1 的 Serial1(RX1/TX1) 在这块板子上根本没有排针引出，物理不可达。以下正文已按修正后的架构改写；Brain 侧设计（`voicelink` 组件、GPIO2、armctrl 调用）不受影响，只有 KM1 侧的实现位置变了。
> **用户已拍板**：
> - 语音板 = KM1 套件自带独立语音识别模块（图形化配置、词库可自定义，非简单拍手传感器）。
> - 语义：语音"开始"= 连续抓取拓（等价网页 `arm_run?on=1&cont=1`）；语音"停止"= 缓停（等价网页 `arm_run?on=0`，**不是**急停）。
> - 用户已在语音模块自带图形化工具里配置好：唤醒词"小电"，指令词"开始处理"(识别ID=1)→原样发送 `#Start!`（发两遍，间隔50ms），指令词"停止"(识别ID=2)→原样发送 `#Stop!`（同样发两遍），波特率 115200（已用户确认，与 KM1 `Serial1.begin`/`Serial2.begin` 一致的项目统一波特率）。
> - 物理接线（已用户确认）：语音模块接 KM1 的"语音接口"，经二极管接入共享 RX2(GPIO41)；Brain 的 GPIO2 已接到 KM1"openmv接口"的 TX2(GPIO42)（即 Brain→KM1 现有那根线所在的同一 4 针接口，新利用了其中原本没用上的 TX2 那一半）。
> - KM1 端固件（`D:\WJ\jixiebi\steward_km1\steward_km1.ino`）由用户自己用 **arduino-cli** 编译烧录；Brain 端（ESP-IDF）由 Claude 用 idf-bridge 完成，flash 仍需当场确认。

---

## 1. 一句话

语音模块（唤醒"小电"+指令词，已配置好发 `#Start!`/`#Stop!`）经 KM1"语音接口"接入共享 RX2(GPIO41)，与 Brain 下发的运动帧走同一条物理总线；KM1 在 `loop_uart()` 分发 `#...!` 帧的地方精确匹配这两个词，命中就原样从 TX2(GPIO42) 转发，不命中的（含真实运动帧）行为完全不变；Brain 新增 `voicelink` 组件在 UART2(RX=GPIO2) 收到后调用现有 `armctrl_request_run()`——语音只是给 Brain 已有的"识别→规划→抓取→切割→放回"连续抓取状态机换了个触发入口，状态机本身不动。

## 2. 背景：为什么不能简单"另开一条独立通道"

读了 KM1 实际固件（`steward_km1.ino`）+ 原理图（`Open-CESP V1.2`）后确认：

- 原厂"声控夹取"根本不做语音识别——`shengkong_jiaqu()` 只是 `digitalRead(19)==LOW` 触发固定动作组，语音板只是干触点输出。现在用户的语音板是真正的可配置 ASR 模块，输出改成了 UART 上的 ASCII 帧，架构因此不同，不能照抄原厂那条路。
- **关键硬件事实（原理图核实，初版曾误判）**：KM1 板上"语音接口"和"openmv接口"（Brain 现在接的那个）在电路上经二极管(D13-D16 一类)共享同一个 GPIO41(RX2) 节点——这是板子设计者的有意设计：多个二极管把不同配件口 OR 到同一条输入线上，谁接了就谁的信号进来，彼此不会电气短路，但**逻辑上是同一条总线**。KM1 的 Serial1(RX1=GPIO18/TX1=GPIO17) 在这块板子上没有排针引出，物理不可达，不能用。
- 因此语音帧和 Brain 的运动帧**天然共用** KM1 的 `uart_data_parse()`/`handleSerial2()`/`loop_uart()` 这同一套解析管线，无法像最初设想那样"另开一条独立通道躲开"。好在两者可以在**帧内容层面**区分：语音帧固定是 `"#Start!"`/`"#Stop!"`，和真实运动帧 `#dddPddddTdddd!`（第 4 位固定是 `P`）格式不同，`parse_action()` 本来就不认识前者，会安全忽略——所以只需要在**分发点**（`loop_uart()` 里 `uart_receive_buf` 组好、即将调用 `parse_action` 之前）加一次精确字符串比对即可，不需要新状态机、不需要新缓冲区。
- 这也意味着 `if (uart_get_ok) return;` 这个忙时丢字节的既有行为（运动帧未被消费时新字节会被丢）对语音帧和运动帧一视同仁——这是这条总线本来就有的特性，我们的改动不引入新风险，也无法比现状更好；`loop_uart()` 在真实抓取流程中每次主循环都跑（未见长阻塞路径接在这条链路上），窗口很短。

## 3. 范围

**做**：
1. KM1 (`steward_km1.ino`)：`loop_uart()` 的 `case 2:` 分支里，在调用 `parse_action()` 之前插入一次对 `uart_receive_buf` 的精确字符串比较（`"#Start!"`/`"#Stop!"`），命中则改为 `Serial2.print()` 原样转发，不再落到 `parse_action`；不命中（含所有真实运动帧）走原逻辑不变。不碰 `uart_data_parse`/`handleSerial1`/`handleSerial2`。
2. Brain：新组件 `components/voicelink/`，UART2 监听（RX=GPIO2，TX=-1），收到 `#Start!` → `armctrl_request_run(true, true)`（连续拓）；收到 `#Stop!` → `armctrl_request_run(false, ...)`（缓停；`cont` 参数值不影响停止本身——已核实 `armctrl_request_run` 内 `s_continuous=cont` 无条件赋值但仅在下次 `on=true` 时才被读取，具体传什么留给实现阶段对齐网页按钮的做法）。Kconfig 仿 `armlink` 模式（默认关，GPIO 默认 -1 强制显式分配）。
3. 接线（已由用户完成）：语音模块 → KM1"语音接口"（经二极管入共享 RX2）；Brain GPIO2 ← KM1"openmv接口"TX2(GPIO42)（复用 Brain→KM1 现有那条线所在的同一接口）。
4. 分步验证计划（§7）。

**不做**（明确排除，防范围蔓延）：
- 不新增语音急停能力——急停仍只走网页红键 + `$DST:0!`（已验证过的唯一急停串）。语音"停止"是缓停，语义上不允许和急停混淆。
- 不处理语音模块图形化配置里其余识别词（如"关灯"/ID10，vendor demo 遗留），只接管"开始处理"/"停止"这两个已确认的词。
- 不改 KM1 现有 `ai_mode`/`loop_Function()` 那套旧 demo 状态机（颜色分拣/定距夹取/红外触发/触摸夹取）——语音入口直连 Brain 的 `armctrl`，不经过 KM1 自己的旧状态机。
- 不改 KM1 现有 `uart_data_parse()`/运动帧解析逻辑本身——只在 `loop_uart()` 分发点加一次内容判断，真实运动帧路径字节不动。

## 4. 架构

```
语音模块 TX ──► KM1"语音接口"──二极管──► 共享 RX2(GPIO41) @115200   [已接线]
                                              ▲
Brain GPIO1(armlink TX) ──► KM1"openmv接口"──二极管──┘            [既有链路，运动帧同源]
                                              │
                                              │  KM1 既有 uart_data_parse() 攒帧(不改)
                                              │  loop_uart() 分发点新增一次精确比较：
                                              │  "#Start!"/"#Stop!" → 转发；其余 → parse_action(不变)
                                              ▼
                              KM1 TX2(GPIO42) ──► Brain GPIO2(UART2 RX) @115200  [已接线]
                                        │
                                        │  Brain 新组件 voicelink：收满一帧、比对
                                        ▼
                              armctrl_request_run(true/false, ...)
                                        │
                              （复用现有连续抓取状态机，与网页 /arm_run 同一入口，
                               估停锁存/单轮-连续语义完全一致）
```

关键点：语音帧和 Brain 的运动帧physically共用同一条 RX2 总线、同一套 KM1 解析管线，靠**帧内容**（`"#Start!"`/`"#Stop!"` 是固定短串，和运动帧的 `#dddPddddTdddd!` 数字格式不同）而不是靠**物理隔离**来区分。KM1 只在分发点多做一次字符串比较，不做任何业务判断；真正的启停语义、急停互锁全部复用 Brain 现有的 `armctrl_request_run()` / `armctrl_is_estopped()`，与网页按钮走同一入口，行为保证一致，不用重新实现一遍状态机。

## 5. 组件与改动、分工

| 位置 | 改动内容 | 谁做 | 验证方式 |
|---|---|---|---|
| 语音模块图形化配置 | 已完成：唤醒词"小电"，ID1→`#Start!`(×2)，ID2→`#Stop!`(×2)，波特率 115200（已用户确认，与 KM1 现有 UART 一致） | 用户 | 已确认 |
| `D:\WJ\jixiebi\steward_km1\steward_km1.ino`（注意**不是** `reference/` 只读参考） | `loop_uart()` 的 `case 2:` 分支里，`parse_action()` 之前插入 `strcmp` 精确匹配两个字符串，命中改发 `Serial2.print()`；不命中原逻辑不变。不改 `uart_data_parse`/`handleSerial1`/`handleSerial2` | 用户（arduino-cli 编译烧录） | 见 §7 步骤1 |
| Brain `components/voicelink/`（新组件） | UART2 任务：RX=GPIO2、TX=-1、115200；收满一帧（等到 `!`）严格比对，调用 `armctrl_request_run()`；Kconfig 仿 `armlink`：默认关，GPIO 默认 -1 强制显式分配，注明会触发连续抓取 | Claude（idf-bridge build；flash 当场确认） | 见 §7 步骤2 |
| 接线 | 语音模块→KM1"语音接口"；Brain GPIO2←KM1"openmv接口"TX2(42) | 用户（已完成） | 见 §7 步骤3 |

**GPIO2 选型依据**（已查 ESP32-S3 官方引脚表确认）：Brain 已占用 GPIO1(armlink TX)、摄像头占满 4/5/6/7/8/9/10/11/12/13/15/16/17/18/21；S3 的 strapping 脚是 0/3/45/46（不是经典 ESP32 的 GPIO2，已核实文档）；19/20 是 USB-Serial-JTAG；33-37 是 Octal PSRAM 专用；26-32 是 SPI flash 专用；43/44 是 UART0 console。排除后 GPIO2 是无限制的 P2 级通用脚。用户已确认接线完成。

## 6. 错误处理与安全

- **急停锁存期间语音"开始"无效**：`armctrl_request_run` 内部已对锁存状态做拒绝判断（`on && s_estop` 时直接 return，已读源码确认），和网页按钮同一套规则，`voicelink` 调同一 API 自动继承，不用额外写判断。
- **语音"停止"始终生效**：不受估停锁存影响（停止请求本身安全），随时可执行；但仅缓停，不具备急停的"立即断电锁存"语义。
- **语音链路断开/模块未唤醒/识别失败**：无信号 = 无动作，是安全默认态，不影响网页控制这条独立通路继续工作。
- **半截帧/噪声**：Brain 侧要求收到完整 `!` 结束符后再做**严格全串匹配**，比不上直接丢弃，不会误触发；同理 KM1 转发侧也是精确匹配才转发，不识别的字节不转发。
- **两次发送去重**：语音模块每次识别固定发送同一帧两遍（50ms 间隔）；`armctrl_request_run()` 对相同状态的重复请求本身是幂等操作（网页按钮已允许连点），`voicelink` 不需要额外去重逻辑。
- **默认关闭 + 显式确认**：`voicelink` 的 Kconfig 默认关闭、GPIO 默认 -1 强制显式分配（仿 `armlink` 既有模式）；因为一旦启用，语音信号将可直接触发机械臂连续运动，烧录前必须当场确认（`.claude/hooks/guard.py` 对 flash 本就会拦截确认）。

## 7. 测试计划（分步验证，降低"一把梭"风险）

1. **KM1 转发单测**：Brain 先断电/拔线（避免同时有真实运动帧在共享 RX2 上跑，干扰判断），用 USB-TTL 直接以 115200 波特率向共享 RX2 节点（接"语音接口"或"openmv接口"任一 4 针座的对应数据脚均可，二极管保证不会电气冲突）发送 `#Start!`/`#Stop!`，监听 TX2(GPIO42) 确认原样转发正确。目的：先排除 KM1 转发逻辑本身的 bug，不引入语音识别、也不引入与 Brain 实时流量交织的不确定性。
2. **Brain 解析单测**：不接 KM1，直接用 USB-TTL 向 Brain GPIO2 发送同样两个字符串，看 monitor 日志出现 `voicelink` 收到帧的 ESP_LOGI，且网页 `/arm_run` 状态查询（`gs` 文本）跟着切换。目的：排除 Brain 侧解析/调用 armctrl 的 bug。
3. **端到端联调**：语音模块、KM1、Brain 全部按实际接线通电，实际喊话（"小电" + "开始处理"/"停止"），确认从声音到 Brain 状态变化、到网页显示、到机械臂真实动作的完整链路——**此时 Brain 的真实运动帧和语音帧会真的交织在同一条 RX2 总线上**，是本设计里唯一需要在这种共享条件下验证的场景。
4. **回归检查**：确认接入语音链路后，网页按钮控制、Brain→KM1 正常连续抓取（真实运动帧路径）未受 `loop_uart()` 改动影响——改动只在 `case 2:` 分支加一次内容判断，不匹配语音词的帧原样调用 `parse_action`，理论零风险，仍需一次真机确认。

## 8. 开放项（写代码前需最终确认，不阻塞设计本身）

- ~~语音模块 Serial1 实际波特率数值~~ ✅ 已确认 115200。
- ~~Brain 板上 GPIO2 是否有可用排针~~ ✅ 已确认接线完成（GPIO2 ← KM1 openmv接口 TX2）。
- ~~KM1 Serial1(RX1/TX1) 方案~~ ❌ 已推翻——该板 Serial1 无排针引出，物理不可达；实际方案改为共享 RX2 + `loop_uart()` 分发点判断（见 §2、§4 修正说明）。
- KM1 `steward_km1.ino` 与只读参考 `reference/esp32.ino` 目前逐字节相同——本设计的改动只应用到前者，后者保持只读不动。
