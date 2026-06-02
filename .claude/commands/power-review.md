---
description: 评估功耗 / sleep / 电流预算。
---
调用 **esp-low-power-tuner** subagent：评估 light/deep sleep 选型与唤醒源、自动 light sleep+DFS、外设/时钟关断、GPIO 睡眠态、电流预算（结合 SAFETY.md 与舵机峰值/ brownout）。
改 PM/clock/sleep 属高风险：显式说明影响 + build + boot check，记 DECISIONS.md。
