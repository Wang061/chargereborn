# S3‑Forge 设计文档（Design Spec）
**Claude Code 驱动的 ESP32‑S3 / ESP‑IDF 5.5.4 极限嵌入式开发底座**

- 版本：v1.0（设计定稿待 review）
- 日期：2026‑06‑02
- 落点：`D:\WJ\jixiebi\WORKplace`（ESP‑IDF 工程根，以后从此目录启动 `claude`）
- 方案：**B｜全功能拆装版** —— repo‑resident、最强、严格项目级隔离
- 关联研究：`WORKplace/_research/synthesis.md`（6 agent 联网核实报告）、`_research/raw_findings.json`、`_research/other_ai_plan.txt`

---

## 1. 目标与硬约束

### 1.1 目标
搭一套**项目级**的、当前最强的 Claude Code 嵌入式开发辅助系统，打通需求 → 编码 → 构建 → 烧录 → 监视 → 诊断 → 修复 → 视觉判读 → 经验沉淀的**单一闭环**，并把高频故障变成可复用资产。

### 1.2 硬约束（必须满足）
1. **严格项目级隔离**：所有配置只能影响本工程，不得污染用户其它 27 个项目（不碰 `~/.claude.json` 顶层、不新增全局插件/MCP、不改全局 PATH/环境变量）。
2. **版本锁**：`IDF=5.5.4`、`target=esp32s3`，禁止自动升级 IDF 主版本或偷改 target。
3. **不翻车**：禁止未经确认的 `flash`/`erase`/`fullclean`/`rm -rf`/读密钥。
4. **可读性 + 文档化**：每个部件职责单一、有文档、可独立理解与测试。
5. **安装边界**：项目内（npm/pip/项目级 `.mcp.json`/拷贝载荷进 `.claude/`）自动执行；改全局 PATH/环境变量/全局配置、克隆大仓库、装系统级工具前先询问用户。

### 1.3 设计目标（五个"单一"）
- 单底座：只认 Claude Code。
- 单主线：只认 ESP‑IDF 5.5.4 + ESP32‑S3。
- 单闭环：一条链打通构建到诊断。
- 少冲突：只保留一个编排脊柱、一个构建入口、一个监视/日志入口。
- 强覆盖：把"全功能"拆成适合嵌入式的 skills / hooks / commands / MCP。

---

## 2. 已核实的环境事实（实现以此为准）

