All facts are already verified and provided. I'll synthesize them directly into a concise Markdown fact-sheet.

# ESP32-S3-WROOM-1-N16R8 — BOARD.md 事实速查

> 核心：ESP32-S3 双核 LX7 @240MHz · 16MB Quad SPI flash (N16) · 8MB **Octal/OPI** PSRAM (R8) · 41 引脚 / 36 可用 GPIO · 工作电压 3.0–3.6V · 温区 -40~65°C
> 主要来源：Espressif WROOM-1 数据手册 v1.8 + ESP-IDF stable docs（2026-06，6.0.x；与 5.5 一致）

---

## 1. 确认 / 修正（含与卖家详情的出入）

| 事实 | 正确值 | 卖家说法 | 置信度 | 来源 |
|---|---|---|---|---|
| **Octal PSRAM 保留脚** | **GPIO35 / 36 / 37**（IO35=SPIIO6, IO36=SPIIO7, IO37=SPIDQS） | ❌ "GPIO34–37" 错误 | high | DS v1.8 |
| **GPIO34 是否保留** | **否**，GPIO34=IO41/MTDI 可正常用 | 被误划入保留 | high | DS v1.8 |
| **PSRAM 模式** | **Octal (OPI)** = 正确，R8 为八线 PSRAM | ✅ "OPI" 正确 | high | IDF flash_psram_config |
| **PSRAM/VDD_SPI 电压** | **3.3V**（不是 1.8V！1.8V 仅 N16R**16V**） | — | high | DS v1.8 脚注7 |
| **原生 USB 引脚** | **GPIO19 = D- / GPIO20 = D+** | ❌ "GPIO47/48 为原生USB" 错误 | high | DS v1.8 Table 3-1 |
| **GPIO47/48 角色** | 普通 GPIO（N16R8 上为 3.3V，**非** USB，**非** 1.8V） | 误标 | high | DS v1.8 脚注c |
| Flash 类型/模式 | 16MB **Quad** SPI，QIO/DIO（**非** octal），3.3V，默认 80MHz | — | high | DS v1.8 + IDF |
| 开发板 USB 口 | 官方 DevKitC-1 为 **2× Micro-USB**；双 Type-C 板为第三方克隆 | — | high | esp-dev-kits UG v1.1 |

**关键纠正三连**：① 保留脚是 **35/36/37**（不是 34-37）；② N16R8 的 PSRAM/VDD_SPI 是 **3.3V**（"R8=1.8V" 是常见误区）；③ 原生 USB 在 **GPIO19/20**（卖家说的 47/48 是错的）。

---

## 2. 危险 / 保留 GPIO 最终清单（抄进 BOARD.md）

| GPIO | 用途 | 能否用作应用 IO |
|---|---|---|
| **GPIO35 / 36 / 37** | Octal PSRAM 数据/选通（SPIIO6/IO7/DQS） | ❌ **绝对禁用**（R8 占用） |
| **GPIO0** | Strapping：boot 模式（默认弱上拉，=1 SPI Boot） | ⚠️ 复位后可用，注意上电电平 |
| **GPIO3** | Strapping：JTAG 源选择（**无内部上下拉，需外部驱动**） | ⚠️ 谨慎 |
| **GPIO45** | Strapping：VDD_SPI 电压（默认弱下拉=0 → 3.3V；=1→1.8V） | ⚠️ 上电勿强拉高 |
| **GPIO46** | Strapping：boot 模式 + ROM 打印（默认弱下拉=0） | ⚠️ 双重 strapping 角色 |
| **GPIO19 / 20** | 原生 USB D- / D+（USB-Serial-JTAG / USB-OTG） | ⚠️ 用 USB 则占用 |
| **GPIO48**（或 v1.1 的 **GPIO38**） | 板载 WS2812 寻址 RGB LED（RMT 驱动） | 视板而定，**需实测** |

