---
name: esp32s3-bringup
description: 新板/新固件从零点亮的分层 bring-up 流程（电源→串口→blink/log→外设→任务→网络/存储）。首次跑通一块 S3 板或大改后用。
---
# ESP32-S3 Bring-up（逐层验证，别跳步）

先读 `docs/ai/BOARD.md`、`docs/ai/SAFETY.md`。每层验证通过再进下一层。

1. **电源/识别**：上电不冒烟；USB/串口被识别（COM 口出现，记入 PORTS.md）。确认是原生 USB-JTAG 口还是 UART 桥。
2. **最小固件**：hello_world / blink + `ESP_LOGI` 启动横幅，`/esp-build`→`/esp-flash`→`/esp-monitor` 看到稳定打印、无 boot loop、`CONFIG_IDF_TARGET=esp32s3`。
3. **时钟/flash/PSRAM**：menuconfig 确认 flash 大小/模式、PSRAM 类型与实际一致（不一致会崩或缩水）。
4. **外设逐项**：一次只加一个（GPIO→LEDC/MCPWM 舵机→I2C/SPI 传感器→UART），每加一个 build+flash+monitor 验证。舵机先抬空/限速（SAFETY.md）。
5. **FreeRTOS 任务**：逐个加任务，声明栈/优先级理由；观察 WDT/栈水位。
6. **网络/存储最后**：WiFi/BLE、NVS、文件系统最后接入（耗电/共存/分区影响大）。

每层失败→定位到 函数+file:line 再改。卡住的签名查/写 `docs/ai/CRASH_SIGNATURES.md`。
