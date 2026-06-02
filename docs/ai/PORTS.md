# PORTS.md — 串口 / JTAG 端口记录

> 连上硬件后填，flash/monitor/openocd 默认参考这里。

## 串口（UART / USB-CDC）
| 设备 | COM 口 | 波特率 | 备注 |
|---|---|---|---|
| ESP32-S3 主串口 | TODO | 115200(monitor) | flash 用 921600，失败回退 460800 |

## JTAG
| 探针 | 接口 cfg | 备注 |
|---|---|---|
| 板载 USB-JTAG | board/esp32s3-builtin.cfg | 仅当 BOARD.md 确认原生 USB-JTAG 引出 |
| 外接（如 ESP-Prog/J-Link） | TODO | 需对应 interface cfg |

## 历史/备注
- TODO（端口偶发变化、驱动问题、占用冲突等记录于此）