| 项 | 事实 |
|---|---|
| 机器/Shell | Windows 11 + git‑bash（MSYS）；亦可用 PowerShell 工具 |
| ESP‑IDF | **v5.5.4 已装** `D:\WJ\Espressif\frameworks\esp-idf-v5.5.4\`；`IDF_TOOLS_PATH=D:\WJ\Espressif` |
| IDF 激活入口 | `D:\WJ\Espressif\Initialize-Idf.ps1 -IdfId esp-idf-664c96ea6a8aaf556c400ba925468017` |
| IDF Python | `D:/WJ/Espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe` |
| 系统 Python | `D:/anaconda/python`（**原生 Windows，不认 `/c/...`，脚本/hook 必须用 Windows 路径**） |
| Node/npm/npx | `D:/WJ/Node.js/` |
| cmake | `D:/WJ/cmake/bin/cmake`；ninja/openocd 在 IDF 工具内（激活后可用） |
| uv/uvx | 未在 PATH（Serena 排 phase 2 时再定位/安装） |
| 已有全局 MCP（8） | memory, embedded-debugger(probe-rs), context7, taskmaster-ai, playwright, gitnexus, codegraphcontext, sequential-thinking |
| 已有全局插件 | `superpowers@claude-plugins-official`（启用） |
| 项目级 MCP | `D:/WJ/jixiebi` 与 `WORKplace` 当前**均无**项目级 MCP（干净起点） |
| 硬件 | 此刻无设备连接（COM 空、probe-rs 无探针）；用户具备 USB‑S3 + JTAG 探针 + 摄像头，HIL 做成即插即用 |

---

## 3. 关键技术裁决（高置信，有官方一手来源）

1. **probe‑rs / 已装 `embedded-debugger` MCP 对 S3 的 C 项目判为不可用。**
   probe‑rs 虽支持 S3 的 Xtensa（连接/halt/烧录/硬件断点），但其 RTT 只读 Rust crate 发布的 RTT 控制块；**ESP‑IDF 的 C 日志（`ESP_LOGx`/printf）走串口，probe‑rs 完全看不见**。→ S3 调试**不建立在 probe‑rs 上**。
2. **S3 调试正解 = OpenOCD（Espressif fork，随 IDF 自带）+ idf.py monitor/coredump。**
   S3 板载 USB‑Serial‑JTAG，`board/esp32s3-builtin.cfg` **零外接**即可断点调试。
3. **ESP‑IDF 5.5.4 没有官方 idf MCP**（`idf.py mcp-server` 是 v6.0 才引入）→ 自写本地 stdio `idf-bridge` 提交进 repo。
4. **官方 Espressif Documentation MCP 为真**：远程 http，单工具 `search_espressif_sources(query, language)`；项目级一行加入；首用触发 GitHub/微信 OAuth；浏览器 GET 返回 405 属正常 MCP 传输。
5. **Claude Code settings 5 层优先级（高→低）**：Managed/Enterprise > CLI 参数 > **Local（`.claude/settings.local.json`）> Project（`.claude/settings.json`）> User（`~/.claude/settings.json`）**。（数组拼接去重，对象深合并，标量高层胜。）
6. **`permissions.allow` 里若含裸 `Bash`，PreToolUse 的 deny/ask 会被忽略（#18312）** → 本设计 allow 中**绝不放裸 `Bash`**。
7. **插件可项目级启用**：`enabledPlugins`（对象）写入项目 `settings.json`；但激活不随目录自动切换（#11461）→ 真隔离首选"载荷住在 repo 里"，本设计据此**全部 repo‑resident**。

### 3.1 打假记录（其它 AI 计划中的虚构项，永不引入）
- `mcpd @ ai.boce.com/mcp/8944.html` —— URL 编造。
- `JosephR26/esp32-devops-mcp` —— 查无实据（仅聚合器孤证，无真 repo/npm）。
- `npx add-skill H1D/agent-skills-esp32` —— 安装命令语法错且低价值（3 star）。
- ECC star 数（182K/202K）灌水；ECC 的 "embedded" 指"嵌入多 AI harness"，**与 ESP‑IDF 无关**。

---

## 4. 架构总览（分层）

```
┌─ 骨架层（方法论 + 编排）────────────────────────────────┐
│ Superpowers（已全局，做方法论脊柱：brainstorm→plan→TDD→review）│
│ + 编排"精华"以 repo-resident 形式实现（/esp-loop ralph 迭代、8 ESP agent）│
│   —— 不装任何全局编排器插件，规避 #11461 泄漏                │
└──────────────────────────────────────────────────────────┘
┌─ MCP 层（./.mcp.json，全项目级）─────────────────────────┐
│ espressif-documentation (http, MUST)                      │
│ idf-bridge (自写 stdio, MUST)                              │
│ serena (stdio, SHOULD/phase2, 需 uv + compile_commands)    │
│ ＋复用全局 context7（拉 IDF v5.5 API 文档）                  │
└──────────────────────────────────────────────────────────┘
┌─ 调试环（全是 IDF 自带 / menuconfig，零新装）────────────┐
│ idf.py monitor + addr2line 解码（MUST，带超时捕获）         │
│ Core Dump→Flash + coredump-info/debug（MUST）              │
│ OpenOCD(Espressif fork) + idf.py gdb（S3 板载 USB-JTAG）    │
│ App Trace / GDBStub（OPTIONAL 进阶）                       │
│ 视觉：/esp-snap 拍照落盘 + 原生 Read（不走 MCP，最稳）       │
│ ⚠ 不用 probe-rs / embedded-debugger 做 S3 环               │
└──────────────────────────────────────────────────────────┘
┌─ Harness 机制层（./.claude/）────────────────────────────┐
│ settings.json：enabledPlugins + permissions(无裸Bash) + hooks + env │
│ hooks：session_doctor / guard / post_build_log / ralph_stop（全 python）│
│ agents(8) / commands(14) / skills(10) / rules / CLAUDE.md  │
└──────────────────────────────────────────────────────────┘
```

---

## 5. 目录树（落地结构）

```
WORKplace/                       ← Claude Code 项目根（从这里启动 claude）
├─ .mcp.json                     ← 项目级 MCP（§6）
├─ .gitignore                    ← build/ logs/ .venv-tools/ ~$*.docx 等
├─ .claude/
│  ├─ CLAUDE.md                  ← 主规则（§11）
│  ├─ settings.json              ← 权限/hooks/env/enabledPlugins（§7）
│  ├─ settings.local.json        ← 个人本地权限（迁入现有 web/python 许可）
│  ├─ agents/                    ← 8 个 ESP 专用 subagent（§9）
│  ├─ commands/                  ← 14 个 slash 命令（§10）
│  ├─ skills/                    ← 10 个精品 ESP 技能（§12）
│  ├─ hooks/                     ← 4 个 python 钩子（§8）
│  ├─ rules/                     ← version-lock / coding / safety / verification
│  └─ statusline.ps1             ← 可选状态栏
├─ scripts/
│  ├─ idf.ps1                    ← IDF5.5.4 激活包装器（唯一激活入口，§13）
│  ├─ idf_bridge_mcp.py          ← 自写本地 idf MCP（§6.2）
│  ├─ serial_capture.py          ← 带超时串口捕获（§13）
│  ├─ snap.py                    ← 摄像头抓帧落盘（§13）
│  └─ ai-doctor.ps1              ← 健康体检（§13）
├─ docs/ai/
│  ├─ BOARD.md  SAFETY.md  PORTS.md  DECISIONS.md  CRASH_SIGNATURES.md（§14）
│  └─ ../superpowers/specs/      ← 本设计文档所在
├─ logs/  build/ flash/ monitor/ panic/ vision/   （gitignore）
├─ tests/  host/ integration/    ← 现有 pytest 迁入
├─ sdkconfig.defaults            ← 通用默认
├─ sdkconfig.defaults.esp32s3    ← S3 专属 + 启 Core Dump→Flash（§15）
├─ partitions.csv                ← 含 coredump 分区（§15）
├─ CMakeLists.txt / main/        ← 现有固件，保留
├─ README.system.md              ← 系统总说明
└─ _research/                    ← 调研产物
```

---

## 6. MCP 层规格（`.mcp.json`）

### 6.1 `.mcp.json` 内容（形态）
```json
{
  "mcpServers": {
    "espressif-documentation": {
      "type": "http",
      "url": "https://mcp.espressif.com/docs"
    },
    "idf-bridge": {
      "type": "stdio",
      "command": "${CLAUDE_PROJECT_DIR:-.}/.venv-tools/Scripts/python.exe",
      "args": ["${CLAUDE_PROJECT_DIR:-.}/scripts/idf_bridge_mcp.py"],
      "env": { "IDF_TOOLS_PATH": "D:\\WJ\\Espressif" }
    }
  }
}
```
- **必须用 `${CLAUDE_PROJECT_DIR:-.}` 默认值形式**（官方核实）：项目级 `.mcp.json` 里 `${CLAUDE_PROJECT_DIR}` 不在 Claude 自身环境、展开会失败；带默认值则回退到 cwd（从 WORKplace 启动即对）。亦可在 `idf_bridge_mcp.py` 内直接读 `os.environ["CLAUDE_PROJECT_DIR"]`（子进程里可用）。
- `serena` 待 phase 2 再加（需 `uv` + `compile_commands.json`）：
  `uvx --from git+https://github.com/oraios/serena serena start-mcp-server --context ide-assistant --project ${CLAUDE_PROJECT_DIR:-.}`
