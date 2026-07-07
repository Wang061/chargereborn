# PORTS.md — 串口 / JTAG 端口记录

> 连上硬件后填，flash/monitor/openocd 默认参考这里。

## 串口（UART / USB-CDC）
| 设备 | COM 口 | 波特率 | 备注 |
|---|---|---|---|
| USB-to-UART 桥（CH340，烧录/monitor 首选） | **COM12** | 115200(monitor) | 2026-07-03 实测：flash 460800 成功。同一 CH340 枚举出 COM11/COM12 两个口，**能用的是 COM12** |
| CH340 第二枚举口 | COM11 | — | 与 COM12 同芯片，未验证 |
| 原生 USB-Serial-JTAG（VID 303A:1001） | COM7 | — | 烧录时报"端口忙/拒绝访问"（可能被 monitor 或其他程序占用），暂不用于 flash |

> 旧记录 COM6 已失效（2026-07-03 起端口重新枚举为上表）。

## JTAG
| 探针 | 接口 cfg | 备注 |
|---|---|---|
| **板载 USB-JTAG（本板可用 ✓）** | `board/esp32s3-builtin.cfg` | ESP32-S3-N16R8 双 Type-C 含原生 USB-Serial-JTAG（GPIO19/20），**零外接探针**即可 `idf.py openocd`/`gdb` |
| 外接（ESP-Prog/J-Link） | —— | 不需要（用板载） |

调试用原生 USB 口（兼供电+JTAG+CDC）；接 GPIO19/20。

> ⚠️ **Windows 板载 JTAG 驱动**：首次用 OpenOCD 若报 `LIBUSB_ERROR_NOT_FOUND`，需给内置 USB-JTAG 装 **WinUSB** 驱动 —— 用 Espressif Installation Manager「Install Drivers」/ `eim install-drivers`，或 Zadig 把该设备绑 WinUSB。
> 烧录/串口默认走 **USB-to-UART 桥口**（即 COM6，≤3Mbps）；原生 USB 口也能烧但官方软件支持不完整。本板为双 Type-C 克隆，桥芯片型号（CH343/CP2102 等）以实测为准。

## 历史/备注
- TODO（端口偶发变化、驱动问题、占用冲突等记录于此）
