# ChargeReborn — 边缘智能锂电池识别与定位（ESP32-S3）

> 面向退役锂电池安全回收的边缘智能预处理系统。把电池识别与定位这一核心环节的 AI 推理**完全收敛到乐鑫 ESP32-S3 芯片上离线运行**，无需联网与云端算力。

- **芯片**：ESP32-S3（8MB PSRAM）+ 板载摄像头
- **框架**：ESP-IDF 5.5.4（`IDF_TARGET = esp32s3`）
- **构建目标**：`chargereborn_brain`
- **状态**：边缘 AI 主轴（识别 → 定位 → 执行接口）已在硬件上实测打通

---

## 项目背景

当前废旧锂电池拆解环节高度依赖人工目检，型号识别效率低，且存在短路与热失控隐患。ChargeReborn 将电池识别、姿态估计与抓取目标解算全部落在乐鑫芯片上离线完成，为退役电池的型号分选、安全拆解与梯次利用提供前端识别支撑。

整套系统围绕 **感知 → 决策 → 执行** 构成闭环：芯片端实时识别电池 → 估计位置与朝向 → 输出标准化机械臂抓取目标。

## 已完成能力

- **芯片端实时识别**：基于乐鑫 ESP-DL，在 S3 上离线运行自训练的 ESPDet-Pico 检测模型，实时框出电池型号与置信度，支持多颗电池同时分框。
- **自建数据 + 自训模型**：团队自采多角度电池数据集并标注，从零训练检测模型（浮点 mAP50 达 0.96），经 int8 + letterbox 量化部署到芯片端实时推理。
- **WiFi 图传与数据采集**：S3 以 SoftAP 自建热点，提供网页端实时图传与连拍采集页，既是演示界面也是采集训练数据的工具。
- **可视化识别**：网页端通过 `/detect` 接口实时叠加检测框、类别与置信度。
- **姿态估计**：用结构张量估计每颗电池长轴角度（对包皮颜色与光照鲁棒），为机械臂抓取提供朝向信息。
- **执行接口**：自研 `armlink` 模块从识别结果选取最佳目标，解算抓取目标（像素中心 + 角度），经 `/arm_target` 接口与舵机串口协议输出。**默认关闭、不驱动实物，保障调试安全。**
- **工程可复现**：依赖锁版（`dependencies.lock`）、模型随仓入库，克隆即可编译复现。

## 目录结构

```
├── main/                     应用入口（app_main：相机→网络→AI→检测任务）
├── components/
│   ├── bsp/                  板级支持（系统信息、PSRAM 自检）
│   ├── camera/              摄像头初始化与取帧
│   ├── net/                 SoftAP + HTTP 服务 + MJPEG 图传 + /detect
│   ├── ai/                  ESP-DL 推理封装 + 电池长轴角度估计
│   ├── armlink/             抓取目标解算 + 舵机串口协议（UART 默认关）
│   └── battery_detect4/     自训 ESPDet-Pico 4类电池检测器 + 随仓入库的 .espdl 模型
├── docs/                     设计规格、决策记录、模型流水线、排障笔记
├── scripts/                 IDF 激活入口、串口抓取、拍照等工具
├── reference/               旧 OpenMV/Arduino 参考实现与样例图片（仅供参考，不参与构建）
├── partitions.csv           分区表
├── sdkconfig.defaults*      构建配置源（含 esp32s3 专属项）
└── dependencies.lock        组件版本锁（保可复现）
```

## 快速开始

前置：安装 ESP-IDF **5.5.4**，`IDF_TARGET = esp32s3`。

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

> 本仓库配套 Windows PowerShell 的 IDF 激活入口 `scripts/idf.ps1`，以及项目级开发 harness（见 `README.system.md`）。

烧录波特率 `921600`（失败回退 `460800`），监视波特率 `115200`。

## 使用（Web 界面）

设备启动后以 **SoftAP** 自建热点。连接后在浏览器访问设备网关地址：

| 接口 | 说明 |
| --- | --- |
| `/` | 实时图传 + 采集页 |
| `/detect` | 实时叠加检测框 / 类别 / 置信度 / 角度标线 |
| `/arm_target` | 输出当前最佳抓取目标（像素中心 + 角度） |

## 文档

- 作品简介：[`docs/作品简介.md`](docs/作品简介.md)
- 模型训练→量化→部署复现：[`docs/ai/MODEL_PIPELINE.md`](docs/ai/MODEL_PIPELINE.md)
- 数据集采集指南：[`docs/ai/DATASET_GUIDE.md`](docs/ai/DATASET_GUIDE.md)
- 板级信息 / 端口 / 首次运行清单：[`docs/ai/`](docs/ai/)
- 设计规格与实现计划：[`docs/superpowers/`](docs/superpowers/)

## 安全说明

`armlink` 的舵机 UART 输出**默认关闭**，识别与定位链路不会驱动真实机械臂，便于安全调试。启用真实执行前请阅读 [`docs/ai/SAFETY.md`](docs/ai/SAFETY.md)。
