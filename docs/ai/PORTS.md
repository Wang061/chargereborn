# PORTS.md — 串口 / JTAG 端口记录

> 连上硬件后填，flash/monitor/openocd 默认参考这里。

## 串口（UART / USB-CDC）

> ⚠️ **Brain 和 KM1 的 USB 桥芯片都是 CH340，Windows 友好名完全相同**（"USB-SERIAL CH340"），
> **不能凭名字/号码区分，只能实测**。最快方法：`scripts/km1_send.py --port COMx --probe`
> 发无动作探针 `$KMS:0,0,0,0!`——**原样回显该字符串** = KM1（真机 `$KMS:` 只回显不派发，绝不动舵机，
> 安全可反复测）；**回显的是 `I (...) main: detect: ...` 这类 ESP_LOG 行** = Brain 控制台。
> 端口号随每次插拔/插入顺序变化，下表数字**不可当真，只能当"最近一次实测"参考**，用前重新探针确认。

| 设备 | COM 口（最近实测） | 波特率 | 备注 |
|---|---|---|---|
| Brain：USB-to-UART 桥（CH340，monitor/日志） | 2026-07-03 实测 COM12 → **2026-07-06 探针实测变成 COM11**（回显实时 detect 日志） | 115200(monitor) | 端口已随插拔漂移，flash 仍走原生 USB 口更稳 |
| Brain：原生 USB-Serial-JTAG（VID 303A:1001） | COM7 | — | 烧录用；曾报"端口忙"可能被 monitor 占用 |
| **KM1（Steward）：USB-to-UART 桥（CH340）** | 2026-06-30 记录 COM3 → 2026-07-02/03 记录 COM4 → **2026-07-06 探针实测变成 COM12**（回显 `$KMS:0,0,0,0!`） | 115200 | G1 全部"COM4 直连 KM1"操作实际口以当次探针结果为准，`ARM_PIPELINE.md` 里写的"COM4"是历史值不是保证 |

> 旧记录 COM6 已失效（2026-07-03 起端口重新枚举）；COM3/COM4/COM12 均出现过对应 KM1，见上表演变。

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
