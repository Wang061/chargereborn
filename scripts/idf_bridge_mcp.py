#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
idf-bridge — 自写本地 stdio MCP，替代 ESP-IDF 5.5.4 缺失的官方 idf MCP。

把 build / size / set_target / flash / coredump / 非阻塞 monitor 暴露成 MCP 工具。
所有 idf 操作经 scripts/idf.ps1（dot-source Initialize-Idf 激活 5.5.4）。

★关键：本进程可能由 git-bash 派生（继承 MSYS 环境），直接 spawn powershell 跑 idf 会触发
  "MSys/Mingw is no longer supported" 而失败。故 spawn 前用 _clean_env() 清洗 MSYSTEM/mingw。
★stdio MCP 铁律：绝不向 stdout 打印非 JSON-RPC 内容（日志走 stderr / 文件）。

自测（不连 MCP）：  python scripts/idf_bridge_mcp.py --selftest build
正常运行（MCP）：   python scripts/idf_bridge_mcp.py
"""
import os, sys, re, json, time, subprocess, datetime

# ---- 项目根 / 路径 ----
_HERE = os.path.dirname(os.path.abspath(__file__))           # .../WORKplace/scripts
_DEF_PROJ = os.path.dirname(_HERE)                            # .../WORKplace
PROJ = os.environ.get("CLAUDE_PROJECT_DIR") or _DEF_PROJ
if not os.path.isdir(PROJ):
    PROJ = _DEF_PROJ
IDF_PS1 = os.path.join(PROJ, "scripts", "idf.ps1")
LOGDIR = os.path.join(PROJ, "logs")

# MSYS 标记 + PATH 中 mingw/msys 段，spawn powershell 前清掉
_MSYS_KEYS = ["MSYSTEM", "MSYS", "MINGW_PREFIX", "MSYSTEM_PREFIX", "MSYSTEM_CARCH",
              "MSYSTEM_CHOST", "MINGW_CHOST", "MINGW_PACKAGE_PREFIX"]
_MSYS_PATH_RE = re.compile(r"(mingw32|mingw64)|((^|[\\/])usr[\\/]bin)|([\\/]msys)", re.I)


def _clean_env():
    """复制 os.environ，移除 MSYS 标记并从 PATH 剔除 mingw/msys/git-usr 段。"""
    env = dict(os.environ)
    for k in _MSYS_KEYS:
        env.pop(k, None)
    path = env.get("PATH") or env.get("Path") or ""
    parts = path.split(";")
    keep = [p for p in parts if p and not _MSYS_PATH_RE.search(p)]
    # 兜底保证 System32 在内（powershell 自身）
    sysroot = env.get("SystemRoot", r"C:\Windows")
    for d in [os.path.join(sysroot, "System32"),
              os.path.join(sysroot, "System32", "WindowsPowerShell", "v1.0")]:
        if d not in keep:
            keep.append(d)
    env["PATH"] = ";".join(keep)
    env.pop("Path", None)
    return env


def _run_ps(idf_args, timeout=600):
    """经 idf.ps1 在干净环境跑一条 idf 命令。返回 dict(ok, rc, out, err)。"""
    if not os.path.isfile(IDF_PS1):
        return {"ok": False, "rc": -1, "out": "", "err": "idf.ps1 not found: %s" % IDF_PS1}
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", IDF_PS1] + list(idf_args)
    try:
        p = subprocess.run(cmd, cwd=PROJ, env=_clean_env(),
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=timeout)
        return {"ok": p.returncode == 0, "rc": p.returncode, "out": p.stdout or "", "err": p.stderr or ""}
    except subprocess.TimeoutExpired:
        return {"ok": False, "rc": -2, "out": "", "err": "timeout after %ss" % timeout}
    except Exception as e:
        return {"ok": False, "rc": -3, "out": "", "err": "spawn error: %r" % e}


_BUILD_ERR = [
    (r"undefined reference", "undefined-symbol(link)"),
    (r"fatal error:.*\.h|No such file or directory", "missing-include"),
    (r"region `?\w+'? overflowed|overflowed by", "region-overflow"),
    (r"does not match the target|Cannot find target", "target-mismatch"),
    (r"Failed to resolve component|component .* not found", "component-not-found"),
    (r"CMake Error", "cmake-error"),
    (r"Kconfig|menuconfig", "kconfig"),
]


def _classify(text):
    return [lab for pat, lab in _BUILD_ERR if re.search(pat, text, re.I)]


def _archive(kind, text, cmd):
    try:
        d = os.path.join(LOGDIR, kind)
        os.makedirs(d, exist_ok=True)
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        path = os.path.join(d, ts + ".log")
        with open(path, "w", encoding="utf-8") as f:
            f.write("# cmd: %s\n\n%s\n" % (cmd, text))
        return path
    except Exception:
        return ""


def _detect_port():
    try:
        from serial.tools import list_ports
        cands = []
        for p in list_ports.comports():
            vid = getattr(p, "vid", None)
            # 0x303A=Espressif, 0x10C4=CP210x, 0x1A86=CH340, 0x0403=FTDI
            if vid in (0x303A, 0x10C4, 0x1A86, 0x0403):
                cands.append(p.device)
        if not cands:
            cands = [p.device for p in list_ports.comports()]
        return cands[0] if cands else None
    except Exception:
        return None


# ================= MCP server =================
def _build_server():
    from mcp.server.fastmcp import FastMCP
    mcp = FastMCP("idf-bridge")

    @mcp.tool()
    def build() -> dict:
        """编译当前 ESP-IDF 工程（idf.py build）。返回 ok/rc/错误分类/日志尾/归档路径。"""
        r = _run_ps(["build"], timeout=900)
        combined = (r["out"] + "\n" + r["err"]).strip()
        log = _archive("build", combined, "idf.py build")
        return {"ok": r["ok"], "rc": r["rc"],
                "errors": (_classify(combined) if not r["ok"] else []),
                "tail": combined[-2500:], "log_path": log}

    @mcp.tool()
    def size() -> dict:
        """查看固件各区大小（idf.py size）。"""
        r = _run_ps(["size"], timeout=300)
        return {"ok": r["ok"], "rc": r["rc"], "out": (r["out"] + r["err"])[-3000:]}

    @mcp.tool()
    def set_target(chip: str = "esp32s3") -> dict:
        """设置芯片 target（默认 esp32s3）。会改 sdkconfig，需 ask 确认。"""
        if chip != "esp32s3":
            return {"ok": False, "rc": -9, "out": "version-lock: 仅允许 esp32s3，拒绝 %s" % chip}
        r = _run_ps(["set-target", "esp32s3"], timeout=600)
        return {"ok": r["ok"], "rc": r["rc"], "out": (r["out"] + r["err"])[-2000:]}

    @mcp.tool()
    def flash(port: str = "") -> dict:
        """烧录到硬件（idf.py flash）。port 省略则自动探测。需 ask 确认。"""
        args = ["-p", port, "flash"] if port else ["flash"]
        r = _run_ps(args, timeout=600)
        combined = (r["out"] + "\n" + r["err"]).strip()
        log = _archive("flash", combined, "idf.py flash")
        return {"ok": r["ok"], "rc": r["rc"], "tail": combined[-2000:], "log_path": log}

    @mcp.tool()
    def coredump_summary() -> dict:
        """读取并解析 flash core dump（idf.py coredump-info：任务/寄存器/原因）。"""
        r = _run_ps(["coredump-info"], timeout=300)
        return {"ok": r["ok"], "rc": r["rc"], "out": (r["out"] + r["err"])[-4000:]}

    # ---- 非阻塞 monitor（后台 serial_capture.py 子进程）----
    _mon = {"proc": None, "log": None, "port": None}

    @mcp.tool()
    def monitor_start(port: str = "", seconds: int = 0) -> dict:
        """启动非阻塞串口监视：后台把串口落 logs/monitor/<ts>.log。seconds=0 表示持续到 monitor_stop。需 ask。"""
        if _mon["proc"] and _mon["proc"].poll() is None:
            return {"ok": False, "msg": "monitor 已在运行: %s" % _mon["log"]}
        p = port or _detect_port()
        if not p:
            return {"ok": False, "msg": "未找到串口（硬件未连？请在 PORTS.md 指定或传 port）"}
        os.makedirs(os.path.join(LOGDIR, "monitor"), exist_ok=True)
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log = os.path.join(LOGDIR, "monitor", ts + ".log")
        cap = os.path.join(PROJ, "scripts", "serial_capture.py")
        args = [sys.executable, cap, "--port", p, "--baud", "115200", "--out", log]
        if seconds and seconds > 0:
            args += ["--seconds", str(seconds)]
        try:
            proc = subprocess.Popen(args, cwd=PROJ, env=_clean_env(),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as e:
            return {"ok": False, "msg": "启动失败: %r" % e}
        _mon.update(proc=proc, log=log, port=p)
        return {"ok": True, "port": p, "log_path": log}

    @mcp.tool()
    def monitor_read(lines: int = 200) -> dict:
        """读取后台监视缓冲的最后 N 行（轮询，不挂死）。"""
        log = _mon["log"]
        if not log or not os.path.isfile(log):
            return {"ok": False, "msg": "无监视日志（先 monitor_start）"}
        try:
            with open(log, encoding="utf-8", errors="replace") as f:
                tail = f.readlines()[-int(lines):]
            running = bool(_mon["proc"] and _mon["proc"].poll() is None)
            return {"ok": True, "running": running, "port": _mon["port"], "text": "".join(tail)}
        except Exception as e:
            return {"ok": False, "msg": "%r" % e}

    @mcp.tool()
    def monitor_stop() -> dict:
        """停止后台串口监视。"""
        proc = _mon["proc"]
        if proc and proc.poll() is None:
            try:
                proc.terminate()
            except Exception:
                pass
        log = _mon["log"]
        _mon.update(proc=None)
        return {"ok": True, "log_path": log}

    return mcp


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        rest = [a for a in sys.argv[1:] if a != "--selftest"] or ["--version"]
        res = _run_ps(rest, timeout=900)
        # 自测：打印到 stderr（不污染将来 MCP 的 stdout 习惯），这里直接 stdout 也无妨
        print(json.dumps({"args": rest, "ok": res["ok"], "rc": res["rc"],
                          "out_tail": (res["out"] or "")[-600:],
                          "err_tail": (res["err"] or "")[-400:],
                          "errors": _classify(res["out"] + res["err"]),
                          "clean_env_has_MSYSTEM": "MSYSTEM" in _clean_env()},
                         ensure_ascii=True))
        sys.exit(0 if res["ok"] else 1)
    else:
        _build_server().run()
