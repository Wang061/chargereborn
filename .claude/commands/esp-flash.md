---
description: 烧录固件到 ESP32-S3（需当场确认；先确保 build 为绿）。
---
1. 先确认最近一次 `mcp__idf-bridge__build` 为绿（否则先 /esp-build）。
2. **烧录前安全确认**：提示用户确认目标板/端口安全、执行器是否会立即动作（见 `docs/ai/SAFETY.md`：默认抬空/限速/不接动力电先验）。
3. 用 `mcp__idf-bridge__flash`（`port` 省略则自动探测；该工具为 **ask**，会请用户确认）。
4. 烧录成功后建议 `/esp-monitor` 看启动日志确认无 boot loop。

不要用 erase（被 deny）；下载失败可让用户降波特率（921600→460800）。
