"""calib_serial.py — 无 WiFi 标定采样: 从 COM11 console 日志解析检测框, 出平均像素中心。
main 每帧打印 `#0 18650 0.32 [x1,y1,x2,y2]`(与 /detect JSON 同一坐标系), 电脑不用连热点。
用法: python scripts/calib_serial.py --port COM11 --seconds 5
输出: 平均中心/抖动/帧数/平均分; 抖动>6px 或 帧数<5 会告警(别用该样本)。ASCII-only。"""
import re, time, argparse

BOX_RE = re.compile(r"#\d+ \S+ ([0-9.]+) \[(\d+),(\d+),(\d+),(\d+)\]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM11")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=5.0)
    a = ap.parse_args()

    import serial
    ser = serial.Serial(a.port, a.baud, timeout=0.5)
    ser.reset_input_buffer()
    end = time.time() + a.seconds
    xs, ys, ss = [], [], []
    clipped = 0
    boxes = []
    while time.time() < end:
        line = ser.readline().decode("utf-8", "replace")
        m = BOX_RE.search(line)
        if not m:
            continue
        s = float(m.group(1))
        x1, y1, x2, y2 = (int(m.group(i)) for i in range(2, 6))
        boxes.append((x1, y1, x2, y2, s))
        if x1 <= 1 or y1 <= 1:   # 框贴图像边 = 被裁切, 中心有偏, 不可用
            clipped += 1
            continue
        xs.append((x1 + x2) / 2.0)
        ys.append((y1 + y2) / 2.0)
        ss.append(s)
    ser.close()

    for b in boxes[:12]:
        print("  box [%d,%d,%d,%d] s=%.2f%s" % (b[0], b[1], b[2], b[3], b[4],
              "  CLIPPED" if (b[0] <= 1 or b[1] <= 1) else ""))
    n = len(xs)
    if n == 0:
        if clipped:
            print("RESULT n=0 (all %d detections CLIPPED at frame edge - point outside usable view)" % clipped)
        else:
            print("RESULT n=0  (no detection - check lighting/placement)")
        return 1
    cx, cy = sum(xs) / n, sum(ys) / n
    jx, jy = max(xs) - min(xs), max(ys) - min(ys)
    print("RESULT n=%d center=(%.1f,%.1f) jitter=(%.1f,%.1f)px score_avg=%.2f clipped=%d"
          % (n, cx, cy, jx, jy, sum(ss) / n, clipped))
    if n < 5:
        print("WARN: <5 frames, weak sample")
    if jx > 6 or jy > 6:
        print("WARN: jitter >6px, battery/scene unstable - resample")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