- `context7` 已全局，**不重复加**。
- 视觉**不做成 MCP**（见 §13 `snap.py`），降低崩溃面。
- 备注：stdio 服务器**严禁向 stdout 打印非 JSON‑RPC 内容**（日志走 stderr/文件）。

### 6.2 自写 `idf-bridge` MCP 工具集（替代 5.5.4 缺失的官方）
所有工具内部 shell out 到 `scripts/idf.ps1`（激活 IDF 后执行），返回结构化结果（状态 + 关键输出 + 错误分类）。
**这是 build/flash/monitor 的主入口**——slash 命令统一优先调用 `mcp__idf-bridge__*`。`build`/`flash` 工具**内部完成日志归档到 `logs/` 与错误分类**，因此核心路径**不依赖 PostToolUse**（PostToolUse 仅作 Bash 直调路径的兜底，见 §8）。这样消除了"入口不一致"（slash→MCP、归档→MCP 内部，单一真相）。

| 工具 | 权限 | 说明 |
|---|---|---|
| `build()` | allow | 运行 `idf.ps1 build`，返回成功/失败 + 日志尾 + 错误分类（include 缺失/未定义符号/Kconfig 依赖/target 不符/链接区溢出/组件未找到） |
| `size()` | allow | 产物大小/各区占用 |
| `set_target(chip)` | ask | 默认拒非 esp32s3 |
| `flash(port?)` | ask | 烧录；端口缺省自动探测 |
| `monitor_start(port?, decode=true)` | ask | **非阻塞**：后台子进程把串口落 `logs/monitor/<ts>.log`，`ESP_MONITOR_DECODE` 开，返回日志路径与句柄 |
| `monitor_read(lines=200)` | allow | 读后台缓冲（轮询，避免挂死 agent） |
| `monitor_stop()` | allow | 结束监视子进程 |
| `coredump_summary()` | allow | `idf.py coredump-info` 文本摘要（任务/寄存器/原因） |
| `openocd_start(cfg=esp32s3-builtin)` | ask | 后台起 OpenOCD（板载 USB‑JTAG） |
| `gdb_batch(script_path)` | ask | `gdb -x <script> -batch`，确定可解析文本 |

