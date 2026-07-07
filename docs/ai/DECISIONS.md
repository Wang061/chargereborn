# DECISIONS.md — 架构决策记录（ADR）

格式：日期 · 决策 · 理由 · 影响。新决策追加在最上。

---

## 2026-07-04 · 4类检测器改走 ESPDet-Pico 重训（否决 YOLOv8n 上板）
- **决策**：队友 YOLOv8n 4类模型上板否决（实测 5.8s/帧，每周期炸 5s TWDT，见 CRASH_SIGNATURES 2026-07-04）；4 类能力由 **ESPDet-Pico(354K) nc=4 重训**接棒。视觉模型线独立分支 `feat/battery-espdet4`，与 phase2-ik-autograsp（G1-G4 硬件线）解耦并行。
- **理由**：ESPDet-Pico 是 MCU 特化架构（板上已验证 219ms/帧）；多类支持后处理器原生具备（`ESPDetPostProcessor::parse_stage` 有类别循环，nc=4 固件侧零改动）；训练三处关键修复（esp_head/batch/letterbox calib）已在 `D:\WJ\jixiebi\ai\esp-detection` 在位；数据复用队友 4 类标注集副本（`D:\WJ\jixiebi\ai\vm.jpg`，394图/373标注）。
- **验收**：float mAP50 ≥ 0.85（4类均衡看，18650 类单独看——抓取主角）；板上单帧 ≤ ~300ms、4类均可出框；上板 A/B 后由用户拍板是否替换 demo 主模型。
- **影响**：新组件 `components/battery_detect4`（battery_yolo 留档禁用勿删）；`components/ai` Kconfig 三选一；partitions.csv 8MB factory 沿用（本轮模型 ~500KB，余量巨大）。落败兜底 = 单类 ESPDet demo + 报告叙述 4 类工作。

## 2026-06-03 · 确定开发板 = ESP32-S3-WROOM-1-N16R8
- **决策**：填 BOARD.md；`sdkconfig.defaults.esp32s3` 设 `FLASHSIZE_16MB` + `SPIRAM`(Octal/OPI, 80M)。
- **依据**：16MB Quad(QIO) Flash + 8MB Octal PSRAM(VDD **3.3V**)；双 Type-C 含**原生 USB-Serial-JTAG**(GPIO19/20)→ 板载 JTAG 零外接可用。
- **数据手册核实修正**(DS v1.8，见 `_research/board_datasheet.md`)：Octal PSRAM 保留脚实为 **GPIO35/36/37**(非 33-37、非卖家的 34-37；33/34 自由)；strapping 共 **4 个**：GPIO0(boot)/GPIO3(JTAG源,无内部上下拉)/GPIO45(VDD_SPI 3.3V默认)/GPIO46(boot+ROM打印)；RGB LED **GPIO48 或 38**(克隆板需实测)；flash 设 QIO；Windows 板载 JTAG 需 WinUSB 驱动。
- **影响**：实测 set-target+build 绿，`--chip esp32s3 --flash_size 16MB`、Octal PSRAM 启用。OpenOCD 可用 `esp32s3-builtin.cfg`，无需外接探针（之前"探针待定"已解决）。

## 2026-06-02 · 接入 Serena（clangd 语义级 C 代码 MCP）
- **决策**：`uv tool install --with PySocks serena@a10c3e1`，`.mcp.json` 注册（command=`C:/Users/WJ0706/.local/bin/serena.exe`，context=claude-code，project=${CLAUDE_PROJECT_DIR:-.}，关 web dashboard），`.serena/project.yml` 语言设 `cpp,python`。
- **理由**：用户已装 uv（`~/.local/bin`，缓存在 `D:\WJ\.uv`）；codegraphcontext/context7 不做 clangd 驱动的语义编辑。serena 默认按文件占比判成 python，需显式加 cpp 服务 C 固件。
- **影响**：实测 `serena project index` 成功（cpp=1, python=8），clangd 经 socks 下载（需 PySocks，见 ref-windows-esp-env-gotchas）。serena 二进制在用户级 `.local/bin`（全局），但**激活只经本项目 .mcp.json**；不要则 `uv tool uninstall serena`。依赖 `build/compile_commands.json`。

