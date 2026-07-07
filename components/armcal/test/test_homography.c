#include "armcal.h"
#include <stdio.h>
#include <math.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)
#define NEAR(a,b) (fabs((a)-(b)) < 0.01)

int main(void) {
    float mx, my;
    // 单位阵: 点映射到自身
    float I[9] = {1,0,0, 0,1,0, 0,0,1};
    homography_apply(I, 100, 80, &mx, &my);
    CHECK(NEAR(mx,100) && NEAR(my,80), "identity");

    // 缩放+平移: mx=0.5*px+10, my=0.5*py+5
    float S[9] = {0.5f,0,10, 0,0.5f,5, 0,0,1};
    homography_apply(S, 100, 80, &mx, &my);
    CHECK(NEAR(mx,60) && NEAR(my,45), "scale+translate");

    // 角度: 单位阵下角度不变
    CHECK(NEAR(homography_angle(I, 30), 30), "angle identity");
    // 纯缩放(各向同性)角度不变
    CHECK(NEAR(homography_angle(S, 30), 30), "angle isotropic scale");

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