**设计要点**：`monitor`/`openocd` 是长任务，必须后台子进程 + 轮询读取，**不可同步阻塞**（这是 Bash 直跑 monitor 会挂死 agent 的根因）。

---

## 7. 安全与权限（`.claude/settings.json`）

```json
{
  "enabledPlugins": { "superpowers@claude-plugins-official": true },
  "env": { "ESP_MONITOR_DECODE": "1" },
  "permissions": {
    "deny": [
      "Read(./.env)", "Read(./.env.*)", "Read(./**/.env*)", "Read(./**/config.env)",
      "Read(./secrets/**)", "Read(./**/*.pem)", "Read(./**/*.key)",
      "Bash(idf.py erase-flash:*)", "Bash(esptool.py erase_flash:*)", "Bash(esptool erase_flash:*)",
      "Bash(rm -rf:*)"
    ],
    "ask": [
      "Bash(scripts/idf.ps1 flash:*)", "Bash(scripts/idf.ps1 set-target:*)",
      "Bash(idf.py flash:*)", "Bash(idf.py set-target:*)", "Bash(idf.py fullclean:*)",
      "Bash(git push:*)", "Bash(pip install:*)", "Bash(uv:*)",
      "mcp__idf-bridge__flash", "mcp__idf-bridge__set_target",
      "mcp__idf-bridge__openocd_start", "mcp__idf-bridge__gdb_batch", "mcp__idf-bridge__monitor_start"
    ],
    "allow": [
      "Bash(scripts/idf.ps1 build)", "Bash(scripts/idf.ps1 size)", "Bash(scripts/idf.ps1 reconfigure)",
      "Read(./**)", "Read(../4.源代码程序/**)",
      "mcp__espressif-documentation__*",
      "mcp__idf-bridge__build", "mcp__idf-bridge__size",
      "mcp__idf-bridge__monitor_read", "mcp__idf-bridge__monitor_stop", "mcp__idf-bridge__coredump_summary"
    ]
  },
  "hooks": { "...": "见 §8" }
}
```
- **铁律：allow 不含裸 `Bash`**（否则 guard hook 失效）。
- `deny → ask → allow` 首个匹配胜；规则跨作用域**合并**。
- 声明式权限是第一层，`guard.py`（PreToolUse 正则，覆盖 **Bash + PowerShell** 两个工具）是更狠的第二层兜底。
- **主安全面 = `idf-bridge` MCP 工具名权限**（`mcp__idf-bridge__flash/set_target/openocd_start/...`=ask）：工具名匹配**无歧义**，是 flash/monitor/openocd 的首选入口。
- **次要：Bash 直调 `idf.ps1`**——Windows 下 `.ps1` 经 `powershell -File ...` 调用，上面 `Bash(scripts/idf.ps1 ...)` 仅为示意；实现时按真实调用形态（`Bash(powershell*idf.ps1 flash*)` 之类）补全 ask/deny，并由 `guard.py` 正则兜底（guard 读到的 `.tool_input.command` 含 "idf.ps1 flash" 即可命中）。
- **PowerShell 工具决策（回应 review，核实后）：默认不启用 `CLAUDE_CODE_USE_POWERSHELL_TOOL`。** 官方确认开启它会使 **Auto 权限模式失效（每次 PS 调用都弹确认）、sandbox 失效**——对自动迭代环是负担。核心 build/flash/monitor 走 `idf-bridge` MCP（python 内部 `subprocess` 调 `powershell -File idf.ps1`），Claude 自身几乎不直接用 PS；偶发 PS 脚本（doctor/扫端口）经 Bash `powershell -File ...` 调用，由 `guard.py`(Bash matcher) 兜底。若你日后偏好原生 PS 工具，再在 `env` 加 `"CLAUDE_CODE_USE_POWERSHELL_TOOL":"1"` + `"defaultShell":"powershell"`（接受其 Auto/sandbox 限制）。

---

## 8. Hooks 链（4 个，全 python）

> 选 python 的原因：python 在 PATH 上、跨平台、且能精准处理"anaconda 不认 `/c/...`"的坑（hook 内一律用 `os.environ` 给的原生 Windows 路径，不手拼 MSYS 路径）。
> **Hook I/O 契约（官方核实）**：从 stdin 读 JSON。
> - **PreToolUse 决策**：stdout 打印 `{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"allow|deny|ask|defer","permissionDecisionReason":"..."}}` 并 `exit 0`。`exit 0 无输出 = 不决策`（沉默≠放行）；`exit 2`（任意 stderr）= 直接拦截、JSON 被忽略、stderr 喂回 Claude。
> - **matcher 是工具名正则**，可匹配 MCP 工具 `mcp__<server>__<tool>`。⚠️ **裸名（仅字母/下划线/`|`）按精确字符串匹配**，要触发正则必须含元字符——匹配某 server 全部工具用 `mcp__idf-bridge__.*`（不是 `mcp__idf-bridge`）。

