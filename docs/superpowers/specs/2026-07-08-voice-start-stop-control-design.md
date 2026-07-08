# 设计：语音"开始/停止"接入机械臂连续抓取

> **生成**：2026-07-08。
> **前置**：`docs/ai/ARM_PIPELINE.md`（armctrl 运行手册）、`docs/ai/BOARD.md`（Brain 引脚/危险 GPIO）、`docs/ai/SAFETY.md`（硬件安全铁律）、`docs/ai/CONTEST_REQUIREMENTS.md`（2026-07-09 18:00 投稿截止）。
> **用户已拍板**：
> - 语音板 = KM1 套件自带独立语音识别模块（图形化配置、词库可自定义，非简单拍手传感器）。
> - 语义：语音"开始"= 连续抓取拓（等价网页 `arm_run?on=1&cont=1`）；语音"停止"= 缓停（等价网页 `arm_run?on=0`，**不是**急停）。
> - 用户已在语音模块自带图形化工具里配置好：唤醒词"小电"，指令词"开始处理"(识别ID=1)→原样发送 `#Start!`（发两遍，间隔50ms），指令词"停止"(识别ID=2)→原样发送 `#Stop!`（同样发两遍），经模块自身"Serial1"输出。
> - 物理接线约束：语音模块的 TX 线只能/只方便接在 **KM1 板**上（不能直接拉到 Brain 板），所以链路必须经 KM1 转发。
> - KM1 端固件（`D:\WJ\jixiebi\steward_km1\steward_km1.ino`）由用户自己用 **arduino-cli** 编译烧录；Brain 端（ESP-IDF）由 Claude 用 idf-bridge 完成，flash 仍需当场确认。

---

## 1. 一句话

语音模块（唤醔"小电"+指令词，已配置好发 `#Start!`/`#Stop!`）接入 KM1 的空闲 Serial1（RX1=GPIO18）；KM1 新增一段**独立于现有共享解析器**的小状态机，识别到这两个词后原样从 TX1(GPIO17) 转发；Brain 新增 `voicelink` 组件在 UART2(RX=GPIO2) 收到后调用现有 `armctrl_request_run()`——语音只是给 Brain 已有的"识别→规划→抓取→切割→放回"连续抓取状态机换了个触发入口，状态机本身不动。

## 2. 背景：为什么不能直接复用 KM1 现成的指令解析器

读了 KM1 实际固件（`steward_km1.ino`，与只读参考 `reference/esp32.ino` 逐字节一致）后发现：

- 原厂"声控夹取"根本不做语音识别——`shengkong_jiaqu()` 只是 `digitalRead(19)==LOW` 触发固定动作组，语音板只是干触点输出。现在用户的语音板是真正的可配置 ASR 模块，输出改成了 UART 上的 ASCII 帧，架构因此不同，不能照抄原厂那条路。
- KM1 的 `uart_data_parse()` 是**全局共享单状态机**：`handleSerial1()`（RX1=18，当前空闲）和 `handleSerial2()`（RX2=41，接 Brain 下发的运动帧，真实在用）共用同一个 `uart_mode`/`uart_receive_buf`/`uart_get_ok`。`#` 开头会被判进"单动作指令"模式——和 Brain 发的 `#000P1500T2000!` 运动帧**同一条通道**；更关键的是 `if (uart_get_ok) return;`：只要有一帧运动指令还没被 `loop_uart()` 消费，Serial1 传来的字节会被逐字节静默吃掉。连续抓取时 Serial2 高频发帧，语音喊"停止"很可能被直接吞掉、毫无反应。
- 结论：**Serial1 的语音收发必须绕开 `uart_data_parse`/`parse_action`**，单独写一条小路径，才能不受 Serial2 忙时影响，也不会污染现有运动帧解析。

## 3. 范围

