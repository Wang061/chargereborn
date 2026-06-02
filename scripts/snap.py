#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
snap.py — 给 AI "眼睛"：抓一帧摄像头存 JPG 并打印路径（Claude Code 原生 Read 该图判读）。
零常驻进程，不可崩。Windows 用 DSHOW 后端。
  python scripts/snap.py [--cam 0] [--out logs/vision/snap-xxx.jpg]
仅向 stdout 打印 ASCII（成功=路径，失败=ERROR: ...），规避 Windows 控制台编码坑。
"""
import sys, os, time, argparse


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cam", type=int, default=0)
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    try:
        import cv2
    except Exception as e:
        print("ERROR: opencv missing: %r" % e)
        return 1

    proj = os.environ.get("CLAUDE_PROJECT_DIR") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(proj, "logs", "vision", "snap-%s.jpg" % time.strftime("%Y%m%d_%H%M%S"))
    try:
        os.makedirs(os.path.dirname(out), exist_ok=True)
    except Exception:
        pass

    cap = cv2.VideoCapture(a.cam, cv2.CAP_DSHOW)
    if not cap.isOpened():
        print("ERROR: camera %d not opened (no webcam / busy)" % a.cam)
        return 2
    frame = None
    for _ in range(6):           # warm up so exposure settles
        ok, frame = cap.read()
        time.sleep(0.05)
    cap.release()
    if frame is None:
        print("ERROR: no frame captured")
        return 3
    try:
        cv2.imwrite(out, frame)
    except Exception as e:
        print("ERROR: imwrite failed: %r" % e)
        return 4
    print(out)                   # path for Claude to Read
    return 0


if __name__ == "__main__":
    sys.exit(main())
