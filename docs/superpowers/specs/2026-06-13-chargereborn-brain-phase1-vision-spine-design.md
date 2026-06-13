# ChargeReborn · Brain 阶段一设计稿:边缘 AI 视觉识别主轴

> **生成**:2026-06-13
> **配套**:`PROJECT_BLUEPRINT_GOLD.md`(What/Why)、`BUILD_STEPS_DETAILED.md`(How)
> **状态**:已与用户协商定稿,待用户复核 → 转 writing-plans 出施工计划
> **范围**:仅新板 ESP32-S3-WROOM-1-N16R8(蓝图"Brain"),独立于机械臂成品(蓝图"Steward")

---

## 0. 一句话

在新 S3 板上,独立打通 **"摄像头 → 板载 ESPDet-Pico 推理 → 实时框出电池型号 + 置信度"** 这条边缘 AI 主轴——它是乐鑫赛道的评分心脏,也是 3-4 周冲刺里**唯一必须先跑通**的核心。其余功能全部延后为加分层。

---

## 1. 背景与现状差距

- **项目**:ChargeReborn —— 双 ESP32-S3 退役锂电池安全拆解智能预处理系统,冲 2026 全国大学生嵌入式芯片与系统设计竞赛·乐鑫赛道国一。硬截止约 2026-07-06 ~ 07-13(距本稿约 3-4 周)。
- **现状**:`WORKplace` 仍是 hello_world,功能固件未起步;这段时间建成的是 S3-Forge 开发 harness(规则 / MCP / BOARD / hooks / Core Dump)——很扎实,但属**脚手架,非产品功能**。按蓝图日历落后约 3 周。
- **结论**:GOLD 全套(双S3 + AI + 视觉伺服 + HMI + 语音 + APP + PCB + 3D)在剩余时间内**不可能**完成。必须残酷分诊,先保评分心脏。

---

## 2. 关键决策(本次协商记录)

| 决策点 | 选择 | 理由 |
|---|---|---|
| **总范围** | 只做 Brain 视觉主轴,其余延后 | 全做 = 全废;主轴是赛道核心,且单板可做、不依赖机械臂 |
| **视觉芯片** | **AI 留 ESP32-S3**(否决 SG2002 / YOLO 外挂) | ① 乐鑫赛规:主要功能 / AI 必须跑乐鑫芯片,换算能 SG2002 ≈ 出局;② 3-4 周换 Linux + TPU-MLIR 平台 = deadline 自杀,且弃掉已就绪的 S3 harness;③ 任务窄(5-6 类受控电池、固定俯拍、可控光),S3 + ESPDet-Pico 够用;精度由数据/打光/对焦决定,而非芯片 TOPS |
| **摄像头** | **OV5640(主,模块/排针版)+ OV2640(备)** | 手上 OpenMV 那颗(OV5640-MOD,星瞳/SingTown 出品)是 B2B 连接器,接不到 S3 DevKitC;OV5640 是 S3 上"有余量"的上限(S3 仅 DVP、无 MIPI-CSI);OV2640 兜单点故障。**2026-06-14 到货** |
| **训练环境** | GPU PyTorch(CUDA 13.0),CPU 兜底 | 用户有 GPU;ESPDet-Pico 仅 0.36M 参数,CPU 也可训(几分钟/epoch) |
| **工程结构** | 在 `WORKplace` 内增量长出 Brain,组件化 | harness 已 wire 到此根目录;不开 01_/03_/04_ 散工程 |
| **本周节奏** | **路线1 · 并行**:固件地基 ∥ AI 链路提前排雷 | 摄像头在途 = 送的时间,正好打掉最大未知(训练→量化→部署链路能否在本机+本板跑通),且零摄像头依赖 |

---

## 3. 范围

