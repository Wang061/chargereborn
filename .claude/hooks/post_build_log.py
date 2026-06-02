#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
.claude/hooks/post_build_log.py — PostToolUse 兜底归档/分类（matcher: Bash + mcp__idf-bridge__.*）。

主归档由 idf-bridge 内部完成；本 hook 仅在 Bash 直调 build/flash 时兜底：把输出落 logs/，
并对 build 错误做粗分类，通过 additionalContext 提示。任何异常静默 exit 0。
"""
import sys, os, json, re, datetime

BUILD_ERR = [
    (r"undefined reference",                              "未定义符号(链接)"),
    (r"fatal error:.*\.h|No such file or directory",     "include/头文件缺失"),
    (r"region `?\w+'? overflowed|overflowed by",         "链接区溢出(flash/iram 超)"),
    (r"does not match the target|Cannot find target",    "target 不符"),
    (r"Failed to resolve component|component .* not found", "组件未找到"),
    (r"CMake Error",                                      "CMake 错误"),
    (r"Kconfig|menuconfig",                               "Kconfig 相关"),
]


def main():
    try:
        data = json.loads(sys.stdin.buffer.read().decode("utf-8", "replace"))
    except Exception:
        sys.exit(0)

    proj = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    tool = data.get("tool_name", "") or ""
    ti = data.get("tool_input") or {}
    cmd = (ti.get("command") if isinstance(ti, dict) else "") or ""

    kind = None
    if "idf-bridge__build" in tool or re.search(r"\bbuild\b", cmd, re.I):
        kind = "build"
    elif "idf-bridge__flash" in tool or re.search(r"\bflash\b", cmd, re.I):
        kind = "flash"
    if not kind:
        sys.exit(0)

    resp = data.get("tool_response")
    if isinstance(resp, (dict, list)):
        resp = json.dumps(resp, ensure_ascii=False, indent=2)
    resp = str(resp or "")

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    d = os.path.join(proj, "logs", kind)
    try:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, ts + ".log"), "w", encoding="utf-8") as f:
            f.write("# tool: %s\n# cmd: %s\n\n%s\n" % (tool, cmd, resp))
    except Exception:
        pass

    if kind == "build":
        hits = [lab for pat, lab in BUILD_ERR if re.search(pat, resp, re.I)]
        if hits:
            print(json.dumps({"hookSpecificOutput": {
                "hookEventName": "PostToolUse",
                "additionalContext": "构建日志已归档 logs/build/%s.log；疑似错误：%s。建议交 esp-build-fix。"
                                     % (ts, "、".join(hits)),
            }}, ensure_ascii=True))
    sys.exit(0)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
