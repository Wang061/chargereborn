# S3-Forge — 项目主规则（Claude Code 每会话读取）

本工程是 **ESP-IDF 5.5.4 + ESP32-S3** 固件，配套 S3-Forge 项目级开发 harness。
设计文档：`docs/superpowers/specs/2026-06-02-s3-forge-design.md`。详细规则见 `.claude/rules/`。

## 版本锁（写死，不得违反）
- `IDF_VERSION = 5.5.4`、`IDF_TARGET = esp32s3`
- 构建经 `scripts/idf.ps1`（唯一 IDF 激活入口）或 `mcp__idf-bridge__*`
- `FLASH_BAUD = 921600`（失败回退 460800）、`MONITOR_BAUD = 115200`
- `SDKCONFIG_DEFAULTS = sdkconfig.defaults;sdkconfig.defaults.esp32s3`
- **禁止**自动升级 IDF 主版本、禁止把 target 从 esp32s3 改成别的。

## 入口与工具优先级
1. **构建/烧录/监视**：优先 `mcp__idf-bridge__*`（build/size/flash/monitor_start/coredump_summary）或 `/esp-*` 命令；不要直接裸跑 idf.py。
2. **查 ESP 文档/寄存器/API**：用 `espressif-documentation` MCP（`search_espressif_sources`），不要凭记忆写 ESP 代码。
3. **理解固件树**：用 codegraphcontext（已全局）/ 原生 Read。
4. 旧 OpenMV/ESP32 参考码在 `../4.源代码程序/`（**只读参考**，不改）。

## 闭环工作流
`/esp-build` →(失败)`esp-build-fix` 改最小文件→回 build（`/esp-loop` 自动迭代到绿）
→(绿)`/esp-flash`(确认)→`/esp-monitor`(超时落盘)→Read 解码回溯
→(panic)`/esp-panic`→`coredump_summary`→`esp-monitor-triage`
→`/esp-snap` 拍板子→Read 图像判物理现象→(不对)回改
→(需断点)OpenOCD+gdb_batch（S3 板载 USB-JTAG，前提见 BOARD.md）
→(通过)`/release-check` 或 `/learn` 沉淀经验

## 铁律（防翻车）
- **先 build，不要 flash**；`flash`/`set-target`/`erase`/`fullclean` 必须当场确认。
- 改 `sdkconfig`/`partitions.csv`/boot/OTA/NVS/clock/power/flash/PSRAM → 显式说明风险 + build(+boot) 校验。
- 默认只改与任务直接相关的文件；新增模块必须组件化（不塞 main）；驱动与业务分层。
- ISR 禁阻塞/重活；任务创建必须声明栈与优先级理由；统一日志 TAG/等级；禁 magic number。
- 危险命令由 `.claude/hooks/guard.py` 拦截（erase/rm -rf/读密钥=deny；flash/set-target=ask）。

## 环境坑（已实测，见持久记忆 ref-windows-esp-env-gotchas）
- `.ps1` 必须 **ASCII-only**（PS5.1 把无 BOM 的 UTF-8 当 GBK）。
- 从 git-bash spawn powershell 跑 idf 会泄漏 MSYS 环境 → 用 PowerShell 工具或 idf-bridge 清洗 MSYSTEM。
- python 内打印中文到 stdout 会撞 GBK → 用 `ensure_ascii=True` 或 `PYTHONIOENCODING=utf-8`。
- pip 在新 venv 受 socks 代理影响 → 清代理 + 清华镜像。

## 经验沉淀
每修好一个 bug，用 `/learn` 把崩溃签名/根因/修法写入 `docs/ai/CRASH_SIGNATURES.md`。
