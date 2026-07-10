"""IK 预览: PC 侧镜像 components/kinematics/kinematics.c 的 kin_solve/kin_move_best,
把 (x,y,z)mm 解成 4 舵机 PWM 与裸协议捆绑帧, 供 G1/标点第二段经 COM4 直连 KM1 手动发帧用
(标定完成前 Brain 侧被"未标定强制干跑"联锁挡住, 这是设计; 见 docs/ai/ARM_PIPELINE.md)。
保真手段: --selftest 用固件 kin_selftest 的 golden 锚点对拍(链长 100/105/75/180)。
默认链长/观察位 = components/armcal/armcal.c 的 armcal_defaults(); 改那边必须同步这边。
用法:
  python scripts/ik_preview.py --selftest
  python scripts/ik_preview.py 40 105 50            # 解一点, 出捆绑帧(默认 ms=1500)
  python scripts/ik_preview.py 40 105 50 --ms 2000
  python scripts/ik_preview.py --observe            # 出 中转点+观察位 两帧(标定前摆位用)
输出 ASCII-only, 防 Windows GBK 控制台坑。"""
import sys, math, argparse

# —— 与 armcal_defaults() 同步(连杆 2026-07-02 实测; 观察位 2026-07-04 定稿=OpenMV Home) ——
DEFAULT_LINKS = (107.0, 107.0, 86.5, 165.0)
OBSERVE = (0.0, 100.0, 70.0)   # observe_x/y/z
CARRY_Z = 120.0

PI = 3.14159265358979


def kin_solve(links, x, y, z, alpha_deg):
    """逐行镜像 kinematics.c kin_solve; 返回 (0,pwm[4]) 或 (errcode,None)。"""
    l0, l1, l2, l3 = links
    theta6 = 0.0 if x == 0.0 else math.atan2(x, y) * 180.0 / PI
    yy = math.sqrt(x * x + y * y) - l3 * math.cos(alpha_deg * PI / 180.0)
    zz = z - l0 - l3 * math.sin(alpha_deg * PI / 180.0)
    if zz < -l0:
        return 1, None
    if math.sqrt(yy * yy + zz * zz) > (l1 + l2):
        return 2, None
    ccc = math.acos(yy / math.sqrt(yy * yy + zz * zz))
    bbb = (yy * yy + zz * zz + l1 * l1 - l2 * l2) / (2.0 * l1 * math.sqrt(yy * yy + zz * zz))
    if bbb > 1.0 or bbb < -1.0:
        return 3, None
    zf = -1.0 if zz < 0.0 else 1.0
    theta5 = (ccc * zf + math.acos(bbb)) * 180.0 / PI
    if theta5 > 180.0 or theta5 < 0.0:
        return 4, None
    aaa = -(yy * yy + zz * zz - l1 * l1 - l2 * l2) / (2.0 * l1 * l2)
    if aaa > 1.0 or aaa < -1.0:
        return 5, None
    theta4 = 180.0 - math.acos(aaa) * 180.0 / PI
    if theta4 > 135.0 or theta4 < -135.0:
        return 6, None
    theta3 = alpha_deg - theta5 + theta4
    if theta3 > 90.0 or theta3 < -90.0:
        return 7, None
    pwm = [int(1500.0 - 2000.0 * theta6 / 270.0),
           int(1500.0 + 2000.0 * (theta5 - 90.0) / 270.0),
           int(1500.0 + 2000.0 * theta4 / 270.0),
           int(1500.0 + 2000.0 * theta3 / 270.0)]
    return 0, pwm


def kin_move_best(links, x, y, z):
    """镜像 kin_move_best: 扫 alpha 0..-135 取最负可达解; 返回 (alpha,pwm) 或 (None,None)。"""
    if y < 0.0:
        return None, None
    best_alpha, found = 0, False
    for i in range(0, -136, -1):
        e, _ = kin_solve(links, x, y, z, float(i))
        if e == 0:
            if i < best_alpha:
                best_alpha = i
            found = True
    if not found:
        return None, None
    e, pwm = kin_solve(links, x, y, z, float(best_alpha))
    return best_alpha, pwm


def clamp_pwm(p):
    return 500 if p < 500 else (2500 if p > 2500 else p)


def bundle_frame(pwm, ms):
    return ("{#000P%04dT%04d!#001P%04dT%04d!#002P%04dT%04d!#003P%04dT%04d!}"
            % (clamp_pwm(pwm[0]), ms, clamp_pwm(pwm[1]), ms,
               clamp_pwm(pwm[2]), ms, clamp_pwm(pwm[3]), ms))


def solve_and_print(links, x, y, z, ms):
    alpha, pwm = kin_move_best(links, x, y, z)
    if pwm is None:
        print("UNREACHABLE: (%.1f, %.1f, %.1f) links=%s" % (x, y, z, links))
        return 1
    print("target mm : (%.1f, %.1f, %.1f)   links=%s" % (x, y, z, links))
    print("best alpha: %d deg" % alpha)
    print("pwm       : #000=%d #001=%d #002=%d #003=%d" % tuple(pwm))
    print("frame     : %s" % bundle_frame(pwm, ms))
    print("(COM4 direct sends as-is; Brain at grade<=1 would stretch ms x1.5)")
    return 0


def selftest():
    """对拍 kinematics.c kin_selftest 内嵌 golden 锚点(测试链长 100/105/75/180)。"""
    tl = (100.0, 105.0, 75.0, 180.0)
    cases = [((0, 200, 50), [1500, 1259, 1776, 861]),
             ((120, 120, 40), [1166, 1300, 1855, 840])]
    ok = True
    for (x, y, z), want in cases:
        _, pwm = kin_move_best(tl, x, y, z)
        status = "PASS" if pwm == want else "FAIL"
        if pwm != want:
            ok = False
        print("anchor (%d,%d,%d): got %s want %s  %s" % (x, y, z, pwm, want, status))
    _, pwm = kin_move_best(tl, 0, 900, 50)
    status = "PASS" if pwm is None else "FAIL"
    if pwm is not None:
        ok = False
    print("anchor unreachable (0,900,50): %s  %s" % ("None" if pwm is None else pwm, status))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="IK preview (mirror of kinematics.c) for COM4 manual frames")
    ap.add_argument("xyz", nargs="*", type=float, help="x y z in mm")
    ap.add_argument("--ms", type=int, default=1500, help="move time ms (default 1500)")
    ap.add_argument("--links", nargs=4, type=float, metavar=("L0", "L1", "L2", "L3"),
                    default=list(DEFAULT_LINKS), help="link lengths mm (default = armcal defaults)")
    ap.add_argument("--selftest", action="store_true", help="check against firmware golden anchors")
    ap.add_argument("--observe", action="store_true", help="emit waypoint+observe frames (calib pose)")
    a = ap.parse_args()
    links = tuple(a.links)
    if a.selftest:
        sys.exit(selftest())
    if a.observe:
        print("== waypoint (0, %.0f, %.0f) ==" % (OBSERVE[1], CARRY_Z))
        r1 = solve_and_print(links, 0.0, OBSERVE[1], CARRY_Z, a.ms)
        print("== observe (%.0f, %.0f, %.0f) ==" % OBSERVE)
        r2 = solve_and_print(links, OBSERVE[0], OBSERVE[1], OBSERVE[2], a.ms)
        sys.exit(r1 or r2)
    if len(a.xyz) != 3:
        ap.error("need x y z (or --selftest / --observe)")
    sys.exit(solve_and_print(links, a.xyz[0], a.xyz[1], a.xyz[2], a.ms))


if __name__ == "__main__":
    main()
