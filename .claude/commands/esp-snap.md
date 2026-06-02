---
description: 拍一张开发板照片并判读物理现象（LED/舵机/夹爪）。
---
给自己"眼睛"看真实硬件：

1. 经 Bash 运行 `python scripts/snap.py`（可选 `--cam N`）。它会**打印一行 JPG 路径**（失败则打印 `ERROR: ...`，需摄像头）。
2. 用 **Read 工具读取该 JPG**，直接判读画面：LED 颜色/亮灭、舵机/机械臂姿态、夹爪开合、电池/开口位置等是否符合预期。
3. 若与预期不符：结合 `/esp-monitor` 的串口日志定位（是固件逻辑还是硬件/机械问题），回到改代码 → /esp-build → /esp-flash → 再 /esp-snap 复检。

注意：拍照只读现象，物理动作仍遵守 `docs/ai/SAFETY.md`。
