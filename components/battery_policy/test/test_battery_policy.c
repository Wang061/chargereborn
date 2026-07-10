#include "battery_policy.h"
#include "ai_classes.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n", m); fails++; } }while(0)
#define NEAR(a,b) (fabsf((a) - (b)) < 0.001f)

static void test_grasp_policy(void)
{
    battery_grasp_policy_t p;

    battery_grasp_policy_for_class(AI_CLASS_AA, 1480, &p);
    CHECK(p.mode == BATTERY_GRASP_HORIZONTAL, "AA uses horizontal grasp");
    CHECK(NEAR(p.pick_z_mm, 0.0f), "AA pick_z is 0");
    CHECK(NEAR(p.wrist_extra_deg, 0.0f), "AA wrist extra is 0");
    CHECK(p.gripper_close_pwm == 1480, "AA uses calibrated close pwm");

    battery_grasp_policy_for_class(AI_CLASS_BAD_AA, 1480, &p);
    CHECK(p.mode == BATTERY_GRASP_HORIZONTAL, "bad_AA uses horizontal grasp");
    CHECK(NEAR(p.pick_z_mm, 0.0f), "bad_AA pick_z is 0");

    battery_grasp_policy_for_class(AI_CLASS_18650, 1480, &p);
    CHECK(p.mode == BATTERY_GRASP_VERTICAL, "18650 uses vertical grasp");
    CHECK(NEAR(p.pick_z_mm, 7.0f), "non-AA pick_z is 7");
    CHECK(NEAR(p.wrist_extra_deg, 90.0f), "non-AA wrist extra is 90");
    CHECK(p.gripper_close_pwm == 1640, "non-AA close pwm is 1640");

    battery_grasp_policy_for_class(AI_CLASS_UNKNOWN, 1480, &p);
    CHECK(p.mode == BATTERY_GRASP_VERTICAL, "unknown class follows non-AA vertical default");
}

static void test_risk_policy(void)
{
    battery_risk_result_t r;

    battery_risk_eval_for_class(AI_CLASS_BAD_AA, &r);
    CHECK(r.level == BATTERY_RISK_DANGEROUS, "bad_AA is dangerous");
    CHECK((r.reasons & BATTERY_RISK_REASON_AI_BAD_AA) != 0, "bad_AA reason bit set");
    CHECK((r.reasons & BATTERY_RISK_REASON_SENSORS_UNAVAIL) != 0, "sensor unavailable bit retained");
    CHECK((r.reasons & BATTERY_RISK_REASON_DEMO_FORCE_DANGER) != 0, "demo force danger bit retained");

    battery_risk_eval_for_class(AI_CLASS_AA, &r);
    CHECK(r.level == BATTERY_RISK_DANGEROUS, "demo forces ordinary AA dangerous");
    CHECK((r.reasons & BATTERY_RISK_REASON_AI_BAD_AA) == 0, "ordinary AA has no bad_AA reason");
    CHECK((r.reasons & BATTERY_RISK_REASON_DEMO_FORCE_DANGER) != 0, "ordinary AA has demo force reason");

    battery_risk_eval_for_class(AI_CLASS_18650, &r);
    CHECK(r.level == BATTERY_RISK_DANGEROUS, "demo forces non-AA dangerous");
    CHECK((r.reasons & BATTERY_RISK_REASON_DEMO_FORCE_DANGER) != 0, "non-AA has demo force reason");

    CHECK(strcmp(battery_grasp_mode_name(BATTERY_GRASP_VERTICAL), "vertical") == 0, "grasp mode name");
    CHECK(strcmp(battery_risk_level_name(BATTERY_RISK_DANGEROUS), "dangerous") == 0, "risk level name");
}

int main(void)
{
    test_grasp_policy();
    test_risk_policy();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
