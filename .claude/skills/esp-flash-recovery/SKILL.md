---
name: esp-flash-recovery
description: 烧录/下载失败恢复——端口、握手、波特率、stub、进下载模式。flash 失败时用。
---
# 烧录失败恢复

1. **端口**：列 COM 口核对 PORTS.md；关闭占用串口的程序（monitor/IDE 串口窗）。
2. **进下载模式**：按住 BOOT(GPIO0) 再按一下 EN/RST 松开 → 进 ROM 下载；确认用的是数据线不是充电线。
3. **波特率**：默认 921600，失败回退 460800（version-lock）。
4. **`Failed to connect: No serial data received`**：多为没进下载模式 / 线 / 端口选错。
5. **stub 出错**：必要时禁 stub 重试。
6. **S3 USB 口区分**：原生 USB-Serial-JTAG（CDC）端口 ≠ UART 桥（CH340/CP210x）端口；BOARD.md 写清用哪个。
7. **禁止**：不用 `erase_flash`（被 deny）；确需整擦由用户手动。

烧录经 `mcp__idf-bridge__flash`（ask）。复杂时序查 espressif-documentation。
