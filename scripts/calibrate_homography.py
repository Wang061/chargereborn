"""单应性标定: 在台面已知mm位置摆真电池(平躺、不垫高、轴心对准标记点),
逐点从板子 /detect 读像素中心, 求 H(px->mm), POST 到 /arm_calib 写入 NVS。
原理: 标定面必须与运行时被测特征(电池轴心)同高度——平躺电池轴心天然在 z=直径/2,
与抓取时同一平面, 视差自动抵消; 垫高反而把标定面抬到 2x 半径, 引入系统偏移。
(只有用"贴台面的平面标记"替代电池时才需要垫高半径。)
用法: 交互式, 按提示在每个已知点放一颗电池, 回车采集。
依赖: pip install opencv-python numpy requests (在 .venv-tools 里)。"""
import sys, time, json
import numpy as np
import requests

BASE = "http://192.168.4.1"
# 台面已知点(mm), 与实际摆放一致; >=6 点最小二乘更稳。按你工作台改。
# G1 定稿(2026-07-04): 观察位=OpenMV Home (0,100,70) alpha=-77 俯视;
# 8 点带 y=150..210 在视野内, 全部 IK 可达(z=0 验证)。旧 y=80 排废弃(近距有 10mm 误差)。
KNOWN_MM = [
    (-50, 150), (0, 150), (50, 150),
    (-50, 210), (0, 210), (50, 210),
    (-30, 180), (30, 180),
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
        input(f"把电池平放到台面 ({mx},{my})mm 处(不垫高, 轴心对准标记点), 回车采集...")
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
