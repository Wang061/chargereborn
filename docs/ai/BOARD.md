# BOARD.md — 开发板信息（连硬件前请填，session_doctor 会读摘要注入会话）

> ⚠️ 未填项请保留 `TODO`。涉及 flash/调试路径的判断以本文件为准。

| 项 | 值 |
|---|---|
| 芯片 | esp32s3 |
| 开发板型号 | TODO（如 ESP32-S3-DevKitC-1 / 自定义板） |
| Flash 大小 | TODO（如 8MB / 16MB） |
| PSRAM | TODO（无 / 2MB / 8MB；Quad / Octal） |
| USB 口类型 | TODO（**原生 USB-Serial-JTAG** / 仅 UART 桥接 CH340/CP210x） |
| **板载 USB-JTAG 是否可用** | TODO（决定能否用 `esp32s3-builtin.cfg` 零外接调试） |
| 外接 JTAG 探针 | TODO（无 / ESP-Prog / J-Link …） |
| COM 口 | TODO（连上后填，如 COM5） |
| 允许 AI 自动 flash | **NO**（默认；每次 flash 当场确认） |

## 引脚分配（按需填）
| 功能 | GPIO | 备注 |
|---|---|---|
| TODO |  |  |

## 危险 GPIO（strapping / 上电敏感）
- TODO（如 GPIO0/45/46 等 strapping 脚，改动前核对）

## 上电 / 复位顺序
- TODO