**做(本期 spec):**
- 摄像头取帧(OV5640/OV2640,esp32-camera 托管组件)
- 板载 ESPDet-Pico 推理(esp-dl / esp-detection),实时输出 电池类别 + 置信度 + bbox
- 配套 WiFi 图传(采数据 + demo 画面)
- PSRAM / flash 启动自检、组件化骨架
- AI 链路:训练 → 量化(.espdl)→ 部署到 S3 → 板上推理,全程打通

**不做(延后,各自单独开 spec):**
视觉伺服(机械臂联动)· HMI / LVGL · ESP-SR 离线语音 · MQTT / 上云 · Flutter APP · 自制 PCB · 3D 打印 · 双 S3 ESP-NOW

---

## 4. 架构与组件(在 WORKplace 内组件化)

换掉 hello_world,按职责分层。依赖方向单向:`main → components`,驱动不向业务层泄漏寄存器细节。

```
WORKplace/
├── main/                  # 薄:初始化编排(init bsp → net → camera → ai loop)
├── components/
│   ├── bsp/               # 板级支持:引脚(按 BOARD.md)、状态灯心跳、PSRAM/flash 自检、日志 TAG 规范
│   ├── net/               # WiFi(先 softAP)+ HTTP 帧流服务(采数据 / demo)
│   ├── camera/            # OV5640/OV2640 取帧 API（esp32-camera 托管组件）← 摄像头到货填实
│   └── ai/                # ESPDet-Pico 推理,返回 框+类+置信度（esp-dl）← 模型到位填实
├── partitions.csv         # 预留 model 分区(放 .espdl)
└── sdkconfig.defaults(.esp32s3)   # PSRAM Octal 已配,本期运行时校验
```

**各组件契约:**
- `bsp`:对外暴露 `bsp_init()` / 状态灯 / 引脚宏 / 启动信息打印;依赖 IDF 驱动。隔离硬件细节。
- `net`:`net_softap_start()` / HTTP 帧推送接口;依赖 esp_wifi / esp_http_server。
- `camera`:`cam_init()` / `cam_get_frame()` / `cam_return_frame()`;依赖 esp32-camera + PSRAM。
- `ai`:`ai_init(model)` / `ai_detect(frame) -> detections{box,class,conf}`;依赖 esp-dl + PSRAM。
- `main`:只做编排与状态机,不写硬件细节。

---

## 5. 本周两条线(2026-06-13 起)

### Track A · 固件地基(Claude 驱动,在已验证的板子上,不需摄像头)

1. 换掉 hello_world → `bsp` 组件 + 薄 `main`:启动打印芯片/flash/PSRAM 信息、状态 LED 心跳、统一日志规范。
2. **PSRAM 实测**:Octal 8MB 真能 init + 能分配(启动自检 + 小块 alloc 测试)。← 相机帧缓冲与 AI 张量都住 PSRAM,命脉,必须早证。
3. **WiFi softAP + 最小 HTTP server**:手机/PC 能连上、能拿到占位端点(后续变 MJPEG 帧流用于采数据)。
4. 定下 `camera`/`ai` 组件结构 + `partitions.csv` 的 model 分区(可 build 验证);实体代码待硬件/模型到位填。

**验证:** 每步 `mcp__idf-bridge__build` 绿 + flash/monitor boot check(无 boot loop、PSRAM 打印正常、WiFi 起、心跳灯亮)。

### Track B · AI 链路提前排雷(PC 端 Python,Claude 帮写脚本趟坑)

1. 搭训练/量化环境:Python 3.11 venv + PyTorch(**CUDA 13.0**,cu130 轮子;对不上退 CPU)+ esp-ppq + ultralytics + clone esp-detection。Windows 代理/镜像/编码坑按 harness 持久记忆处理。
2. **跑通官方猫咪例程全链路**:训练 → 量化 → 导出 `.espdl` → 把官方 cat 检测例程部署到本块 S3 → 确认板上推理跑起来、打印检测结果 + 推理耗时。

**意义:** 在等货窗口内彻底干掉最大未知(本机 + 本板能否训→量化→部署→板上推理)。摄像头 + 电池数据就绪后,**只需换数据集与类名**。
**验证:** 产出 `.espdl`;cat 例程烧进 S3;monitor 见检测输出 + 推理 ms。

