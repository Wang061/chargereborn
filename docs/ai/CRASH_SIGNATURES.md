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
