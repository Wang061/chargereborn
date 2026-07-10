#include "armlink_frame.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)

int main(void) {
    char buf[128];
    int n;

    CHECK(armlink_clamp_pwm(400) == 500, "clamp low");
    CHECK(armlink_clamp_pwm(3000) == 2500, "clamp high");
    CHECK(armlink_clamp_pwm(1500) == 1500, "clamp mid");

    int pwm[4] = {1166, 1300, 1855, 840};
    n = armlink_encode_arm_frame(pwm, 1000, buf, sizeof(buf));
    CHECK(n > 0, "arm frame len");
    CHECK(strcmp(buf, "{#000P1166T1000!#001P1300T1000!#002P1855T1000!#003P0840T1000!}") == 0, "arm frame bytes");

    // 越界自动钳位
    int pwm2[4] = {400, 3000, 1500, 1500};
    armlink_encode_arm_frame(pwm2, 800, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#000P0500T0800!#001P2500T0800!#002P1500T0800!#003P1500T0800!}") == 0, "arm frame clamp");

    n = armlink_encode_servo_frame(4, 1500, 600, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#004P1500T0600!}") == 0, "servo frame #004");

    n = armlink_encode_servo_frame(5, 1700, 800, buf, sizeof(buf));
    CHECK(strcmp(buf, "{#005P1700T0800!}") == 0, "servo frame #005");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
