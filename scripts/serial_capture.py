#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serial_capture.py — 带超时/可被杀的串口捕获，落盘到 --out（行刷新，绝不挂死调用方）。
由 idf-bridge 的 monitor_start 作为后台子进程拉起；也可独立用。
  python scripts/serial_capture.py --port COM5 --baud 115200 --out logs/monitor/x.log [--seconds 8]
seconds=0 表示持续到被 terminate。
"""
import sys, os, time, argparse


def _err(out, msg):
    try:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w", encoding="utf-8") as f:
            f.write("[serial_capture error] " + msg + "\n")
    except Exception:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=int, default=0)
    a = ap.parse_args()

    try:
        import serial
    except Exception as e:
        _err(a.out, "pyserial missing: %r" % e)
        return 1
    try:
        ser = serial.Serial(a.port, a.baud, timeout=1)
    except Exception as e:
        _err(a.out, "open %s @ %d failed: %r" % (a.port, a.baud, e))
        return 2

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    end = (time.time() + a.seconds) if a.seconds > 0 else None
    with open(a.out, "w", encoding="utf-8", errors="replace") as f:
        f.write("# serial capture %s @ %d  (%s)\n" %
                (a.port, a.baud, time.strftime("%Y-%m-%d %H:%M:%S")))
        f.flush()
        try:
            while True:
                if end and time.time() >= end:
                    break
                data = ser.readline()
                if data:
                    f.write(data.decode("utf-8", "replace"))
                    f.flush()
        except KeyboardInterrupt:
            pass
        finally:
            try:
                ser.close()
            except Exception:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
