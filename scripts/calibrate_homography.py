"""单应性标定: 在台面已知mm位置摆标定点(垫高9mm到18650中轴面),
逐点从板子 /detect 读像素中心, 求 H(px->mm), POST 到 /arm_calib 写入 NVS。
用法: 交互式, 按提示在每个已知点放一颗电池, 回车采集。
依赖: pip install opencv-python numpy requests (在 .venv-tools 里)。"""
import sys, time, json
import numpy as np
import requests

BASE = "http://192.168.4.1"
# 台面已知点(mm), 与实际摆放一致; >=6 点最小二乘更稳。按你工作台改。
KNOWN_MM = [
    (-60, 80), (0, 80), (60, 80),
    (-60, 130), (0, 130), (60, 130),
    (-40, 105), (40, 105),
]

def read_center():
    r = requests.get(BASE + "/detect", timeout=3).json()
    if r.get("n", 0) < 1:
        return None
    b = max(r["boxes"], key=lambda x: x["s"])  # 最高分框
    return ((b["x1"] + b["x2"]) / 2.0, (b["y1"] + b["y2"]) / 2.0)

def main():
    px_pts, mm_pts = [], []
    for (mx, my) in KNOWN_MM:
        input(f"把电池放到台面 ({mx},{my})mm 处(垫高9mm), 回车采集...")
        c = None
        for _ in range(10):
            c = read_center()
            if c: break
            time.sleep(0.2)
        if not c:
            print("  未检测到, 跳过该点"); continue
        print(f"  像素中心 {c}")
        px_pts.append(c); mm_pts.append((mx, my))
    if len(px_pts) < 4:
        print("有效点 < 4, 无法求单应性"); sys.exit(1)
    import cv2
    H, _ = cv2.findHomography(np.array(px_pts, np.float32), np.array(mm_pts, np.float32))
    Hs = ",".join("%.8f" % v for v in H.flatten())
    print("H =", Hs)
    # 残差自检
    for (px, mm) in zip(px_pts, mm_pts):
        v = H @ np.array([px[0], px[1], 1.0])
        est = (v[0]/v[2], v[1]/v[2])
        print(f"  {mm} -> est({est[0]:.1f},{est[1]:.1f}) err {((est[0]-mm[0])**2+(est[1]-mm[1])**2)**0.5:.1f}mm")
    if input("写入板子 NVS? (y/N) ").lower() == "y":
        resp = requests.post(BASE + "/arm_calib", data=Hs, timeout=3)
        print("saved:", resp.text)

if __name__ == "__main__":
    main()