| Hook 事件 | matcher | 脚本 | 职责 |
|---|---|---|---|
| `SessionStart` | — | `session_doctor.py` | 体检：cwd 是否 IDF 工程、`idf.py --version`==5.5.4、target==esp32s3；注入 `BOARD.md`/`SAFETY.md` 摘要；提示"先 build 别 flash" |
| `PreToolUse` | `Bash`、`PowerShell` | `guard.py` | 正则兜底（**同时挂在 Bash 与 PowerShell 两个 matcher 上**）：`erase[_-]flash`/`\brm\s+-rf\b`/读 `.env\|.pem\|.key` → **deny**；`flash`/`set-target`/`fullclean` → **ask**；附原因 |
| `PostToolUse` | `Bash`、`mcp__idf-bridge__.*` | `post_build_log.py` | **兜底**归档/分类（**主归档在 `idf-bridge` 工具内部完成**，见 §6.2）：Bash 直调 build/flash 时归档日志到 `logs/` 并分类，panic 时摘要前后 200 行到 `logs/panic/`。matcher 用正则形式 `mcp__idf-bridge__.*`（裸名会失效） |
| `Stop` | — | `ralph_stop.py` | **/ralph 迭代控制器（精确契约）**：仅当存在 `.claude/.loop_active`（由 `/esp-loop` 写，含 `max_iterations`+"完成短语"）、未命中完成短语、未达上限时，**stdout 输出 top-level `{"decision":"block","reason":"<下一步指令>"}` + `exit 0`** 强制续跑（`reason` 当作下一步指令喂回）；否则清哨兵正常收尾。⚠️ **必须读输入的 `stop_hook_active`：为 `true` 时直接 `exit 0` 放行**——这是官方防无限循环的关键；再叠 `max_iterations` + 每轮产物/`git` 快照，三重防跑飞 |

---

## 9. Agents（8 个 ESP 专用 subagent，`.claude/agents/`）

每个 agent = 一个 `.md`（frontmatter: name/description/tools/model）+ 系统提示，职责单一、可独立调用。

| Agent | 职责 |
|---|---|
| `esp-planner` | 拆任务、定义"要改哪些文件/为什么/如何验证"，永远先写计划 |
| `esp-build-fix` | 专攻构建错误：CMake / Kconfig / 链接 / 组件依赖 / include / target 不符 |
| `esp-flash-debugger` | flash / 端口 / 握手失败 / 下载速率 / stub / USB‑JTAG 问题 |
| `esp-monitor-triage` | 串口日志：Guru Meditation / WDT / brownout / 栈溢出 / 堆损坏 / assert / abort / boot loop |
| `esp-freertos-reviewer` | 任务模型 / 栈 / 优先级 / 队列 / 事件组 / 死锁 / ISR 使用边界 |
| `esp-driver-architect` | 组件抽象 / HAL 分层 / BSP / driver API 设计 |
| `esp-low-power-tuner` | light/deep sleep / 唤醒源 / 电流预算 / 外设关断 |
| `esp-release-guardian` | 版本 / 分区 / OTA / factory app / NVS 兼容性审查 |

---

## 10. Commands（14 个，`.claude/commands/`）

| 命令 | 作用 / 触发 |
|---|---|
| `/esp-build` | `idf.ps1 build`（免确认）→ PostToolUse 归档分类；失败转 `esp-build-fix` |
| `/esp-flash` | 确认后烧录（板载/指定端口） |
| `/esp-monitor` | `serial_capture.py` 带超时落盘 → Read（已 addr2line 解码） |
| `/esp-fix` | 读分类后的构建/运行错误，选 skill，改最小范围文件 |
| `/esp-panic` | `coredump-info` + `esp-monitor-triage` 定位崩溃 |
| `/esp-loop` | **ralph 迭代到编译绿**：写 `.loop_active`（带 max_iterations + 完成短语），交 `ralph_stop` 续跑 |
| `/esp-snap` | `snap.py` 拍开发板 → Read 图像判断 LED/舵机/夹爪物理现象 |
| `/esp-menucheck` | 分析 sdkconfig/menuconfig 改动风险 |
| `/esp-partition` | 检查 `partitions.csv` 与 OTA/coredump 布局 |
| `/freertos-review` | 调 `esp-freertos-reviewer` |
| `/driver-review` | 调 `esp-driver-architect` |
| `/power-review` | 调 `esp-low-power-tuner` |
| `/release-check` | 发版前全套检查（调 `esp-release-guardian`） |
| `/learn` | 把本次修的 bug 沉淀为 `CRASH_SIGNATURES.md` 条目或新 skill —— **经验资产化** |

