# CRASH_SIGNATURES.md — 崩溃签名库（/learn 沉淀）

每修好一个 bug，用 `/learn` 追加一条：**症状 → 根因 → 修法 → 预防**。
让高频故障变成可检索资产；下次 monitor/triage 先查这里。

格式：

```
### <短标题>  (日期)
- 症状: <panic 关键字 / 现象 / 回溯特征>
- 根因: <真正原因>
- 修法: <最小修改 + 文件:行>
- 预防: <规则/检查/skill，避免再犯>
- 标签: #wdt #stack #heap #flash #i2c #freertos ...
```

---

<!-- 示例（删除或保留作模板）
### Task watchdog on app_main blocking  (2026-01-01)
- 症状: "Task watchdog got triggered. ... CPU 0: main"
- 根因: app_main 里忙等未让出，触发 TWDT
- 修法: 用 vTaskDelay/事件驱动替换忙等（main/app_main.c:NN）
- 预防: 见 rules/coding-standard.md「避免忙等」；freertos-task-design skill
- 标签: #wdt #freertos
-->

### KM1(Open-CESP) 语音/openmv接口共享同一条 RX2 总线，Serial1 无排针引出  (2026-07-08)
- 症状: 设计阶段基于固件源码反推"Serial1(RX1=GPIO18/TX1=GPIO17) 看起来空闲"，据此设计语音模块接 KM1 Serial1、独立于运动帧通道的转发方案；代码写完、编译通过、烧录成功，用户反馈"板子上没找到 IO17 的位置"——物理排针根本不存在。
- 根因: 核对原理图（`D:\WJ\jixiebi\5.软件工具\1.原理图\OpenCESP.pdf`，KM1=Open-CESP V1.2）后发现，KM1 的"语音接口"和"openmv接口"（Brain 所接）经二极管共享同一条 RX2(GPIO41)，TX2(GPIO42)则直接扇出到语音接口/openmv接口/蓝牙接口/用户接口等多个连接器。Serial1 在这块板子上根本没有排针引出。"代码里定义的引脚"不等于"板子上实际引出的排针"，对非标准 DevKit 的第三方小板子（教育/竞赛套件配套主控板）尤其如此。
- 修法: 撤销 Serial1 独立通道方案，改为在 KM1 现有的 `loop_uart()` 分发点（处理共享总线上已攒好的 `"#...!"` 帧的地方）做内容匹配——精确字符串比较区分语音指令帧(`#Start!`/`#Stop!`)和真实运动帧(`#dddPddddTdddd!` 格式)，命中语音指令就转发，不命中走原有 `parse_action` 逻辑。零新增接线，只改一处已有的分发点。见 `docs/superpowers/specs/2026-07-08-voice-start-stop-control-design.md` 的"2026-07-08 当日修正"。
- 预防: 给不熟悉的第三方硬件分配引脚/设计通信方案前，优先索取/查阅原理图，或让用户核对物理排针确实存在，不要仅凭 `.ino`/固件源码里的 `#define`/引脚映射做设计决策。
- 标签: #uart #km1 #hardware-assumption #schematic #pinout

### arduino-cli 改源码不加 --clean 会复用旧编译缓存，体积/日志看不出来  (2026-07-08)
- 症状: 排查"KM1 烧录后机械臂初始姿态异常疑似固件回归"时，做单变量控制测试——把 KM1 重新烧成逐字节原版固件排除嫌疑；`arduino-cli upload` 日志显示 "No changed sectors found, verifying if data is in flash"，误以为已经烧了新内容，实际很可能仍是旧编译产物。
- 根因: 源码内容改变（含"改回原版"这种反向改动）但不加 `--clean` 时，`arduino-cli compile` 会命中旧编译缓存，不会真正重新编译；`compile` 报的字节数/`upload` 的"No changed sectors"提示都不能证明这次内容确实变了。
- 修法: 用 `arduino-cli compile --fqbn <board> --clean <sketch>` 强制干净重编译，通过编译产物体积（如本例 400783 字节 vs 之前被污染读数）交叉核实内容确实变了，再 `upload`。
- 预防: 每次改动 `.ino` 源码后（含"改回上一版"）编译都带 `--clean`；不要用"体积没变/日志没报错"当作"内容没变"的证据，必要时直接对比源文件 diff/hash。
- 标签: #km1 #arduino-cli #build-cache #methodology