- **Boot**：SPI Boot(默认) = GPIO0=1；下载模式 = GPIO0=0 & GPIO46=0（支持 USB / UART 下载）。
- **Strapping latch**：复位时锁存，t_H ≥ 3ms 后释放为普通 IO。
- **RGB LED**：先试 **GPIO48**，不亮再试 **GPIO38**（v1.0→v1.1 改动）。另有独立的纯电源指示 LED（接 USB 即亮）。
- 注意：**GPIO33/34 不保留**（仅 octal **flash** 才会占用 SPIIO4/5，本模块是 quad flash）。

---

## 3. N16R8 sdkconfig 要点

```
# --- Flash：16MB Quad ---
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y          # Flash size -> 16 MB
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y           # QIO 优先（或 DIO）；Quad 只支持 STR
# CONFIG_ESPTOOLPY_OCT_FLASH 必须保持「取消勾选」(quad flash 勿开，否则报 EFUSE not configured)
#   默认 flash 80MHz；120MHz/auto-suspend 需联系 Espressif，竞赛勿用

# --- PSRAM：8MB Octal/OPI ---
CONFIG_SPIRAM=y                            # Component config -> Hardware Settings
CONFIG_SPIRAM_MODE_OCT=y                   # R8 = 八线 OPI（Octal 只支持 DTR）
CONFIG_SPIRAM_SPEED_80M=y                  # 80MHz 性能最佳（保守用 _40M）
```

**连带约束 / 注意**：
- **共享时钟**：flash 与 PSRAM 同一内部时钟，速度必须成组。F4R8（=quad flash + octal PSRAM）合法组合：flash 80MHz SDR / 40MHz ↔ PSRAM 80MHz DDR / 40MHz DDR。**不能任意混搭**。
- **120MHz 组合**为实验性（需 `CONFIG_IDF_EXPERIMENTAL_FEATURES`），有温度相关崩溃风险 → 竞赛可靠性优先，**禁用**。
- **GPIO35/36/37 内部被 PSRAM 占用**，应用层勿引出。
- "octal PSRAM 需特定 flash 配置" 的坑实为 **octal flash** 专属，**不适用** N16R8 → 不要烧 `FLASH_TYPE` eFuse（不可逆）。

**调试（内置 JTAG，无需外部探针）**：
- 仅需 USB 线接原生口（GPIO19/20）即可 JTAG 调试。
- 启动：`openocd -f board/esp32s3-builtin.cfg`（IDF 自带）。
- **Windows 注意**：内置 USB-JTAG 可能需装 WinUSB 驱动，否则 OpenOCD 报 `LIBUSB_ERROR_NOT_FOUND`；用 Espressif Installation Manager 的「Install Drivers」或 `eim install-drivers`（旧版用 Zadig 绑 WinUSB）。
- 烧录/串口默认用 **USB-to-UART 桥口**（出现为 COM 口，≤3Mbps）；原生 USB 口也可烧但 get-started 软件支持不完整。

---

## 4. 置信度与缺口

- **全部核心事实 confidence=high**，主源为官方数据手册 v1.8 与 ESP-IDF stable docs，无来源冲突。
- **需实测确认（无法仅凭文档定论）**：
  1. **RGB LED 引脚**——克隆板可能为 GPIO48 或 GPIO38，必须上板试。
  2. **USB 口物理形态与丝印**——本项目板为双 Type-C（非官方双 Micro-USB），哪个口是 UART 桥 / 哪个是原生 USB 须看丝印或实测。
  3. 板载是否真有 USB-UART 桥芯片及型号（克隆板可能用 CH343/CP2102 等）——文档基于官方板，克隆板需核对。
- **IDF 版本**：CONFIG 符号与行为在 5.5 与 6.0.x 一致，可直接用于 5.5 的 sdkconfig。

**唯一来源 URL**：
- 数据手册：https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
- Flash/PSRAM 配置：https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/flash_psram_config.html
- 开发板用户指南：https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html
- 内置 JTAG：https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/configure-builtin-jtag.html