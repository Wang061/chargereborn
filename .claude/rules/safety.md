# Rule: 安全与防翻车

## 软件危险操作（由 .claude/hooks/guard.py 兜底）
- **deny（拦截）**：`erase_flash`/`erase-flash`、`rm -rf`、读取 `.env`/`.pem`/`.key`/`config.env`。
- **ask（确认）**：`flash`/`write_flash`、`set-target`、`fullclean`、`git push`、`pip install`、`openocd`。
- 这些只能由你显式确认后执行；harness 不自动做。

## 改动需显式说明风险 + 校验
- 改 `sdkconfig`/menuconfig 关键项 → 说明影响 + build；用 `/esp-menucheck`。
- 改 `partitions.csv`/OTA/factory/NVS → layout 校验；用 `/esp-partition`。
- 改 clock/power/flash/PSRAM → build + boot check。
- 改启动路径 → flash + boot check。
- 改并发路径 → monitor 验证。

## 硬件安全（细节在 docs/ai/SAFETY.md，连硬件前必读/必填）
- 默认 `允许 AI flash = NO`（每次 flash 当场确认）。
- 舵机/执行器：遵守限位与电流预算，禁止把舵机驱到机械极限；上电顺序按 BOARD.md。
- 危险 GPIO（strapping/上电默认电平敏感脚）在 BOARD.md 标注，改动前核对。

## 不可做
- 未说明地 fullclean / erase_flash / 覆盖用户 BOARD 配置。
- 把密钥/证书/大 bin/大日志提交进 git（.gitignore 已挡，勿强加）。