---

## 6. 摄像头到货后的收敛(2026-06-14 起可与本周重叠)→ MVP

```
OV5640 拍第一帧 → WiFi 图传采 300+ 张电池(5-6 类 × 多角度 × 多光照)
→ 标注(YOLO 格式) → 训练电池 ESPDet-Pico(复用 B 线已验证链路) → 量化部署
→ 板上实时识别 电池型号 + 置信度  ==  能上台的 MVP
```

**MVP 验收标准:** S3 离线、实时(目标 ≥7 FPS @224)框出电池并给出型号 + 置信度;识别率目标 ≥92%(受控集合),不达标走第 7 节保险阶梯。

---

## 7. 性能保险阶梯(效果不达标的兜底,从轻到重)

1. **加数据 + 数据增强**(精度大头通常在这)
2. **QAT 量化感知训练**(救量化掉点)
3. **输入尺寸 / 模型**:224→160,或换 ESPDet-Pico-Nano(救算力/内存)
4. **最终兜底**:蓝图的"色彩 + 形状分类器"组合,保 ≥85%

---

## 8. 技术护栏与风险(诚实)

- **PSRAM 是双重命脉**(相机帧 + AI 张量)→ Track A 第 2 步早证 Octal 真能用。
- **DVP 杜邦线接 DevKitC** 在高像素时钟下有信号完整性风险 → 线尽量短;OV2640 备胎兜 OV5640 翻车/DOA。
- **引脚已核**:蓝图相机引脚(4,5,6,7,8,9,10,11,12,13,15,16,17,18)均 ≤GPIO18,**不撞** Octal PSRAM 专用 35/36/37、不撞 USB-JTAG 19/20、不撞 SPI flash;落地接线时再用官方文档逐脚复核。
- **已知坑(延后修)**:蓝图 HMI 表用了 GPIO35/36/37,**与 Octal PSRAM 冲突**——HMI 延后,届时重排引脚。
- **量化掉精度** → 第 7 节保险阶梯。
- **版本锁**:全程 IDF 5.5.4 / target esp32s3,不升版、不换 target。
- **ESP 代码 / 寄存器 / 驱动参数**:落地时查 `espressif-documentation` 官方文档核对,不凭记忆写(项目铁律)。

---

## 9. 验证标准(按 `.claude/rules/verification.md`)

| 改动类型 | 必做验证 |
|---|---|
| 改功能代码 | `idf-bridge build` 绿 |
| 改启动路径 / app_main 早期 | build + flash + boot check(无 boot loop) |
| 改并发 / 任务 / ISR | build + flash + monitor(看 WDT / 栈溢出 / 死锁) |
| 改 sdkconfig / Kconfig | `/esp-menucheck` + build |
| 改 partitions.csv | `/esp-partition` + build |
| 改 PSRAM / flash / clock | build + boot check |

panic 必须解码回溯(addr2line / coredump_summary)定位到 函数 + file:line 再改。不把"看起来应该行"当成"验证过"。

---

## 10. 延后清单(加分层,主轴 demo 后才碰,各自单独 spec)

视觉伺服(臂)· HMI / LVGL(+ 修 GPIO35/36/37 冲突)· ESP-SR 语音 · MQTT / 上云 · Flutter APP · 自制 PCB · 3D 打印 · 双 S3 ESP-NOW

---

## 11. 开放项与默认(用户复核时可改)

- **A 线本周范围**:默认做 1-3(骨架 / PSRAM / WiFi softAP+HTTP)+ 预留 camera/ai 结构与 model 分区;第 4 步实体代码待摄像头/模型到位。
- **WiFi 模式**:默认先 **softAP**(无路由依赖,采数据 / demo 最简),STA 后补。
- **训练 venv**:未见训练用 `.venv`(仅 harness 的 `.venv-tools`),默认按全新搭建;若别处已装 PyTorch/ultralytics 可省时。