## 2026-06-02 · 启用 Core Dump→Flash + 自定义分区表 + 去 MINIMAL_BUILD
- **决策**：加 `partitions.csv`（含 coredump 分区）+ `sdkconfig.defaults`（CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH/DATA_FORMAT_ELF + 自定义分区表 + 栈检测）；移除根 CMakeLists 的 `MINIMAL_BUILD ON`。
- **理由**：MINIMAL_BUILD 裁掉了 esp_coredump 组件导致其 Kconfig 缺失、coredump 配置无法生效；真实 harness 固件需完整组件 + panic 取证能力。
- **影响**：build 较慢但代表性强；`mcp__idf-bridge__coredump_summary` 可用。实测 build 绿、target=esp32s3、coredump+自定义分区生效、产物 --chip esp32s3。注意 `rm -rf build sdkconfig` 会丢 target，需重 set-target esp32s3。

## 2026-06-02 · 固件收敛为单一根工程
- **决策**：删除重复的 `hello_world/` 子目录，以 WORKplace 根（`CMakeLists.txt` + `main/`）为唯一固件主工程。
- **理由**：两套相同骨架会让 idf.ps1 工作目录有歧义；根目录布局最扁平、与"项目根=WORKplace"一致。
- **影响**：`idf.ps1` 工作目录 = WORKplace 根；源码已在 git，可恢复。

## 2026-06-02 · S3 调试不走 probe-rs / embedded-debugger MCP
- **决策**：S3 的 C 项目调试用 OpenOCD(Espressif fork) + idf.py monitor/coredump，不用 probe-rs。
- **理由**：probe-rs RTT 只读 Rust 的 RTT 控制块，看不见 ESP-IDF 的 ESP_LOGx/printf（走串口）。
- **影响**：调试环建在 monitor+coredump(+OpenOCD JTAG) 上；已装的 embedded-debugger MCP 对 S3 视为不可用。

## 2026-06-02 · 本地自写 idf-bridge MCP（5.5.4 无官方）
- **决策**：自写 stdio `idf-bridge` 提交进 repo，封装 build/flash/size/coredump/后台 monitor。
- **理由**：官方 `idf.py mcp-server` 是 v6.0 才有，5.5.4 没有；锁版本不升 6.x。
- **影响**：构建/烧录/监视主入口 = `mcp__idf-bridge__*`。

## 2026-06-02 · harness 采用方案 B（repo-resident，全项目级隔离）
- **决策**：Superpowers 脊柱 + 把编排精华(/ralph 等)拆成 repo-resident 项目级 commands/agents，不装全局编排器插件。
- **理由**：严格不影响其他项目（规避插件激活泄漏 #11461）；可审计、可演进。
- **影响**：所有配置在 `WORKplace/.mcp.json` + `.claude/`；从 WORKplace 启动 claude。

## 2026-06-02 · 版本锁 IDF 5.5.4 / esp32s3
- **决策**：锁定 ESP-IDF 5.5.4 + target esp32s3，不自动升级。
- **理由**：用户指定 5.5.4；稳定线，避免 6.x 迁移噪声。
- **影响**：见 `.claude/rules/version-lock.md`。

## 2026-07-04 · factory 分区 4MB→8MB（partitions.csv）
- **决策**：factory app 分区扩到 0x800000；espdet_det/coredump 顺移（0x810000/0xB10000），NVS 不动。
- **理由**：队友交付 YOLOv8n 4类电池模型 espdl 3.05MB，rodata 内嵌后 app 必超 4MB；8MB 允许新旧双模型共存做板上 A/B。16MB flash 总占 ~11.1MB，余量充足。
- **风险与验证**：改分区表 = 全片重烧（含分区表+app），boot check 必做；无 OTA 在用，coredump 保留，NVS offset 未变旧数据兼容。
