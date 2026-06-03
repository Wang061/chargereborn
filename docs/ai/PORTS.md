# PORTS.md — 串口 / JTAG 端口记录

> 连上硬件后填，flash/monitor/openocd 默认参考这里。

## 串口（UART / USB-CDC）
| 设备 | COM 口 | 波特率 | 备注 |
|---|---|---|---|
| ESP32-S3 主串口 | TODO | 115200(monitor) | flash 用 921600，失败回退 460800 |

## JTAG
| 探针 | 接口 cfg | 备注 |
|---|---|---|
| **板载 USB-JTAG（本板可用 ✓）** | `board/esp32s3-builtin.cfg` | ESP32-S3-N16R8 双 Type-C 含原生 USB-Serial-JTAG（GPIO19/20），**零外接探针**即可 `idf.py openocd`/`gdb` |
| 外接（ESP-Prog/J-Link） | —— | 不需要（用板载） |

调试用原生 USB 口（兼供电+JTAG+CDC）；接 GPIO19/20。

## 历史/备注
- TODO（端口偶发变化、驱动问题、占用冲突等记录于此）
