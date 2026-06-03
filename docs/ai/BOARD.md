# BOARD.md — 开发板信息（session_doctor 会读摘要注入会话）

| 项 | 值 |
|---|---|
| 芯片 | **esp32s3**（双核 LX7 @ 240 MHz，内置 AI 指令加速） |
| 模组 | **ESP32-S3-WROOM-1-N16R8** |
| 开发板 | ESP32-S3-N16R8 开发板（乐鑫原装模组，DevKitC-1 类布局，排针向下焊好） |
| Flash | **16 MB**（SPI Flash, N16） |
| PSRAM | **8 MB，Octal / OPI**（R8）→ menuconfig 启 `SPIRAM` + `SPIRAM_MODE_OCT`（已在 sdkconfig.defaults.esp32s3 配好） |
| USB 口 | **双 Type-C**：① 原生 USB-Serial-JTAG（接 GPIO19/20）② UART 桥接。两口都可供电+通信 |
| **板载 USB-JTAG** | **可用 ✓**（原生 USB 口）→ OpenOCD `board/esp32s3-builtin.cfg` **零外接**断点调试 |
| 外接 JTAG 探针 | 不需要（用板载） |
| WiFi / 蓝牙 | 2.4G 802.11 b/g/n（ADC2 在 WiFi 开时不可用）/ BLE 5.0 |
| COM 口 | TODO（连上后填；可能出现两个：桥接口 + 原生 CDC 口，注意区分） |
| 允许 AI 自动 flash | **NO**（每次 flash 当场确认） |

## 危险 / 特殊 GPIO（改动前必核对）
- **GPIO33–37**：N16R8 的 **Octal Flash/PSRAM 专用**，**禁止用作普通 IO**（卖家标注 34-37；以 ESP32-S3 TRM 为准，用 espressif-documentation 可核）。
- **Strapping 脚**：**GPIO0**（拉低进下载模式）、**GPIO45**（VDD_SPI 电压选择）、**GPIO46**（boot 模式）——上电默认电平敏感，慎接外设。
- **原生 USB**：**GPIO19 (D-) / GPIO20 (D+)**——用 USB-Serial-JTAG/USB-OTG 时这两脚不可挪用。（注：卖家详情写"GPIO47/48 为原生USB"系**笔误**，S3 原生 USB 是 19/20。）
- **板载 RGB LED**：**GPIO48**（WS2812，丝印 GPIO48；可做最小 blink 验证目标）。
- **ADC**：优先用 **ADC1**（约 GPIO1–GPIO10）；ADC2 在 WiFi 开启时不可用。

## 上电 / 复位
- 板载按键：**RST**（复位）、**BOOT**（GPIO0，按住上电进下载模式）。
- 双 Type-C 供电：任一口可供电；调试期建议用原生 USB 口（兼供电+JTAG+CDC）。

## 机械臂相关（待补，见 SAFETY.md）
- 舵机 PWM 用 LEDC/MCPWM 输出；具体引脚分配填下表。

| 功能 | GPIO | 备注 |
|---|---|---|
| RGB LED（板载） | 48 | WS2812，blink/状态指示 |
| 舵机×6 PWM | TODO | 避开 33-37 / strapping / USB(19,20) |
| 串口/通信 | TODO |  |
