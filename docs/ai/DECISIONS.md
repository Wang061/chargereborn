# DECISIONS.md — 架构决策记录（ADR）

格式：日期 · 决策 · 理由 · 影响。新决策追加在最上。

---

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