### S3 flash 后卡 DOWNLOAD 模式不进 app —— USB-Serial-JTAG 复位不重采样 strapping  (2026-07-05)
- 症状: esptool 烧录成功（hash 校验过 + "Hard resetting via RTS pin... Done"），但之后 monitor 只反复打印 `rst:0x15 (USB_UART_CHIP_RESET),boot:0x23 (DOWNLOAD(USB/UART0))` + `waiting for download`，app 永远不启动。看起来像"烧成砖了"，实际固件完好。日志 `logs/monitor/20260705_reset_test.log`。
- 根因: ESP32-S3 **原生 USB-Serial-JTAG 只能触发 core reset，不会重新采样 GPIO0 boot-strapping 脚**（乐鑫官方文档确认）。烧录序列把芯片带进 download 模式后，RTS hard_reset 走的是 core reset 路径，download 状态被锁存，无论复位多少次都回到 ROM bootloader。与固件内容无关——不要去改代码/重烧。
- 修法: 跑任意一条带 `--after watchdog_reset` 的 esptool 只读命令触发**真·全芯片 watchdog 复位**（会重采样 strapping）：`python -m esptool --chip esp32s3 --port COM7 --after watchdog_reset read_mac`。注意本机 esptool v4.12.dev1 参数是**下划线** `watchdog_reset`，连字符写法会被 argparse 拒。执行后板子立即正常进 app。
- 预防: 经原生 USB 口（COM7）烧录后若 monitor 出现 `boot:0x23 DOWNLOAD ... waiting for download` 循环，第一反应就是 watchdog_reset，别怀疑固件；可考虑把 `--after watchdog_reset` 直接加进 flash 命令/idf-bridge 封装。抓 boot 日志时**先启动 capture 再触发复位**（复位极快，后启动 capture 必漏开头）。
- 标签: #flash #usb-serial-jtag #strapping #bootmode #esptool

---

### YOLOv8n 4类模型上板 5.8s/帧 → task_wdt 每周期触发  (2026-07-04)
- 症状: flash 后 monitor 每周期 `task_wdt: ... IDLE0 (CPU 0)` + `Tasks currently running: CPU 0: detect`，`detect: n=0 infer=5825ms`（重复回溯、CPU0 饥饿）；WiFi AP station drop（reason=8）；板子进降级循环但不复位。日志 `logs/monitor/20260704_155154.log`。
- 根因: 队友模型是**全尺寸 YOLOv8n backbone**（3.0M 参数 / 8.1 GFLOPs），非 MCU 特化架构。ESP32-S3@160MHz int8 实测 5.8s/帧，vs ESPDet-Pico（354K 参数，为该 MCU 设计）219ms —— 26x。1→4 类不需要 8.5x 参数量，是 backbone 选型错误；量化调参/包装代码**无法修复架构级超载**（导出侧 6 分离输出、letterbox 校准这次都做对了，照样 5.8s）。
- 修法: 回退 `CONFIG_AI_DETECTOR_ESPDET`（sdkconfig 实际值 + `components/ai/Kconfig` 默认值双双回 espdet），16:24 重烧、16:26 monitor 确认 219ms 稳定。battery_yolo 组件留档禁用（Kconfig help 已标注勿选）。4 类能力改由 **ESPDet-Pico 架构重训**接棒（分支 feat/battery-espdet4，数据复用队友 4 类标注集）。
- 预防: 任何新模型上板前先做**算力预算**：参数量/GFLOPs 对照已验证基线线性外推（354K@219ms → 3.0M ≈ 3.6s 起步，必炸 5s TWDT）；先板上单帧 benchmark，再接进 detect 循环；MCU 部署只选 MCU 特化架构（ESPDet/PicoDet 系），通用 YOLO 系一律先过预算关。跨人协作交付模型时把"目标板算力预算"写进需求，而不只是类别数和 mAP。
- 标签: #wdt #model #sizing #espdet #yolo

---

