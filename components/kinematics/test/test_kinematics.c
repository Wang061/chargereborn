#include "kinematics.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); fails++; } } while(0)

// 测试用固定连杆(仅验证数学,与真机实测连杆无关): L0=100 L1=105 L2=75 L3=180 mm
static const float TL[4] = {100.0f, 105.0f, 75.0f, 180.0f};

int main(void) {
    int pwm[4];
    int r;

    // Anchor A: x=0 正前方 -> 底座居中 pwm0=1500
    r = kin_move_best(TL, 0, 200, 50, pwm);
    CHECK(r == 0, "A reachable");
    CHECK(pwm[0] == 1500 && pwm[1] == 1259 && pwm[2] == 1776 && pwm[3] == 861, "A pwm golden");

    // Anchor B: x=y=120 (底座45°) -> pwm0=1166 (若=1000 说明 theta6 用了错误的 *270/pi)
    r = kin_move_best(TL, 120, 120, 40, pwm);
    CHECK(r == 0, "B reachable");
    CHECK(pwm[0] == 1166 && pwm[1] == 1300 && pwm[2] == 1855 && pwm[3] == 840, "B pwm golden");
    CHECK(pwm[0] != 1000, "B not the *270/pi bug");

    // Anchor C: 太远不可达
    r = kin_move_best(TL, 0, 900, 50, pwm);
    CHECK(r == -1, "C unreachable");

    // Anchor D: y<0 拒绝
    r = kin_move_best(TL, 0, -50, 50, pwm);
    CHECK(r == -1, "D y<0 rejected");

    // solve 指定 alpha=-45 (B 点)
    r = kin_solve(TL, 120, 120, 40, -45.0f, pwm);
    CHECK(r == 0, "B45 solve ok");
    CHECK(pwm[0] == 1166 && pwm[1] == 1597 && pwm[2] == 2470 && pwm[3] == 1372, "B45 pwm golden");

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
