# Rule: 版本锁

- `IDF_VERSION = 5.5.4`（安装于 `D:\WJ\Espressif\frameworks\esp-idf-v5.5.4`，IdfId `esp-idf-664c96ea6a8aaf556c400ba925468017`）。
- `IDF_TARGET = esp32s3`。
- 激活只走 `scripts/idf.ps1`（dot-source `Initialize-Idf.ps1`）。
- `FLASH_BAUD = 921600`，失败回退 `460800`；`MONITOR_BAUD = 115200`。
- `BUILD_DIR = build`；`SDKCONFIG_DEFAULTS = sdkconfig.defaults;sdkconfig.defaults.esp32s3`。

## 禁止
- 禁止自动升级 ESP-IDF 主版本（不碰 latest/master/6.x）。
- 禁止把 `IDF_TARGET` 从 esp32s3 悄悄改成别的（`set-target` 必须确认）。
- 禁止引入并行构建系统（PlatformIO 等）。
- 升级/换版需先在 `docs/ai/DECISIONS.md` 记录决策并经用户确认。
