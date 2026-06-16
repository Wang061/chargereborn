# ChargeReborn · Brain 阶段一收敛(一):相机点亮 + 数据集采集工具

> **生成**:2026-06-16
> **上位 spec**:`docs/superpowers/specs/2026-06-13-chargereborn-brain-phase1-vision-spine-design.md`(§6 收敛步骤的第一刀)
> **配套**:`PROJECT_BLUEPRINT_GOLD.md`(What/Why)、`BUILD_STEPS_DETAILED.md`(How)、`docs/ai/BOARD.md`(板/引脚)
> **状态**:已与用户协商定稿,待用户复核 → 转 writing-plans 出施工计划
> **范围**:仅 Brain 板 ESP32-S3-WROOM-1-N16R8;只做"取帧 + 图传采数据",不含 AI 推理

---

## 0. 一句话

让 S3 用**排针 + 杜邦线**点亮相机 → 浏览器**实时 MJPEG 取景对焦** + **一键抓拍下载 JPEG**,当天就能开始拍电池攒数据集。把下游最长的线(采数据 + 标注)尽早开起来,是 MVP 的限速环节。

---

## 1. 背景与定位

- **赛程**:乐鑫赛道,硬截止约 2026-07-06 ~ 07-13,距本稿约 3 周。
- **现状(本稿日)**:
  - **Track A 固件地基 = 已完成**:`bsp`(芯片/flash 横幅 + PSRAM 8MB Octal 实测 alloc+rw OK)、`net`(WiFi softAP `chargereborn` + HTTP `/`、`/status`)、`main.c` 编排。
  - **Track B AI 链路 = 已排雷**:已在本块 S3 上跑通官方猫例程 训练→量化→`.espdl`→部署→板上推理(cat score 0.88)。最大未知已消除。
  - **相机已到货**:OV5640(5MP)+ OV2640(2MP)的 **DVP 模块 + 排针 + FPC 排线**。OpenMV 那颗 `OV5640-MOD R3`(B2B 座)接不到 DevKitC,作废。
- **本刀定位**:上位 spec §6 收敛链 `相机取帧 → 图传采数据 → 标注 → 训练 → 部署 → MVP` 的**第一环**。采数据 + 标注是纯手工长线任务,因此"先让画面流起来"是当前最高杠杆、且不依赖任何其他未完成项的开发。

---

## 2. 范围

**做(本刀):**
- 新增 `components/camera/`:基于 `espressif/esp32-camera` 托管组件,取帧(JPEG 直出,帧缓冲住 PSRAM),SCCB 自动探测传感器并打印 PID。
- 扩 `components/net/`:`/stream`(multipart MJPEG 实时流)+ `/capture`(单张 JPEG,`attachment` 下载,文件名带类名前缀)。
- `main.c`:init 顺序加 `camera_init()`,失败有清晰诊断、不硬崩。
- 一版**经官方文档逐脚核对**的相机接线表(排针 → DevKitC GPIO)。

**不做(延后,各自后续处理):**
- 板上 AI 推理(ESPDet-Pico)与 `.espdl` 模型部署 —— 上位 spec §6 后续环,数据到位后单独成刀。
- `partitions.csv` factory 扩容 + model 分区 —— AI 模型上板时再做(本刀固件小,1MB factory 够)。
- 批量"自动每 N 秒抓拍"连拍 —— v1 先手动一键抓拍(YAGNI);确有需要再加小开关。
- 板载存储(SD/flash 存图)—— DevKit 无 SD 卡槽,抓拍走浏览器下载到 PC 即可。
- OV5640 锁定为最终相机 —— 先用 OV2640 看真实画面后再决定(见 §4)。
- 视觉伺服 / HMI / 语音 / APP / 双 S3 等 —— 上位 spec §10 延后清单不变。

---

## 3. 架构与组件契约

沿用上位 spec §4 分层,依赖单向 `main → components`,驱动不向业务层泄漏寄存器/HTTP 细节。

```
WORKplace/
├── main/main.c                 # 编排:bsp → psram自检 → camera → net(softAP+http)
├── components/
│   ├── bsp/                    # (已有,不改)
│   ├── camera/                 # 新增:取帧驱动(封装 esp32-camera)
│   │   ├── idf_component.yml    #   依赖 espressif/esp32-camera
│   │   ├── include/camera.h
│   │   ├── camera.c            #   camera_init / camera_capture / camera_return + 引脚宏
│   │   └── CMakeLists.txt
│   └── net/                    # 扩:加 /stream + /capture
│       ├── include/net.h        #   加 net_http_start 内注册流端点(对外接口不变)
│       ├── http_srv.c          #   加 stream_get / capture_get
│       ├── wifi_ap.c           #   (已有,不改)
│       └── CMakeLists.txt       #   PRIV_REQUIRES 加 camera
```

