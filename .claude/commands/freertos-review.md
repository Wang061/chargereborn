---
description: 审查 FreeRTOS 任务/同步/ISR 结构。
---
调用 **esp-freertos-reviewer** subagent 对当前改动或指定文件做并发审查（任务栈/优先级/核绑定、队列/mutex/事件组、死锁、ISR 边界、忙等→事件驱动）。
输出按 文件:行 的问题 + 风险等级 + 建议。配合 `freertos-task-design` skill 落实修改。
