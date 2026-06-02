# S3-Forge — 系统总说明

Claude Code 驱动的 **ESP-IDF 5.5.4 + ESP32-S3** 项目级开发底座。
完整设计见 `docs/superpowers/specs/2026-06-02-s3-forge-design.md`；决策见 `docs/ai/DECISIONS.md`。

## 怎么用（重要）
1. **从本目录（`WORKplace`）启动 Claude Code**——所有 harness 配置（`.mcp.json` + `.claude/`）按此目录为项目根生效，**不影响你其它项目**。
2. 首次会话：`espressif-documentation` MCP 会要求一次浏览器 **OAuth**（GitHub/微信）。
3. 连硬件前先填 `docs/ai/BOARD.md`、`docs/ai/SAFETY.md`。
4. 体检：在干净 PowerShell 跑 `powershell -ExecutionPolicy Bypass -File scripts/ai-doctor.ps1`。

## 闭环工作流
```
/esp-build →(失败) esp-build-fix 改最小文件 →回build   [/esp-loop 自动迭代到绿]
 →(绿) /esp-flash(确认) → /esp-monitor(后台落盘) → Read 解码回溯
 →(panic) /esp-panic → coredump_summary → esp-monitor-triage
 → /esp-snap 拍板子 → Read 图像判物理现象 →(不对)回改
 →(需断点) OpenOCD+gdb（S3 板载 USB-JTAG，前提见 BOARD.md，进阶）
 →(通过) /release-check 或 /learn 沉淀经验
```

## 能力清单
- **MCP**：`espressif-documentation`(官方文档检索) · `idf-bridge`(自写：build/size/set_target/flash/coredump/非阻塞 monitor) · `serena`(clangd 语义级找/改符号，已索引 C 固件) · 复用全局 `context7`。
- **命令(14)**：`/esp-build /esp-flash /esp-monitor /esp-snap /esp-fix /esp-panic /esp-menucheck /esp-partition /freertos-review /driver-review /power-review /release-check /esp-loop /learn`。
- **Agents(8)**：esp-planner / build-fix / flash-debugger / monitor-triage / freertos-reviewer / driver-architect / low-power-tuner / release-guardian。
- **Skills(10)**：esp32s3-bringup / esp-idf-build-fix / esp-flash-recovery / esp-monitor-triage / esp-panic-backtrace / freertos-task-design / heap-memory-triage / esp-partition-ota / sdkconfig-change-review / embedded-code-review。
- **Hooks(4)**：session_doctor(SessionStart) · guard(PreToolUse 安全闸门) · post_build_log(PostToolUse 归档分类) · ralph_stop(Stop 迭代环)。
- **规则**：`.claude/CLAUDE.md` + `.claude/rules/`（版本锁/编码/安全/验证）。
- **脚本**：`scripts/idf.ps1`(IDF激活) · `idf_bridge_mcp.py` · `serial_capture.py` · `snap.py` · `ai-doctor.ps1`。

## 安全模型
- `permissions`：deny 密钥/erase；ask flash/set-target/git push/pip install。**allow 不含裸 Bash**（否则 guard 失效）。
- `guard.py`(PreToolUse, Bash+PowerShell)：`erase_flash`/`rm -rf`/读密钥→deny；`flash`/`set-target`/`fullclean`/`openocd`→ask。
- `ralph_stop`：`decision:block`+`stop_hook_active`+max_iterations 三重防跑飞。
- 硬件：默认不自动 flash；舵机/执行器遵守 SAFETY.md 限位与电流预算。

## 版本锁
IDF=5.5.4 / target=esp32s3 / 构建经 idf.ps1 或 idf-bridge / FLASH_BAUD=921600(回退460800) / MONITOR_BAUD=115200。固件已启 Core Dump→Flash + 自定义分区表(`partitions.csv`)。

## 环境坑（已实测，写脚本/hook 必看）
1. anaconda python 原生 → 路径用 `C:/`/`D:/` 不用 `/c/`。
2. socks 代理破新 venv 的 pip → 清代理 + 清华镜像。
3. PS 5.1 把无 BOM 的 UTF-8 当 GBK → `.ps1` 纯 ASCII。
4. git-bash spawn powershell 泄漏 MSYS → idf-bridge 用 `_clean_env()` 清洗 MSYSTEM/mingw（已内置）。
5. python 打印中文撞 GBK 控制台 → hook 用 `ensure_ascii=True` / 脚本 ASCII stdout。

## Serena（已配置可用）
- 已 `uv tool install` serena 1.5.4（`C:/Users/WJ0706/.local/bin/serena.exe`），`.mcp.json` 注册，context=claude-code，`.serena/project.yml` 语言=cpp+python。
- **clangd 经 socks 代理需 PySocks**：已 `uv tool install --with PySocks`，clangd 已下载、C 固件已索引（实测 cpp=1, python=8）。
- 用途：`find_symbol` / `find_referencing_symbols` / `replace_symbol_body` / `rename_symbol` / `get_diagnostics` 等符号级操作（依赖 `build/compile_commands.json`，build 后生成）。

## 进阶（待触发，未默认配置）
- **OpenOCD JTAG / App Trace / GDBStub**：需接硬件 + 在 BOARD.md 确认 S3 原生 USB-JTAG 引出。
- **cv2 视觉 MCP**：当前用 `snap.py` 落盘 + 原生 Read（更稳），如需实时流再上。

## 隔离保证
全部配置在 `WORKplace/.mcp.json` + `.claude/` + repo 内脚本；不碰 `~/.claude.json`、不新增全局插件、不改全局 PATH/环境变量。`.venv-tools` 项目级隔离。
