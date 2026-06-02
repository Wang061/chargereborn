#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
.claude/hooks/session_doctor.py — SessionStart：注入项目上下文 + 轻量体检。

只做"快"的检查（文件存在性、target、BOARD 摘要），不跑 idf.py（激活慢，留给 ai-doctor.ps1）。
通过 additionalContext 把版本锁、关键规则、BOARD/SAFETY 摘要、闭环入口提示喂给会话。
异常一律静默 exit 0，绝不阻断会话启动。
"""
import sys, os, json


def read_head(path, n=18):
    try:
        with open(path, encoding="utf-8") as f:
            return "".join(f.readlines()[:n]).strip()
    except Exception:
        return None


def main():
    try:
        sys.stdin.buffer.read()  # 读掉 stdin（SessionStart JSON），不强依赖
    except Exception:
        pass

    proj = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    lines = []
    lines.append("【S3-Forge 项目级 harness 已激活】ESP-IDF 5.5.4 + ESP32-S3，从 WORKplace 启动。")
    lines.append("版本锁：IDF=5.5.4 / target=esp32s3 / FLASH_BAUD=921600(回退460800) / MONITOR_BAUD=115200。")
    lines.append("入口：构建/烧录/监视优先用 mcp__idf-bridge__* 或 /esp-* 命令；查 ESP 文档用 espressif-documentation MCP。")
    lines.append("铁律：先 build 不要 flash；flash/set-target/erase 需确认；改 sdkconfig/分区/启动路径必 build(+boot)校验。")

    # target 检查（读 sdkconfig 里的 CONFIG_IDF_TARGET，快）
    sdk = os.path.join(proj, "sdkconfig")
    tgt = None
    try:
        with open(sdk, encoding="utf-8") as f:
            for ln in f:
                if ln.startswith("CONFIG_IDF_TARGET="):
                    tgt = ln.split("=", 1)[1].strip().strip('"')
                    break
    except Exception:
        pass
    if tgt and tgt != "esp32s3":
        lines.append("⚠ 当前 sdkconfig target=%s，非 esp32s3！需 set-target esp32s3。" % tgt)
    elif tgt:
        lines.append("target 校验：esp32s3 ✓")
    else:
        lines.append("提示：尚无 sdkconfig（未 set-target）。")

    board = read_head(os.path.join(proj, "docs", "ai", "BOARD.md"))
    if board:
        lines.append("BOARD.md 摘要：\n" + board)
    else:
        lines.append("⚠ docs/ai/BOARD.md 未填——连硬件前请补板型/flash/PSRAM/引脚/危险GPIO/COM口。")

    safety = read_head(os.path.join(proj, "docs", "ai", "SAFETY.md"), 10)
    if safety:
        lines.append("SAFETY.md 摘要：\n" + safety)

    ctx = "\n".join(lines)
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "SessionStart",
        "additionalContext": ctx,
    }}, ensure_ascii=True))
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
