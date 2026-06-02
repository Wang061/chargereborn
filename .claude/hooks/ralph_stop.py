#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
.claude/hooks/ralph_stop.py — Stop：/esp-loop 的 ralph 迭代控制器。

仅当存在 .claude/.loop_active（由 /esp-loop 写，JSON: {reason, max_iterations, count}）、
未命中 stop_hook_active、未达上限时，输出 top-level {"decision":"block","reason":...} 强制续跑
（reason 作为下一步指令喂回模型）；否则正常停止。

三重防跑飞：
  1) stop_hook_active=true 直接放行（官方防无限循环关键）；
  2) max_iterations 上限；
  3) 任务方达成目标时由 /esp-loop 指示删除 .loop_active。
契约经 claude-code-guide 核实：续跑用 TOP-LEVEL decision/reason，不是 hookSpecificOutput。
"""
import sys, os, json


def main():
    try:
        data = json.loads(sys.stdin.buffer.read().decode("utf-8", "replace"))
    except Exception:
        sys.exit(0)

    if data.get("stop_hook_active"):
        sys.exit(0)  # 已由本 hook 续跑过一轮 → 放行，杜绝无限循环

    proj = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    flag = os.path.join(proj, ".claude", ".loop_active")
    if not os.path.exists(flag):
        sys.exit(0)  # 非循环模式 → 正常停

    try:
        with open(flag, encoding="utf-8") as f:
            st = json.load(f)
    except Exception:
        _rm(flag)
        sys.exit(0)

    count = int(st.get("count", 0)) + 1
    mx = int(st.get("max_iterations", 5))
    reason = st.get("reason") or ("继续推进当前任务，直到 idf build 通过(绿)；"
                                  "完成后删除 .claude/.loop_active 文件。")

    if count >= mx:
        _rm(flag)
        sys.exit(0)  # 达上限 → 停

    st["count"] = count
    try:
        with open(flag, "w", encoding="utf-8") as f:
            json.dump(st, f, ensure_ascii=False)
    except Exception:
        pass

    print(json.dumps({"decision": "block",
                      "reason": "[ralph %d/%d] %s" % (count, mx, reason)},
                     ensure_ascii=True))
    sys.exit(0)


def _rm(p):
    try:
        os.remove(p)
    except Exception:
        pass


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.exit(0)
