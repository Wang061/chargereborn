#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
.claude/hooks/guard.py — PreToolUse 安全闸门（挂在 Bash 与 PowerShell 两个 matcher 上）

读 stdin 的 PreToolUse JSON，对危险命令返回 deny / ask；其余 exit 0 不决策（沉默≠放行，
走正常权限流）。任何异常都 exit 0，绝不因 hook 自身故障而误拦正常命令。

已实测前提（见 ref-windows-esp-env-gotchas）：
  - hook 由 git-bash 调用，本脚本是 win32 原生 python（venv）。
  - stdin 是 UTF-8 JSON，含 tool_name / tool_input.command。
  - 用 sys.stdin.buffer 读 + ensure_ascii=True 输出，规避 Windows 控制台 GBK 编码坑。
"""
import sys, json, re


def emit(decision, reason):
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": decision,          # allow | deny | ask
        "permissionDecisionReason": reason,
    }}, ensure_ascii=True))
    sys.exit(0)


# —— DENY：不可逆破坏 / 泄密 ——
DENY = [
    (r"erase[_-]flash",
     "擦除 flash 不可逆，已拦截。确需请你手动执行。"),
    (r"\brm\s+-rf\b",
     "rm -rf 递归强删已拦截。请用更精确的删除或确认后手动执行。"),
    (r"(?:\bcat\b|\btype\b|\bgc\b|Get-Content)[^\n]*\.(?:env|pem|key)\b",
     "读取密钥/凭据文件（.env/.pem/.key）已拦截。"),
    (r"(?:\bcat\b|\btype\b|\bgc\b|Get-Content)[^\n]*config\.env",
     "读取 config.env（构建环境，可能含路径/令牌）已拦截。"),
]

# —— ASK：有风险但合法，需当场确认 ——
ASK = [
    (r"\b(?:idf\.py|idf\.ps1|idf)\b[^\n]*\bflash\b",
     "即将烧录到硬件，请确认目标板与串口。"),
    (r"\bwrite_flash\b",
     "esptool 烧录到硬件，请确认。"),
    (r"\bset[-_]target\b",
     "切换芯片 target，可能改写 sdkconfig，请确认仍锁定 esp32s3。"),
    (r"\bfullclean\b",
     "fullclean 会清空 build 目录，请确认。"),
    (r"\bgit\s+push\b",
     "推送到远端，请确认。"),
    (r"\bpip\s+install\b",
     "安装 python 包，请确认（注意 socks 代理需清+镜像）。"),
    (r"\bopenocd\b",
     "启动 OpenOCD/JTAG，请确认硬件已连接。"),
]


def main():
    try:
        raw = sys.stdin.buffer.read().decode("utf-8", "replace")
        data = json.loads(raw)
    except Exception:
        sys.exit(0)  # 解析失败 → 不决策

    ti = data.get("tool_input") or {}
    cmd = ti.get("command") if isinstance(ti, dict) else ""
    cmd = cmd or ""
    if not cmd:
        sys.exit(0)

    for pat, why in DENY:
        if re.search(pat, cmd, re.IGNORECASE):
            emit("deny", why)
    for pat, why in ASK:
        if re.search(pat, cmd, re.IGNORECASE):
            emit("ask", why)

    sys.exit(0)  # 无匹配 → 不决策


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)  # 兜底：永不因故障误拦
