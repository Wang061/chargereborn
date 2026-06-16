# BOARD.md — 开发板信息（session_doctor 会读摘要注入会话）

| 项 | 值 |
|---|---|
| 芯片 | **esp32s3**（双核 LX7 @ 240 MHz，内置 AI 指令加速） |
| 模组 | **ESP32-S3-WROOM-1-N16R8** |
| 开发板 | ESP32-S3-N16R8 开发板（乐鑫原装模组，DevKitC-1 类布局，排针向下焊好） |
| Flash | **16 MB**（SPI Flash, N16） |
| PSRAM | **8 MB，Octal / OPI**（R8，**VDD 3.3V**，非 1.8V）→ `SPIRAM` + `SPIRAM_MODE_OCT`（已配好）。flash 与 PSRAM **共时钟、速度成组**：本配 80M(flash SDR)+80M(PSRAM DDR) 为合法组合 |
| USB 口 | **双 Type-C**：① 原生 USB-Serial-JTAG（接 GPIO19/20）② UART 桥接。两口都可供电+通信 |
| **板载 USB-JTAG** | **可用 ✓**（原生 USB 口）→ OpenOCD `board/esp32s3-builtin.cfg` **零外接**断点调试 |
| 外接 JTAG 探针 | 不需要（用板载） |
| WiFi / 蓝牙 | 2.4G 802.11 b/g/n（ADC2 在 WiFi 开时不可用）/ BLE 5.0 |
| COM 口 | **COM7 = 原生 USB-Serial-JTAG（flash 用）；COM11 = CH340 UART 桥 = UART0 主 console（monitor/日志用）**（2026-06-13 实测；原 COM6 已过时，端口号可能随插拔变，认友好名而非号） |
| 允许 AI 自动 flash | **NO**（每次 flash 当场确认） |

## 危险 / 特殊 GPIO（官方 DS v1.8 核实，改动前必核对）
- **GPIO35 / 36 / 37**：**Octal PSRAM 专用**（SPIIO6/IO7/DQS），**禁止用作普通 IO**。
  ⚠️ **GPIO33 / 34 不保留、可正常用**（那两脚仅 octal *flash* 才占用，本模块是 quad flash）。卖家标"34-37"、我先前标"33-37"**均有误**，正确仅 **35/36/37**。
- **Strapping 脚（4 个）**：
  - **GPIO0**：boot（默认弱上拉=SPI Boot；=0 且 GPIO46=0 进下载模式）。
  - **GPIO3**：JTAG 源选择，**无内部上下拉、需外部确定电平**，慎用。
  - **GPIO45**：VDD_SPI 电压（默认弱下拉=**3.3V**；上电被拉高=1.8V）→ **上电时勿强拉高**。
  - **GPIO46**：boot 模式 + ROM 打印（默认弱下拉）。
  - 复位时锁存（t_H≥3ms 后释放为普通 IO），但**上电电平敏感**，接外设注意默认态。
- **原生 USB**：**GPIO19 (D-) / GPIO20 (D+)**——用 USB-Serial-JTAG/USB-OTG 时这两脚不可挪用。（卖家写"GPIO47/48 为原生USB"系**错误**；47/48 是普通 GPIO。）
- **板载 RGB LED**：**GPIO48 或 GPIO38**（WS2812；克隆板 v1.0→v1.1 有改动，**先试 48 不亮再试 38**）。另有纯电源指示 LED（接 USB 即亮）。
- **ADC**：优先用 **ADC1**（约 GPIO1–GPIO10）；ADC2 在 WiFi 开启时不可用。

## 上电 / 复位
- 板载按键：**RST**（复位）、**BOOT**（GPIO0，按住上电进下载模式）。
- 双 Type-C 供电：任一口可供电；调试期建议用原生 USB 口（兼供电+JTAG+CDC）。

## 机械臂相关（待补，见 SAFETY.md）
- 舵机 PWM 用 LEDC/MCPWM 输出；具体引脚分配填下表。

| 功能 | GPIO | 备注 |
|---|---|---|
| RGB LED（板载） | 48 或 38 | WS2812，先试 48；blink/状态指示 |
| 舵机×6 PWM | TODO | 避开 35/36/37(PSRAM)、strapping(0/3/45/46)、USB(19/20) |
| 串口/通信 | TODO |  |

## 相机接线表（正点原子 ATK-OV2640 排针 → DevKitC，2026-06-16 实测点亮 OK）
> 实测日志：`Detected OV2640 camera` / `PID=0x0026` / SCCB `pin_sda 4 pin_scl 5`。
> **要点：模块板载 24MHz 有源晶振自时钟，排针无 XCLK 脚 → `pin_xclk=-1`**（驱动 esp_camera.c:180 守卫支持外部时钟）。
> PWDN/RST 接 GPIO 由驱动做上电+复位脉冲（检测最稳）。FLASH(补光灯) 不接。真相以 `components/camera/include/camera.h` 的 `CAM_PIN_*` 为准。

| 模块脚 | GPIO | 模块脚 | GPIO |
|---|---|---|---|
| SCL(SIO_C) | 5 | VSYNC | 6 |
| SDA(SIO_D) | 4 | HREF | 7 |
| PCLK | 13 | PWDN | 15 |
| D0 | 8 | RST | 21 |
| D1 | 9 | D5 | 18 |
| D2 | 10 | D6 | 17 |
| D3 | 11 | D7 | 16 |
| D4 | 12 | XCLK | 不接(自时钟) |
| VCC | 3V3 | GND | GND |

- OV5640（正点原子 ATK-MC5640 排针 / FD5640 FPC，备选）：有 MCLK 脚需外部 XCLK，固件 pin 配置另起一套；上 OV5640 时再核对 `datasheets/OV5640-*`。
