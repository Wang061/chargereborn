#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
idf-bridge — 自写本地 stdio MCP，替代 ESP-IDF 5.5.4 缺失的官方 idf MCP。

把 build / size / set_target / flash / coredump / 非阻塞 monitor 暴露成 MCP 工具。
所有 idf 操作经 scripts/idf.ps1（dot-source Initialize-Idf 激活 5.5.4）。

★关键：本进程可能由 git-bash 派生（继承 MSYS 环境），直接 spawn powershell 跑 idf 会触发
  "MSys/Mingw is no longer supported" 而失败。故 spawn 前用 _clean_env() 清洗 MSYSTEM/mingw。
★stdio MCP 铁律：绝不向 stdout 打印非 JSON-RPC 内容（日志走 stderr / 文件）。

★防挂死（2026-06-16 修）：子进程输出一律重定向到“日志文件”而非管道。
  ESP-IDF build 会派生深进程树（powershell→idf.py→cmake→ninja→编译器）。旧实现用
  subprocess.run(capture_output=True) 走管道：超时只杀直接子进程(powershell)，存活的
  孙进程(ninja)继续占着 stdout/stderr 管道写端，父进程 communicate() 死等管道 EOF →
  超时也不返回，永久挂死。改为：输出落文件(无管道) + 超时用 taskkill /T 杀整棵进程树。
  另：build/flash 提供非阻塞 *_start/*_read（仿 monitor），长任务不阻塞 MCP 调用。

自测（不连 MCP）：  python scripts/idf_bridge_mcp.py --selftest [idf args...]
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


def _ps_cmd(idf_args):
    return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", IDF_PS1] + list(idf_args)


def _kill_tree(pid):
    """Windows: 杀掉 pid 及其整棵子进程树(ninja/cmake/编译器)，避免孤儿占用与管道悬挂。"""
    try:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    except Exception:
        try:
            os.kill(pid, 9)
        except Exception:
            pass


def _new_logpath(kind):
    d = os.path.join(LOGDIR, kind)
    os.makedirs(d, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    return os.path.join(d, ts + ".log")


def _read_text(path, tail_bytes=0):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            t = f.read()
        return t[-tail_bytes:] if tail_bytes else t
    except Exception:
        return ""


def _run_ps(idf_args, timeout=600, kind="_run"):
    """经 idf.ps1 在干净环境跑一条 idf 命令（同步，但防挂死）。返回 dict(ok, rc, out, err, log_path)。

    输出落文件而非管道 → 不会因孙进程占着 stdout 管道而 communicate() 死等 EOF。
    超时 → taskkill /T 杀整棵进程树后返回 rc=-2。
    """
    if not os.path.isfile(IDF_PS1):
        return {"ok": False, "rc": -1, "out": "", "err": "idf.ps1 not found: %s" % IDF_PS1, "log_path": ""}
    log = _new_logpath(kind)
    try:
        fh = open(log, "w", encoding="utf-8", errors="replace")
    except Exception as e:
        return {"ok": False, "rc": -4, "out": "", "err": "open log fail: %r" % e, "log_path": ""}
    timed_out = False
    rc = None
    try:
        p = subprocess.Popen(_ps_cmd(idf_args), cwd=PROJ, env=_clean_env(),
                             stdout=fh, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    except Exception as e:
        try:
            fh.close()
        except Exception:
            pass
        return {"ok": False, "rc": -3, "out": "", "err": "spawn error: %r" % e, "log_path": log}
    try:
        rc = p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        _kill_tree(p.pid)
        try:
            rc = p.wait(timeout=15)
        except Exception:
            rc = -2
    finally:
        try:
            fh.close()
        except Exception:
            pass
    out = _read_text(log)
    if timed_out:
        out += "\n[idf-bridge] TIMEOUT after %ss -> killed process tree\n" % timeout
        rc = -2
    return {"ok": (rc == 0), "rc": (rc if rc is not None else -2), "out": out, "err": "", "log_path": log}


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

    # ---- 非阻塞后台任务(build / flash)：仿 monitor，输出落 logs/<kind>/<ts>.log，轮询读取 ----
    _job = {"proc": None, "log": None, "kind": None, "fh": None}

    def _job_start(kind, idf_args):
        if _job["proc"] and _job["proc"].poll() is None:
            return {"ok": False, "msg": "%s 任务进行中: %s（先 %s_read 轮询或等其结束）"
                    % (_job["kind"], _job["log"], _job["kind"])}
        if not os.path.isfile(IDF_PS1):
            return {"ok": False, "msg": "idf.ps1 not found: %s" % IDF_PS1}
        log = _new_logpath(kind)
        try:
            fh = open(log, "w", encoding="utf-8", errors="replace")
            proc = subprocess.Popen(_ps_cmd(idf_args), cwd=PROJ, env=_clean_env(),
                                    stdout=fh, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        except Exception as e:
            return {"ok": False, "msg": "启动失败: %r" % e}
        _job.update(proc=proc, log=log, kind=kind, fh=fh)
        return {"ok": True, "kind": kind, "running": True, "log_path": log,
                "hint": "用 %s_read 轮询进度/结果（不阻塞、不挂死）" % kind}

    def _job_snapshot(kind, lines):
        if _job["kind"] != kind or not _job["log"]:
            return {"ok": False, "msg": "无 %s 任务（先 %s_start 或 %s）" % (kind, kind, kind)}
        proc = _job["proc"]
        running = bool(proc and proc.poll() is None)
        rc = (None if running else (proc.poll() if proc else None))
        try:
            with open(_job["log"], encoding="utf-8", errors="replace") as f:
                tail = f.readlines()[-int(lines):]
        except Exception as e:
            return {"ok": False, "msg": "读日志失败: %r" % e, "log_path": _job["log"]}
        text = "".join(tail)
        res = {"ok": True, "kind": kind, "running": running, "log_path": _job["log"], "tail": text}
        if not running:
            res["rc"] = rc
            res["done_ok"] = (rc == 0)
            if rc != 0:
                res["errors"] = _classify(text)
        return res

    def _job_run(kind, idf_args, wait_s):
        """启动后台任务并轮询至多 wait_s 秒：完成→返回最终结果；未完→running=true + 提示用 *_read。"""
        st = _job_start(kind, idf_args)
        if not st.get("ok"):
            return st
        deadline = time.time() + max(0, int(wait_s))
        while time.time() < deadline:
            if _job["proc"].poll() is not None:
                break
            time.sleep(1)
        snap = _job_snapshot(kind, 120)
        if snap.get("running"):
            snap["hint"] = "build/flash 仍在进行；用 %s_read 继续轮询（不阻塞）" % kind
        else:
            # 完成：归档完整日志，附错误分类
            full = _read_text(_job["log"])
            snap["log_path"] = _archive(kind, full, "idf.py " + " ".join(idf_args))
            snap["tail"] = full[-2500:]
            if not snap.get("done_ok"):
                snap["errors"] = _classify(full)
        return snap

    @mcp.tool()
    def build(wait: int = 45) -> dict:
        """编译当前 ESP-IDF 工程（idf.py build，后台跑+轮询，绝不挂死）。
        wait 秒内完成→返回 ok/rc/errors/tail/log_path；未完成→running=true，用 build_read 继续轮询。
        首次全量(拉组件)慢→建议直接用 build_start + build_read。"""
        r = _job_run("build", ["build"], wait)
        if "done_ok" in r:
            r["ok"] = r["done_ok"]
        return r

    @mcp.tool()
    def build_start() -> dict:
        """[非阻塞] 后台启动 idf.py build，立即返回 log_path；用 build_read 轮询进度与结果。长/首次构建首选。"""
        return _job_start("build", ["build"])

    @mcp.tool()
    def build_read(lines: int = 400) -> dict:
        """读取后台 build 的日志尾 + 是否结束 + rc/错误分类（轮询，不挂死）。"""
        return _job_snapshot("build", lines)

    @mcp.tool()
    def size() -> dict:
        """查看固件各区大小（idf.py size）。"""
        r = _run_ps(["size"], timeout=300, kind="size")
        return {"ok": r["ok"], "rc": r["rc"], "out": r["out"][-3000:]}

    @mcp.tool()
    def set_target(chip: str = "esp32s3") -> dict:
        """设置芯片 target（默认 esp32s3）。会改 sdkconfig，需 ask 确认。"""
        if chip != "esp32s3":
            return {"ok": False, "rc": -9, "out": "version-lock: 仅允许 esp32s3，拒绝 %s" % chip}
        r = _run_ps(["set-target", "esp32s3"], timeout=600, kind="set_target")
        return {"ok": r["ok"], "rc": r["rc"], "out": r["out"][-2000:]}

    @mcp.tool()
    def flash(port: str = "", wait: int = 120) -> dict:
        """烧录到硬件（idf.py flash，后台跑+轮询，不挂死）。port 省略则自动探测。需 ask 确认。"""
        args = ["-p", port, "flash"] if port else ["flash"]
        r = _job_run("flash", args, wait)
        if "done_ok" in r:
            r["ok"] = r["done_ok"]
        return r

    @mcp.tool()
    def flash_start(port: str = "") -> dict:
        """[非阻塞] 后台启动 idf.py flash（需 ask 确认）；用 flash_read 轮询。port 空则自动探测。"""
        args = ["-p", port, "flash"] if port else ["flash"]
        return _job_start("flash", args)

    @mcp.tool()
    def flash_read(lines: int = 300) -> dict:
        """读取后台 flash 的日志尾 + 是否结束 + rc（轮询，不挂死）。"""
        return _job_snapshot("flash", lines)

    @mcp.tool()
    def coredump_summary() -> dict:
        """读取并解析 flash core dump（idf.py coredump-info：任务/寄存器/原因）。"""
        r = _run_ps(["coredump-info"], timeout=300, kind="coredump")
        return {"ok": r["ok"], "rc": r["rc"], "out": r["out"][-4000:]}

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
        res = _run_ps(rest, timeout=900, kind="selftest")
        # 自测：打印 JSON 到 stdout（ensure_ascii 防 GBK 撞车）
        print(json.dumps({"args": rest, "ok": res["ok"], "rc": res["rc"],
                          "out_tail": (res["out"] or "")[-600:],
                          "log_path": res.get("log_path", ""),
                          "errors": _classify(res["out"] + res["err"]),
                          "clean_env_has_MSYSTEM": "MSYSTEM" in _clean_env()},
                         ensure_ascii=True))
        sys.exit(0 if res["ok"] else 1)
    else:
        _build_server().run()