### 机械臂测试帧不动 —— 真机 KM1 固件 $KMS: 自解算未实现  (2026-07-02)
- 症状: 点"机械臂测试帧"按钮，Brain 经 UART1 发出 `$KMS:0,120,80,1000!`（日志确认已发出、KM1 侧确认已收到），但机械臂完全不动；此前 OpenMV 版本能正常驱动。
- 根因: COM4 直连 KM1（绕开 Brain/WiFi，走同一 `serialEvent()`→`uart_data_parse()`→`parse_cmd` 解析链）三帧对照实测：`$KMS:...!` 只回显输入、之后无任何响应（`$KMS:` 分支内 `sscanf` 未匹配任何字段）；`$DST:0!`（内部重路由到 `#000PDST!`）与裸协议 `{#004P1500T1000!}` 均正常回显并派发。链路其余环节（Brain 发送、KM1 接收、`parse_cmd`、`parse_action` 舵机派发）全部正常——真机固件里就没有可用的 `$KMS:` 运动学自解算实现（对应源码分支系后加，未烧进当前真机）。
- 修法: 测试帧改发已验证可动的裸协议序列：腕舵机(#004) 中位→摆动(~20°)→回中位，3 帧 `{#004Pxxxx T0600!}`，不再依赖 `$KMS:`。`components/armlink/armlink.c` 新增 `armlink_send_wrist_pwm()` 小工具 + 重写 `armlink_send_test_frame()`；`armlink.h` 同步更新函数注释。已 build+flash+串口日志+用户物理确认（腕舵机可见摆动）通过。
- 预防: "帧已发送但设备无响应"类问题，怀疑接收端解析层时，优先用独立直连（本例 COM4 直连 KM1 USB 口，绕开中间链路）对同一条命令做多帧交叉对照（可疑命令 vs 已知能通的替代命令 vs 最底层裸协议），只信物理实测回显；不要假设源码里存在某分支就等于它在真机固件里已编译生效。真机 IK 常量供 Phase 2 参考：`setup_kinematics(100,105,75,180,&kinematics)` → L0=100,L1=105,L2=75,L3=180mm。
- 标签: #uart #armlink #km1 #protocol #root-cause

---

### /detect 轮询卡死 / httpd 任务栈溢出  (2026-06-18)
- 症状: 浏览器点"识别开始"(每 180ms 轮询 `/detect`)即卡死/视频冻结;`detect_task` 串口仍正常打印(板不一定复位)。
- 根因: `detect_get` handler 栈占用超 httpd 默认 4096 栈。`ai_box_t` 加 `angle_deg/anisotropy` 后 `ai_result_t r`≈336B + `char buf[1024→1536]` ≈ 1872B 局部 + httpd 框架开销 → 撑爆 4096。
- 修法: `net_http_start()` 加 `config.stack_size = 8192;`(components/net/http_srv.c)。与 81 口 stream server 一致。
- 预防: 在 httpd handler 放大局部缓冲/结构体前核对 `HTTPD_DEFAULT_CONFIG().stack_size`(=4096);大缓冲用 static/堆;改 `ai_result_t`/`ai_box_t` 体积时注意所有栈上拷贝点。
- 标签: #stack #httpd #freertos

---

### OV2640 花屏(画面撕裂成两半+偏色)/取帧超时 —— 2026-07-07 突发，跨复位间歇复现  (2026-07-07，**未根治**)
- 症状: 视频流画面水平撕裂成上下/左右两截、色彩整体错乱(色度错位)；串口伴随成串 `cam_hal: Failed to get frame: timeout` + `esp_camera_fb_get returned NULL`(单场 85~163 条)，检测断流最长 86s；病发时模型在花屏帧上产出乱类别幽灵框(AA/9V/21700 翻转、score 0.27~0.73、σ_cy≈29px)或直接 n=0。**同一天内有的 boot 干净(0 失败)、有的 boot 带病**——boot 级随机。
- 根因: **未定位**。已排除代码回归(07-06 两场烧录后检查日志 0 次取帧失败，其间相机代码零改动)。症状=DVP 帧同步错位(丢 VSYNC 后字节错排)，头号嫌疑是当天动过机架/排线导致的**物理接触不良或供电边际**(当天还出现过一次非人为 POWERON 复位，供电嫌疑加重)。
- 修法(临时): 按 RST 复位重启碰运气，直到抽到干净 boot(实测干净 boot 全程稳定)。
- 预防/待办: ①演示前断电重插相机排线/接头并检查供电(换独立供电或好线)；②候选软件自愈=连续 N 次 fb_get NULL 时 esp_camera deinit+reinit 重同步(**未实现**，注意 stream/capture/detect 三处并发持 fb 的释放时序)；③判断 boot 好坏的快速方法=看串口头 60s 有无 `cam_hal: Failed` 串。
- 标签: #camera #ov2640 #dvp #hardware #open