**契约:**
- `camera`:
  - `esp_err_t camera_init(void)` —— 初始化 DVP + SCCB,自动探测传感器,日志打印 PID 与生效分辨率;失败返回错误码并打印诊断,**不 abort**。
  - `camera_fb_t *camera_capture(void)` —— 取一帧(JPEG);失败返回 NULL。
  - `void camera_return(camera_fb_t *fb)` —— 归还帧缓冲(`esp_camera_fb_return`)。
  - 依赖 `esp32-camera` + PSRAM;隔离引脚/格式细节,业务层只见上述 3 个 API。
- `net`(扩):
  - `/stream` —— `multipart/x-mixed-replace`,循环 `camera_capture → 发送 JPEG → camera_return`。
  - `/capture` —— 取一帧 JPEG,`Content-Disposition: attachment; filename="<类名>_<ms>.jpg"` 触发浏览器下载;类名取自查询参数(缺省 `cap`)。
  - `/` 存活页升级为采集页(见 §6):内嵌 `<img src=/stream>` + 类名输入框 + 抓拍按钮。
  - net 从 camera **拉**帧,camera 不知道 web 存在。
- `main`:只编排与状态机;`camera_init` 失败时跳过流端点注册并持续打印告警心跳(便于查接线),不进 boot loop。

---

## 4. 传感器策略:OV2640 先行,同固件两颗通吃

两颗模块同为标准 DVP 接口,`esp32-camera` 经 SCCB 自动识别传感器 ID,**同一份固件两颗都能驱动**。

- **先点 OV2640**(像素时钟低、`esp32-camera` 标准目标、飞线最稳),把取帧→图传→抓拍全管道跑通。
- 拍到你**真实电池**的画面后再判断:OV2640 的 2MP 画质能否清楚区分 5-6 类电池。
  - 够 → 锁 OV2640 为最终相机(飞线风险最低)。
  - 不够(需看清表面印字)→ 换 OV5640 模块,必要时调 XCLK / 分辨率;固件不需重写。
- 之所以把"选最终相机"推到有真实画面之后:bring-up 工作量两颗一样,先有画面再决定,避免空想拍板。

---

## 5. 接线与引脚(排针 → DevKitC)

DVP 相机需 16 路信号:`XCLK / PCLK / VSYNC / HREF / SIOD(SDA) / SIOC(SCL) / D0–D7(8) / PWDN / RESET`,外加 `3V3 / GND`。多数模块 `PWDN`、`RESET` 可不接(置 -1,用 SCCB 软复位),则需 14 路 GPIO。

**候选引脚池(蓝图选定,本稿未逐脚核 → plan 第 1 步官方锁定):**
`4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18`(共 14,正好覆盖 14 路;`PWDN=-1`、`RESET=-1`)。

**硬约束(必须避开,改前核对 `docs/ai/BOARD.md` + 官方 DS):**
- **GPIO35 / 36 / 37** —— Octal PSRAM 专用,禁用。
- **GPIO19 / 20** —— 原生 USB-Serial-JTAG,占用会丢 flash/调试口。
- **SPI flash / PSRAM 占用脚段(GPIO26–37 区)** —— 禁用。
- **strapping 脚 0 / 3 / 45 / 46** —— 尽量不用作相机信号;必须用时核对上电默认电平。
- 候选池 `4–18` 均 ≤18,落在上述禁区之外;落地时用 `espressif-documentation` MCP 逐脚复核后写死接线表(项目铁律:不凭记忆定 ESP 引脚)。

**接线纪律:** 飞线**尽量短**(高时钟下信号完整性);`3V3` 供电就近、`GND` 共地多接;XCLK 取适中频率(bring-up 阶段不追高)。

---

## 6. 取景 / 抓拍 UX 与数据流

**采集页(`http://192.168.4.1/`):**
- 实时画面:`<img src="/stream">`(MJPEG)用于对焦、构图。
- 类名输入框(如 `18650` / `lipo` / `aa` ...)。
- 「抓拍」按钮 → 请求 `/capture?name=<类名>` → 浏览器下载 `<类名>_<时间戳ms>.jpg`。