---

## 11. 主规则（`CLAUDE.md`）与 `rules/`

**版本硬规则（写死）**：`IDF_VERSION=5.5.4`、`IDF_TARGET=esp32s3`、`BUILD 经 scripts/idf.ps1`、`FLASH_BAUD=921600 失败回退 460800`、`MONITOR_BAUD=115200`、`SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3`。

- `rules/version-lock.md`：禁止自动升级 IDF 主版本/偷改 target/默认切 latest/master/6.x。
- `rules/coding-standard.md`：新增模块必须组件化（不塞 main）；驱动与业务分层；统一 TAG/日志等级；禁 magic number；ISR 禁阻塞/重活；任务创建必须声明栈与优先级理由。
- `rules/safety.md`：涉及 sdkconfig/partitions/boot/OTA/NVS/clock/power/flash/PSRAM 的修改必须显式说明风险并要求 build(+boot) 校验；禁止未说明 fullclean / 随意 erase_flash / 覆盖 BOARD 配置。
- `rules/verification.md`：改功能必 build；改启动路径必 flash+boot check；改并发必 monitor 验证；改存储/分区必 layout 校验。

---

## 12. Skills（先做 10 个精品，`.claude/skills/`，可扩 30+）

`esp32s3-bringup` · `esp-idf-build-fix` · `esp-flash-recovery` · `esp-monitor-triage` · `esp-panic-backtrace` · `freertos-task-design` · `heap-memory-triage` · `esp-partition-ota` · `sdkconfig-change-review` · `embedded-code-review`。

每个 skill = `SKILL.md`（name/description/触发条件 + 步骤 + 检查清单）。Superpowers 提供通用方法论脊柱，这些提供 ESP 领域纵深。`/learn` 会把新故障签名增量写入。

---

## 13. 关键脚本规格

### 13.1 `scripts/idf.ps1`（唯一 IDF 激活入口）
```powershell
param([Parameter(ValueFromRemainingArguments=$true)]$IdfArgs)
& "D:\WJ\Espressif\Initialize-Idf.ps1" -IdfId "esp-idf-664c96ea6a8aaf556c400ba925468017" | Out-Null
idf.py @IdfArgs
exit $LASTEXITCODE
```
（实现时验证 `Initialize-Idf.ps1` 的参数名；失败回退 `idf_cmd_init.bat`。）

### 13.2 `scripts/serial_capture.py`
带超时把指定串口读 N 秒落 `logs/monitor/<ts>.log`，到时主动结束，**绝不挂死**；可选调 IDF 的 addr2line 解码。原生 Windows 路径。

### 13.3 `scripts/snap.py`（给 AI"眼睛"）
`cv2.VideoCapture(0, cv2.CAP_DSHOW)` 抓一帧存 `logs/vision/snap-<ts>.jpg` 并打印路径；Claude Code 原生 Read 该 JPG 做视觉判读。**零常驻进程、不可崩**。

### 13.4 `scripts/ai-doctor.ps1`
体检：IDF 5.5.4？target=s3？`.venv-tools` 与 python 依赖？`.mcp.json` 已注册（`claude mcp list`）？`BOARD.md` 已填？输出红/绿清单。

---

## 14. 文档载体（`docs/ai/`）

- `BOARD.md`：芯片=esp32s3、板型、flash 大小、是否 PSRAM、引脚图、危险 GPIO、是否允许 AI flash（默认 NO/ask）、COM 口（连上后填）。
- `SAFETY.md`：硬件安全（舵机限位/电流预算/AI 绝不可自动做的动作）。
- `PORTS.md`：串口 / JTAG 端口记录。
- `DECISIONS.md`：架构决策日志（ADR）。
- `CRASH_SIGNATURES.md`：`/learn` 沉淀的崩溃签名库（症状 → 根因 → 修法）。
- 根 `README.system.md`：系统如何工作、如何从 WORKplace 启动、闭环怎么跑。

---

## 15. 固件侧最小改动（仅为支撑调试环）

- `sdkconfig.defaults.esp32s3`：启 **Core Dump → Flash（ELF 格式）**、保 `ESP_MONITOR_DECODE`、合理日志等级。
- `partitions.csv`：加入 `coredump` 分区。
- 不动现有业务代码；固件重构属另案（各自 spec）。

---

## 16. 闭环流程（落地）