**做**：
1. KM1 (`steward_km1.ino`)：`handleSerial1()` 改为独立小逻辑——攒 Serial1 字节、精确匹配 `"#Start!"` / `"#Stop!"`，匹配上原样从 TX1 转发。不碰 `uart_data_parse`/`parse_action`/Serial2 路径。
2. Brain：新组件 `components/voicelink/`，UART2 监听（RX=GPIO2，TX=-1），收到 `#Start!` → `armctrl_request_run(true, true)`（连续拓）；收到 `#Stop!` → `armctrl_request_run(false, ...)`（缓停；`cont` 参数值不影响停止本身——已核实 `armctrl_request_run` 内 `s_continuous=cont` 无条件赋值但仅在下次 `on=true` 时才被读取，具体传什么留给实现阶段对齐网页按钮的做法）。Kconfig 仿 `armlink` 模式（默认关，GPIO 默认 -1 强制显式分配）。
3. 新接线：语音模块 TX → KM1 RX1(GPIO18)（模块侧已完成配置，接线待做）；新增 KM1 TX1(GPIO17) → Brain GPIO2（新线）。
4. 分步验证计划（§7）。

**不做**（明确排除，防范围蔓延）：
- 不新增语音急停能力——急停仍只走网页红键 + `$DST:0!`（已验证过的唯一急停串）。语音"停止"是缓停，语义上不允许和急停混淆。
- 不处理语音模块图形化配置里其余识别词（如"关灯"/ID10，vendor demo 遗留），只接管"开始处理"/"停止"这两个已确认的词。
- 不改 KM1 现有 `ai_mode`/`loop_Function()` 那套旧 demo 状态机（颜色分拣/定距夹取/红外触发/触摸夹取）——语音入口直连 Brain 的 `armctrl`，不经过 KM1 自己的旧状态机。
- 不改 Brain→KM1 现有单向运动帧链路（UART1, TX=GPIO1, RX=-1）——`voicelink` 是全新的第二条独立 UART，不复用/不修改 `armlink`。

## 4. 架构

```
语音模块 TX ──► KM1 RX1(GPIO18) @115200            [新接线：模块→KM1]
                  │
                  │  KM1 .ino 新增独立小状态机（绕开 uart_data_parse）
                  │  精确匹配 "#Start!" / "#Stop!" → 原样转发
                  ▼
              KM1 TX1(GPIO17) ──► Brain GPIO2(UART2 RX) @115200   [新接线：KM1→Brain]
                                        │
                                        │  Brain 新组件 voicelink：收满一帧、比对
                                        ▼
                              armctrl_request_run(true/false, ...)
                                        │
                              （复用现有连续抓取状态机，与网页 /arm_run 同一入口，
                               估停锁存/单轮-连续语义完全一致）
```

关键点：KM1 只做"收+转发"两件事，不做任何业务判断；真正的启停语义、急停互锁全部复用 Brain 现有的 `armctrl_request_run()` / `armctrl_is_estopped()`，与网页按钮走同一入口，行为保证一致，不用重新实现一遍状态机。

## 5. 组件与改动、分工

| 位置 | 改动内容 | 谁做 | 验证方式 |
|---|---|---|---|
| 语音模块图形化配置 | 已完成：唤醒词"小电"，ID1→`#Start!`(×2)，ID2→`#Stop!`(×2)。**待确认**：其 Serial1 波特率须设为 115200（与 KM1 现有 `Serial1.begin(115200,...)` 一致，否则乱码） | 用户 | 用户核对配置工具里的波特率设置项 |
| `D:\WJ\jixiebi\steward_km1\steward_km1.ino`（注意**不是** `reference/` 只读参考） | `handleSerial1()` 改写：独立小缓冲区 + 状态机，精确匹配两个字符串，匹配后 `Serial1.print()` 原样转发（TX1 出）。不改 `uart_data_parse`/`parse_action`/`handleSerial2` | 用户（arduino-cli 编译烧录） | 见 §7 步骤1 |
| Brain `components/voicelink/`（新组件） | UART2 任务：RX=GPIO2、TX=-1、115200；收满一帧（等到 `!`）严格比对，调用 `armctrl_request_run()`；Kconfig 仿 `armlink`：默认关，GPIO 默认 -1 强制显式分配，注明会触发连续抓取 | Claude（idf-bridge build；flash 当场确认） | 见 §7 步骤2 |
| 接线 | 语音模块 TX→KM1 RX1(18)；新增 KM1 TX1(17)→Brain GPIO2 | 用户（物理接线） | 见 §7 步骤3 |

