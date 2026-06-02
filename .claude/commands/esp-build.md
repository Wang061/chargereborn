---
description: 编译当前 ESP-IDF 工程并自动归档/分类日志；失败转 esp-build-fix。
---
用 `mcp__idf-bridge__build` 编译当前工程（**不要裸跑 idf.py**）。

- 若 `ok=true`：报告成功，并可用 `mcp__idf-bridge__size` 给出各区占用摘要。
- 若 `ok=false`：读返回的 `errors`（分类）+ `tail`，按 `esp-build-fix` 思路（或调用 esp-build-fix subagent）定位根因，**只改与错误直接相关的最小范围文件**，然后重新 build，直到绿。
- 反复失败时考虑 `/esp-loop` 自动迭代到绿。

遵守 `.claude/rules/`（版本锁 esp32s3、组件化、不引入并行构建系统）。