**默认闭环（无探针，最可自动化）**
```
/esp-build →(失败) esp-build-fix 改最小文件 →回build   [/esp-loop 自动迭代到绿]
 →(绿) /esp-flash(确认) → /esp-monitor(超时落盘) → Read(回溯已解码)
 →(panic) /esp-panic → coredump_summary → esp-monitor-triage 定位
 → /esp-snap 拍板子 → Read 图像判物理现象 →(不对)回改代码
 →(需断点) idf-bridge openocd_start + gdb_batch(script)  ← 见下注：JTAG 路径取决于板卡
 →(通过) /release-check 或 /learn 沉淀经验
```
> **JTAG 注（条件成立才"零外接"）**：仅当**具体板卡把 ESP32‑S3 原生 USB‑Serial‑JTAG 引出**（如 ESP32‑S3‑DevKitC‑1 的 USB 口）时，`esp32s3-builtin.cfg` 才零外接可用；若板卡只用 UART 桥接芯片（CH340/CP210x）而未引出原生 USB‑JTAG，则需外接探针（ESP‑Prog/J‑Link）+ 对应 OpenOCD interface cfg。**此前提必须在 `BOARD.md` 里核验后再依赖**（连上硬件即查）。

**故障修复流**：读失败日志 → 归类 → 选 skill → 改最小范围 → 重建 → 重刷 → 再 monitor → `/learn` 记签名。
**Bring‑up 流**：校验板卡定义 → 电源/USB/串口识别 → blink/log 最小验证 → 外设逐项启用 → FreeRTOS 任务逐层加 → 网络/存储最后接入。

---

## 17. 项目级隔离保证（如何确保不泄漏）

| 机制 | 隔离方式 |
|---|---|
| MCP | 全部写 `WORKplace/.mcp.json`，不碰 `~/.claude.json` 顶层 |
| 插件 | 仅 `enabledPlugins` 在项目 `settings.json` 启已全局缓存的 Superpowers；**不新增全局插件**；编排"精华"全 repo‑resident（规避 #11461） |
| 权限/hooks/skills/agents/commands/rules | 全在 `WORKplace/.claude/`，随 repo 走 |
| python 依赖 | 项目级 `.venv-tools`，不污染 anaconda base |
| 环境变量/PATH | **不改全局**；IDF 激活由 `idf.ps1` 每次调用内完成 |
| 启动方式 | 从 `WORKplace` 启动 claude，作用域天然限定 |

> **明确例外（回应 review）**：权限 allow 含 `Read(../4.源代码程序/**)` —— 这是**唯一**越过项目根的项，且**只读**，用途是让 agent 参考旧 OpenMV/ESP32 源码做迁移对照。判定为**受控例外**而非泄漏：只读、范围限定该一个目录、不写不执行。若你不需要该旁路，删除此 allow 项即可恢复纯项目内。

---

## 18. 分阶段落地顺序

- **Phase 0 — 地基 + 版本控制**：① **先写 `.gitignore`（含 `**/build/`、`logs/`、`.venv-tools/`、`settings.local.json`、`*.elf/*.bin/*.map`、`**/config.env`、`~$*`）→ `git init` → `git add -A` → `git status` 核验无构建产物入库 → 首次提交**（纳入源码+设计文档+调研+harness，排除上述）。当前 WORKplace 850 文件/61MB，**832 文件在 `hello_world/build/`**，务必先挡。② 建 `.venv-tools`（pip: pyserial/opencv-python/mcp）；③ 写 `scripts/idf.ps1` 并验证裸 build 可跑。
- **Phase 1 — 安全与机制**：`settings.json`（permissions + hooks）、4 个 hooks、`CLAUDE.md` + rules、`docs/ai/*` 模板、`session_doctor`/`ai-doctor`。
- **Phase 2 — MCP 与命令**：`.mcp.json`（espressif-docs + idf-bridge）、`idf_bridge_mcp.py`、`serial_capture.py`、`snap.py`、3 个核心命令 `/esp-build` `/esp-flash` `/esp-monitor` + `/esp-snap`。
- **Phase 3 — agents/skills/闭环**：8 agents、10 skills、其余命令、`/esp-loop` ralph 环、`/learn`。
- **Phase 4 — 进阶（可选）**：Serena（装/定位 uv + compile_commands）、OpenOCD JTAG 流、App Trace、cv2 视觉 MCP、固件侧 coredump 配置。
- **Phase 5 — 体检与文档**：`ai-doctor` 全绿、`README.system.md` 完稿、首次 `/learn` 跑通。

---

## 19. 需用户参与的清单（其余全自动）

