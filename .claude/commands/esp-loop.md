---
description: ralph 迭代环——自动 build→修→build 直到编译绿（max_iterations 防跑飞）。
---
把任务自动迭代到目标达成（默认目标：`idf build` 绿）。`$ARGUMENTS` 可给目标描述与轮数。

1. 解析目标与 `max_iterations`（默认 8）。
2. **用 Bash 写哨兵文件** `.claude/.loop_active`，内容为 JSON（单行即可）：
   `{"reason":"<下一步指令，如：继续修构建错误直到 idf build 通过(绿)；完成后删除 .claude/.loop_active>","max_iterations":<N>,"count":0}`
   说明：`ralph_stop` Stop hook 会在你过早停下时按此 `reason` 强制续跑（`decision:block`），最多 N 轮，并靠 `stop_hook_active` 防无限循环。
3. 迭代：`mcp__idf-bridge__build` → 失败则按 `esp-build-fix` 改**最小范围** → 再 build …… 每轮可 `git add -A && git commit` 作快照便于回滚。
4. **达成目标后立即 `rm -f .claude/.loop_active`** 并报告——这是正常退出循环的唯一方式。
5. 达 max_iterations 仍未绿：hook 自动停，总结卡点交用户。

安全：循环内**不做 flash/erase**（仍需当场确认）；不改 target/版本。
