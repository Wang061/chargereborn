"""km1_send.py — G1 用: 向 KM1 (COM 直连) 发裸协议帧并抓回显。
KM1 会把收到的字节原样回显(Phase 1 COM4 实测), 回显 = 等效逻辑分析仪:
证明的是"KM1 收到并进了解析链", 不只是"我们发出去了"。
用法(在 .venv-tools 里, pyserial 已装):
  python scripts/km1_send.py --port COM6 --probe                 # 无动作探针($KMS:回显不派发)
  python scripts/km1_send.py --port COM6 "{#000P1500T1000!}"     # 发一帧, 抓 2s 回显
  python scripts/km1_send.py --port COM6 f1 f2 --gap 1.5         # 多帧, 帧间隔 1.5s
输出 ASCII-only(防 GBK 控制台坑); 回显以 repr 打印, 不可见字符可辨。"""
import sys, time, argparse

PROBE_FRAME = "$KMS:0,0,0,0!"   # Phase 1 实测: 只回显、sscanf 不匹配、零派发 -> 绝不动舵机


def send_one(ser, frame, wait_s):
    ser.reset_input_buffer()
    ser.write(frame.encode("ascii"))
    ser.flush()
    print("TX %d B: %s" % (len(frame), frame))
    end = time.time() + wait_s
    buf = b""
    while time.time() < end:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
    if buf:
        print("RX %d B: %s" % (len(buf), repr(buf)))
    else:
        print("RX 0 B: (no echo)")
    return buf


def main():
    ap = argparse.ArgumentParser(description="send raw KM1 frames and capture echo")
    ap.add_argument("frames", nargs="*", help="frames like {#000P1500T1000!}")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--wait", type=float, default=2.0, help="echo window per frame (s)")
    ap.add_argument("--gap", type=float, default=0.0, help="extra delay between frames (s)")
    ap.add_argument("--probe", action="store_true",
                    help="send no-op $KMS: probe (echo only, never actuates)")
    a = ap.parse_args()

    frames = list(a.frames)
    if a.probe:
        frames.insert(0, PROBE_FRAME)
    if not frames:
        ap.error("no frames (give frames or --probe)")

    import serial
    try:
        ser = serial.Serial(a.port, a.baud, timeout=0.2)
    except Exception as e:
        print("OPEN FAIL %s @ %d: %r" % (a.port, a.baud, e))
        return 2

    try:
        got_any = False
        for i, f in enumerate(frames):
            if i and a.gap > 0:
                time.sleep(a.gap)
            if send_one(ser, f, a.wait):
                got_any = True
        return 0 if got_any else 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