1. **Espressif Docs MCP 首用**：浏览器 OAuth（GitHub/微信）一次。
2. **`BOARD.md`**：板型/flash/PSRAM/引脚/危险 GPIO（连硬件时填）。
3. **硬件连接时**：确认 COM 口、JTAG 探针型号（默认先用 S3 板载 USB‑JTAG）。
4. **Serena 的 `uv`**：若需 Serena 而 uv 缺失，安装 uv 前会再问（系统级）。
5. **每次 flash/erase/set-target**：按 ask 当场确认。

---

## 20. 置信度与已知缺口

- **高置信**：所有 harness 机制、Espressif Docs MCP、5.5.4 无官方 idf MCP、probe‑rs RTT 对 C 日志失效、OpenOCD 是 S3 正解、settings 5 层优先级、#18312/#11461 —— 均有官方一手来源。
- **待实现期验证**：`Initialize-Idf.ps1` 精确参数名；`uv` 是否已装于 `D:/WJ`；Serena 对 IDF 生成的 `compile_commands.json` + Xtensa include 的可见性；hook `command` 在 Windows 下的 python 调用形态。
- **范围外（另案）**：固件业务重构（双 S3 协同 / 电池识别迁 S3 / ESP‑DL）—— 本 harness 只服务开发，不含产品功能实现。

---

## 21. 成功标准
1. 从 WORKplace 启动 claude，`ai-doctor` 全绿。
2. `/esp-build` 一键构建并自动归档分类日志；失败可经 `/esp-fix` 或 `/esp-loop` 修到绿。
3. `/esp-flash`+`/esp-monitor` 跑通，崩溃回溯被 addr2line 解码、coredump 可解析。
4. `/esp-snap` 拍板子，Claude 能据图判断物理现象。
5. 危险命令（erase/rm -rf/读密钥）被 deny，flash/set-target 被 ask。
6. 全部配置仅作用于本工程，其它项目零变化。
7. `/learn` 能把一次真实故障沉淀进 `CRASH_SIGNATURES.md`。

---

## 22. Review 响应与修订记录（2026‑06‑02 · 用户 review + claude-code-guide 官方核实）

**逐条对账：**

| # | Review 点 | 裁决 | 本版处理 |
|---|---|---|---|
| 1 | `.mcp.json` 用 `${CLAUDE_PROJECT_DIR}` 不稳 | ✅成立 | §6.1 改 `${CLAUDE_PROJECT_DIR:-.}`，并说明可在 server 内读 env（官方确认该变量不在 Claude 自身环境，需默认值）|
| 2 | `Read(../4.源代码程序/**)` 与隔离冲突 / 补 .env deny | ✅成立 | §17 记为"受控只读例外"；§7 deny 补 `.env*`/`**/config.env` |
| 3 | 入口不一致（MCP 为主 vs Bash 直调） | ✅成立 | 定死：slash→`mcp__idf-bridge__*` 主入口，归档/分类在 bridge **内部**完成；PostToolUse 降为兜底并匹配 `mcp__idf-bridge__.*` |
| 4 | 显式加 `CLAUDE_CODE_USE_POWERSHELL_TOOL=1` | ⚖️部分采纳+反向建议 | 核实该变量为真，但**开启会废 Auto 权限模式/sandbox**；**默认不开**、核心走 MCP；§7 给出按需开启法 |
| 5 | Stop hook 续跑契约 | ✅成立 | §8 改精确 `{"decision":"block","reason":...}`+exit0，且**必读 `stop_hook_active` 防无限循环** |
| 6 | `.gitignore` 用 `**/build/` | ✅成立 | 已写 `**/build/`+`**/config.env`；实测产物只在 `hello_world/build/` |
| 7 | 首次提交纳入范围 | ✅采纳 | §18 Phase0：先 gitignore→init→add→**status 核验**→commit（纳源码/文档/调研/harness，排产物）|
| 8 | "板载 USB‑JTAG 零外接"改条件句 | ✅成立 | §16 加 JTAG 条件注，前提移交 `BOARD.md` 核验 |

**环境事实修正（本轮体检）：**
- WORKplace 850 文件 / 61MB，**832 在 `hello_world/build/`**；两个 `config.env` 均在该 build 内；无 `.pem/.key`；原无 `.gitignore`。
- 官方核实新增确定项：PostToolUse/PreToolUse **可匹配 MCP 工具**（正则 `mcp__<server>__.*`，裸名失效）；PreToolUse 决策值含 `defer`；hook `exit 2` 走 stderr 拦截。

**新开放项（需你定，非 harness 阻塞）：**
- **两套 IDF 骨架**：根 `main/`+`CMakeLists.txt`（未构建）与 `hello_world/`（已构建 61MB）并存。**以哪套为 S3‑Forge 固件主工程？** 建议 Phase 0 收敛为单一工程（留一删一或合并），否则 `idf.ps1` 的工作目录有歧义。
