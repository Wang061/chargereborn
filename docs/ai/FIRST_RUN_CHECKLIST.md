# S3-Forge 首启 Checklist

第一次从 WORKplace 启动并跑通的逐项清单。按顺序勾，遇到问题看底部「排错速查」。

## 0. 启动（最关键）
- [ ] **从 `D:\WJ\jixiebi\WORKplace` 启动 `claude`**（不是外层 `jixiebi`）。
- [ ] 确认项目根：`/status`，或让 Claude `pwd` —— 应为 `…\WORKplace`。
- [ ] 会话开头应出现 session_doctor 注入的「**S3-Forge 项目级 harness 已激活**」横幅 + 版本锁 + BOARD 摘要。没有 → 见排错。

## 1. MCP 连接（3 个项目级 server）
- [ ] `/mcp` 查看：`espressif-documentation`、`idf-bridge`、`serena`。首次为 Pending → **批准**（project 级 server 需一次性授权）。
- [ ] **espressif-documentation 首用**：让 Claude 查一次官方文档（如「查 ledc 在 5.5.4 的 API」）→ 弹浏览器 **OAuth（GitHub/微信）授权一次**。
- [ ] **idf-bridge**：让 Claude 调 `mcp__idf-bridge__size`，或直接 `/esp-build` → 应能跑（内部已清洗 MSYS）。
- [ ] **serena**：clangd 已缓存；让 Claude `find_symbol app_main` 试符号查找。

## 2. 安全与上下文（确认在工作）
- [ ] 安全闸门：`guard.py` 在每个 Bash/PowerShell 前跑。`flash`/`set-target`/`fullclean`/`openocd` 会 **ask**；`erase_flash`/`rm -rf`/读密钥会 **deny**。知道即可，无需手动测。
- [ ] 规则已加载：让 Claude 复述版本锁（IDF 5.5.4 / esp32s3）确认 CLAUDE.md 生效。

## 3. 体检
- [ ] 让 Claude **用 PowerShell 工具**（干净环境）跑 `scripts/ai-doctor.ps1` → 应 `PASS≥6 FAIL=0`（BOARD 未填会有 1 个 WARN）。

## 4. 填板子信息（连硬件前）
- [ ] 填 `docs/ai/BOARD.md`：芯片/板型/flash/PSRAM/**是否原生 USB-JTAG 引出**/COM 口/危险 GPIO/是否允许 AI flash。
- [ ] 填 `docs/ai/SAFETY.md`：舵机限位/PWM 范围/电流预算/急停/上电顺序。
- [ ] 填完 ai-doctor 的 BOARD WARN 应消失。

## 5. 软件闭环（无需硬件）
- [ ] `/esp-build` → 应绿（确认 target=esp32s3、产物 `--chip esp32s3`）。
- [ ] 可选：故意写个编译错误 → `/esp-build` 红 → 看错误分类 + `esp-build-fix` 是否定位 → 修复回绿。
- [ ] 可选：`/esp-loop` 试自动迭代到绿（注意它写/删 `.claude/.loop_active`）。

## 6. 硬件闭环（接上 S3 板后）
- [ ] 插 USB → 确认 COM 口（让 Claude 扫端口，填 `docs/ai/PORTS.md`）。
- [ ] `/esp-flash`（**ask 会让你确认**；注意 SAFETY.md：先抬空/限速/不接动力电）。
- [ ] `/esp-monitor` → 看启动日志、无 boot loop。
- [ ] 制造一个 panic（如空指针解引用）→ `/esp-panic` → `coredump_summary` 解码到 函数+file:line。
- [ ] 接摄像头：`/esp-snap` → Claude Read 图像判 LED/舵机/夹爪。
- [ ] 有 JTAG 且 BOARD.md 确认原生 USB-JTAG → 可让我配 OpenOCD 断点流（进阶）。

## 7. 经验沉淀
- [ ] 修好第一个真实 bug → `/learn` → 写入 `docs/ai/CRASH_SIGNATURES.md`。

---

## 排错速查（均为本机实测坑）
| 现象 | 原因 / 解法 |
|---|---|
| idf 报 `MSys/Mingw is no longer supported` | 别用 Bash 直跑 idf；走 `mcp__idf-bridge__*`（已 `_clean_env()` 清洗 MSYS） |
| ai-doctor idf 检查 WARN「run in clean PowerShell」 | 经 git-bash 跑导致；让 Claude 用 **PowerShell 工具**跑 ai-doctor |
| `/mcp` 里 server 没连上/Pending | 批准 project servers；必要时重启会话；`claude mcp list` 核对 |
| serena clangd 报 `Missing SOCKS support` | 已 `uv tool install --with PySocks` 修复；若重装 serena 需再加 `--with PySocks` |
| pip 装包报 SOCKS / 卡住 | socks 代理坑：`ALL_PROXY= pip install -i https://pypi.tuna.tsinghua.edu.cn/simple ...` |
| `.ps1` 报 ParserError / 乱码 | PS5.1 把 UTF-8 当 GBK；`.ps1` 必须 ASCII-only |
| flash 连接失败 | 按住 BOOT 上电进下载模式；数据线非充电线；波特率 921600→460800 |
| target 变回 esp32 | 别 `rm -rf build sdkconfig` 后直接 build；先 `set-target esp32s3` |

> 详见 `README.system.md`、`docs/ai/DECISIONS.md`、设计文档 `docs/superpowers/specs/2026-06-02-s3-forge-design.md`。