**数据流:**
```
传感器 → DVP → esp32-camera(JPEG 直出, 帧缓冲在 PSRAM)
     ├─ /stream  : 循环取帧→multipart 推送→归还(实时取景)
     └─ /capture : 取一帧→当附件下发(浏览器另存)→归还
PC 端按类名分文件夹收图 → 后续 YOLO 标注直接用
```

**采集分辨率:** 默认 **VGA 640×480**(够清晰又轻;模型输入 224 后续 resize,采集端不必等于模型端)。OV5640 可上更高分辨率;分辨率/质量做成 `camera.h` 里的命名常量,便于调。

---

## 7. WiFi 模式

- **默认 softAP**(`chargereborn`,已就绪、无路由依赖、现场可用)。代价:PC 连 AP 时断外网。
- **可选 STA**(板子连家用 WiFi,PC 同网段访问、保留外网)—— 桌前采集更顺手;作为一句话可加的后续开关,不在本刀默认范围。

---

## 8. 错误处理与资源

- `camera_init` 失败(多为接线错/传感器未识别)→ 打印 PID/错误码诊断,跳过流端点,主循环持续告警心跳,**不 abort、不 boot loop**。
- 帧缓冲:`CAMERA_FB_IN_PSRAM`,`fb_count=2`,`grab_mode=LATEST`,`pixformat=JPEG`(省 RAM、直出可传)。
- 每次 `camera_capture` 必配对 `camera_return`,杜绝帧缓冲泄漏拖垮 PSRAM。
- 供电:相机吃电流,`3V3` 供,留意 brownout(必要时看 `brownout` 复位日志,降分辨率/换供电)。

---

## 9. 验证标准(按 `.claude/rules/verification.md`)

| 步骤 | 必做验证 |
|---|---|
| 加 camera/net 代码 | `idf-bridge`(或 `scripts/idf.ps1`)build 绿;首次拉 `esp32-camera` 托管组件需联网 |
| 改 init/启动路径 | flash(当场确认;**纯取景无执行器,安全**)+ monitor:见传感器 PID、生效分辨率、无 boot loop、PSRAM 帧缓冲 alloc OK |
| 加并发(stream 任务 + httpd) | flash + monitor 看 WDT/栈溢出/堆;`/stream` 持续推流不崩 |
| 功能实测 | 手机/PC 连 `chargereborn` → 开页见实时画面 → 抓拍下载到一张 `<类名>_<ms>.jpg` |

panic 必须解码回溯(addr2line / `coredump_summary`)定位到 函数 + file:line 再改。不把"看起来对"当"验证过"。

> 注:本机 `idf-bridge` build/monitor 曾挂死;按持久记忆 `dev-flow-build-flash-monitor`,build 走 `scripts/idf.ps1`、monitor 走 .NET SerialPort 抓 **COM11**、flash 用 **COM7**(`ESPPORT=COM7`)。flash 每次当场确认(BOARD.md 允许 AI flash = NO)。

---

## 10. 风险与护栏(诚实)

- **飞线信号完整性**(高时钟)→ OV2640 先行 + 短线 + XCLK 适中;翻车按 降分辨率 → 降 XCLK → 换 OV2640 兜底。
- **接线错**是首次 bring-up 头号失败源 → `camera_init` 诊断日志为抓手;逐脚对照锁定的接线表。
- **供电/brownout** → 见 §8。
- **版本锁**:全程 IDF 5.5.4 / target esp32s3;`esp32-camera` 取兼容 5.5 的托管版本,不升 IDF 主版本、不换 target。
- **ESP 代码/引脚/寄存器** → 落地用 `espressif-documentation` 官方核对,不凭记忆(项目铁律)。

---

## 11. 延后清单(本刀之后,各自单独处理)

板上 AI 推理 + `.espdl` 部署 · `partitions.csv` 扩 factory + model 分区 · 批量自动连拍 · 板载存图 · STA 模式 · OV5640 锁定 · 视觉伺服 / HMI / 语音 / APP / 双 S3(上位 spec §10)。

---

## 12. 开放项与默认(用户复核时可改)

- **先行传感器**:默认 **OV2640**(理由见 §4)。
- **WiFi**:默认 **softAP**;STA 后补。
- **抓拍**:默认 **手动一键**;自动连拍延后。
- **采集分辨率**:默认 **VGA 640×480**,做成命名常量可调。
- **引脚**:候选池 `4–18`,plan 第 1 步官方逐脚核对后写死接线表。