**GPIO2 选型依据**（已查 ESP32-S3 官方引脚表确认）：Brain 已占用 GPIO1(armlink TX)、摄像头占满 4/5/6/7/8/9/10/11/12/13/15/16/17/18/21；S3 的 strapping 脚是 0/3/45/46（不是经典 ESP32 的 GPIO2，已核实文档）；19/20 是 USB-Serial-JTAG；33-37 是 Octal PSRAM 专用；26-32 是 SPI flash 专用；43/44 是 UART0 console。排除后 GPIO2 是无限制的 P2 级通用脚，逻辑上空闲。**用户需确认板子该脚实际有排针可接**（无法远程看实物）。

## 6. 错误处理与安全

- **急停锁存期间语音"开始"无效**：`armctrl_request_run` 内部已对锁存状态做拒绝判断（`on && s_estop` 时直接 return，已读源码确认），和网页按钮同一套规则，`voicelink` 调同一 API 自动继承，不用额外写判断。
- **语音"停止"始终生效**：不受估停锁存影响（停止请求本身安全），随时可执行；但仅缓停，不具备急停的"立即断电锁存"语义。
- **语音链路断开/模块未唤醒/识别失败**：无信号 = 无动作，是安全默认态，不影响网页控制这条独立通路继续工作。
- **半截帧/噪声**：Brain 侧要求收到完整 `!` 结束符后再做**严格全串匹配**，比不上直接丢弃，不会误触发；同理 KM1 转发侧也是精确匹配才转发，不识别的字节不转发。
- **两次发送去重**：语音模块每次识别固定发送同一帧两遍（50ms 间隔）；`armctrl_request_run()` 对相同状态的重复请求本身是幂等操作（网页按钮已允许连点），`voicelink` 不需要额外去重逻辑。
- **默认关闭 + 显式确认**：`voicelink` 的 Kconfig 默认关闭、GPIO 默认 -1 强制显式分配（仿 `armlink` 既有模式）；因为一旦启用，语音信号将可直接触发机械臂连续运动，烧录前必须当场确认（`.claude/hooks/guard.py` 对 flash 本就会拦截确认）。

## 7. 测试计划（分步验证，降低"一把梭"风险）

1. **KM1 转发单测**：不接真语音模块，用 USB-TTL 直接以 115200 波特率向 RX1(GPIO18) 发送 `#Start!`/`#Stop!`，用示波器/另一 USB-TTL 监听 TX1(GPIO17) 确认原样转发正确。目的：先排除 KM1 转发逻辑本身的 bug，不引入语音识别的不确定性。
2. **Brain 解析单测**：不接 KM1，直接用 USB-TTL 向 Brain GPIO2 发送同样两个字符串，看 monitor 日志出现 `voicelink` 收到帧的 ESP_LOGI，且网页 `/arm_run` 状态查询（`gs` 文本）跟着切换。目的：排除 Brain 侧解析/调用 armctrl 的 bug。
3. **端到端联调**：语音模块接 KM1、KM1 接 Brain，实际喊话（"小电" + "开始处理"/"停止"），确认从声音到 Brain 状态变化、到网页显示、到机械臂真实动作的完整链路。
4. **回归检查**：接入后确认网页按钮控制、Brain→KM1 正常连续抓取（Serial2 路径）未受 Serial1 改动影响——KM1 改动只触碰 `handleSerial1`，理论零风险，仍需一次真机确认。

## 8. 开放项（写代码前需最终确认，不阻塞设计本身）

- 语音模块 Serial1 实际波特率数值（需与 KM1 的 115200 一致，用户去配置工具里核实）。
- Brain 板上 GPIO2 是否有可用排针（GPIO 选型基于代码排除法，未见实物）。
- KM1 `steward_km1.ino` 与只读参考 `reference/esp32.ino` 目前逐字节相同——本设计的改动只应用到前者，后者保持只读不动。
