#include "target_track.h"
#include <math.h>

// —— 滤波参数(像素单位；上板实测后按运行手册流程调整这里的数值，不改算法结构)——
// 2026-07-07 静止实标(两次开机~250帧, AA@0.73): σ_cx=3.0 σ_cy=3.6px → R_POS=12;
// h 对称呼吸 σ≈27px(cy 不受累)、w σ≈6px → R_WH 取 σ≈8 折中。
#define TRACK_R_POS       12.0f     // cx,cy 测量噪声方差(px²), σ≈3.5px 实测
#define TRACK_R_WH        64.0f     // w,h 测量噪声方差(px²), σ≈8px 实测折中
#define TRACK_R_ANG       0.011f    // cos2θ/sin2θ 测量噪声方差(单位向量域)
#define TRACK_LAMBDA_POS  0.06f     // 位置通道 λ=q/R(主旋钮): 稳态K≈0.22
#define TRACK_LAMBDA_WH   0.03f     // 尺寸通道 λ 减半(更钝)
#define TRACK_LAMBDA_ANG  0.06f     // 角度通道同位置

#define TRACK_MIN_SCORE_NEW     0.40f   // 新建轨迹门限
#define TRACK_MIN_SCORE_UPDATE  0.25f   // 已确认轨迹更新门限(ByteTrack 双阈值)
#define TRACK_MIN_ANISOTROPY    0.10f   // 角度置信门限

#define TRACK_GATE_NU_K        3.0f     // 新息门限系数: |nu| <= K*sqrt(P+R)
#define TRACK_GATE_MIN_PX      20.0f    // 空间门限最小值(px)

#define TRACK_MIN_HITS          3       // 连续命中数达此值才 confirmed
#define TRACK_MAX_AGE_US    800000LL    // 0.8s，滑行超时判 LOST(微秒)。0707: 1.2s→0.8s 重锁提速

#define TRACK_RECAPTURE_COUNT       2       // 连续N帧门外聚集候选才判"目标被挪动"。0707: 3→2 重锁提速
#define TRACK_RECAPTURE_CLUSTER_PX  20.0f   // 聚集判定：候选两两中心距离阈值

#define TRACK_STABLE_NU_PX      8.0f    // 稳定判据: 单帧新息阈值。0707实标: 新息真实σ≈3.9px,
                                        // 旧值3px只覆盖~58%帧→STABLE靠运气; 8≈2σ覆盖~97%
#define TRACK_STABLE_SIGMA_PX   2.5f    // 稳定判据: sqrt(P) 阈值(R=12下确认期P略高,2.0过卡)
#define TRACK_STABLE_EXTENT_PX  4.0f    // 稳定判据: 滑窗中心极差阈值
#define TRACK_STABLE_EXTENT_DEG 5.0f    // 稳定判据: 滑窗角度极差阈值
#define TRACK_STABLE_MAX_MISS   1       // 滑窗内(5帧)允许的最大miss数
#define TRACK_STABLE_ENTER_N    3       // 连续满足N帧才进入STABLE。0707: 5→3 重锁提速
#define TRACK_STABLE_EXIT_N     2       // 连续不满足N帧才退出STABLE

#define TRACK_DT_MAX_S   2.0f     // dt钳位上限(防止长间隔后 predict 让 P 异常增大)

// —— 单通道标量KF ——
static void kf1_predict(float *p, float q_dot, float dt_s) { *p += q_dot * dt_s; }   // 恒位置模型: x 不变

static float kf1_update(float *x, float *p, float z, float r) {
    float s  = *p + r;
    float nu = z - *x;
    float k  = *p / s;
    *x += k * nu;
    *p *= (1.0f - k);
    return nu;
}

// 初值方差 3R: 首帧后 σ≈6px 级不确定度——既容纳真实抖动, 又保证第二帧起
// 类翻转框跳(~31px, 见 test_class_flip)仍被新息门限拒收(3√(3R+R)≈21px < 31px)。
static void kf1_init(float *x, float *p, float z, float r) { *x = z; *p = 3.0f * r; }

// —— 角度双通道编解码(mod-180°回绕安全) ——
static void angle_to_vec(float angle_deg, float *c2, float *s2) {
    float two_theta_rad = angle_deg * (float)M_PI / 90.0f;   // *2 倍角, deg->rad
    *c2 = cosf(two_theta_rad);
    *s2 = sinf(two_theta_rad);
}
static float vec_to_angle(float c2, float s2) {
    float two_theta_deg = atan2f(s2, c2) * 180.0f / (float)M_PI;   // (-180,180]
    float theta_deg = two_theta_deg / 2.0f;                         // (-90,90]
    if (theta_deg < 0.0f) theta_deg += 180.0f;                      // 折到 [0,180)
    return theta_deg;
}
static float angle_diff_mod180(float a, float b) {
    float d = a - b;
    while (d > 90.0f) d -= 180.0f;
    while (d < -90.0f) d += 180.0f;
    return d;
}

// —— 内部：清空KF估计与生命周期状态，不碰 gate_ts_us ——
static void reset_estimate(track_state_t *tr) {
    tr->initialized = false;
    tr->confirmed = false;
    tr->coasting = false;
    tr->stable = false;
    tr->hits = 0;
    tr->x_cx = tr->p_cx = 0.0f; tr->x_cy = tr->p_cy = 0.0f;
    tr->x_w  = tr->p_w  = 0.0f; tr->x_h  = tr->p_h  = 0.0f;
    tr->x_c2 = tr->p_c2 = 0.0f; tr->x_s2 = tr->p_s2 = 0.0f;
    tr->last_ts_us = 0;
    tr->last_hit_ts_us = 0;
    tr->last_nu_cx = tr->last_nu_cy = 0.0f;
    tr->last_score = 0.0f;
    tr->last_cls = -1;
    tr->reject_count = 0;
    tr->win_idx = 0; tr->win_filled = 0;
    for (int i = 0; i < 5; i++) { tr->win_good[i] = false; tr->win_cx[i] = tr->win_cy[i] = tr->win_ang[i] = 0.0f; }
    tr->stable_enter_streak = 0;
    tr->stable_exit_streak = 0;
}

void track_init(track_state_t *tr) {
    if (!tr) return;
    reset_estimate(tr);
    tr->suspended = false;
    tr->gate_ts_us = 0;
}

void track_suspend(track_state_t *tr) { if (tr) tr->suspended = true; }

void track_resume(track_state_t *tr, int64_t now_us) {
    if (!tr) return;
    reset_estimate(tr);
    tr->suspended = false;
    tr->gate_ts_us = now_us;
}

static void advance_stability_window(track_state_t *tr, bool hit) {
    bool instant_ok = tr->confirmed && !tr->coasting && hit
                       && fabsf(tr->last_nu_cx) <= TRACK_STABLE_NU_PX
                       && fabsf(tr->last_nu_cy) <= TRACK_STABLE_NU_PX
                       && sqrtf(tr->p_cx) <= TRACK_STABLE_SIGMA_PX
                       && sqrtf(tr->p_cy) <= TRACK_STABLE_SIGMA_PX;

    tr->win_good[tr->win_idx] = instant_ok;
    tr->win_cx[tr->win_idx] = tr->x_cx;
    tr->win_cy[tr->win_idx] = tr->x_cy;
    tr->win_ang[tr->win_idx] = vec_to_angle(tr->x_c2, tr->x_s2);
    tr->win_idx = (tr->win_idx + 1) % 5;
    if (tr->win_filled < 5) tr->win_filled++;

    bool overall_ok = false;
    if (tr->win_filled == 5) {
        int miss = 0;
        float minx = tr->win_cx[0], maxx = tr->win_cx[0];
        float miny = tr->win_cy[0], maxy = tr->win_cy[0];
        for (int i = 0; i < 5; i++) {
            if (!tr->win_good[i]) miss++;
            if (tr->win_cx[i] < minx) minx = tr->win_cx[i];
            if (tr->win_cx[i] > maxx) maxx = tr->win_cx[i];
            if (tr->win_cy[i] < miny) miny = tr->win_cy[i];
            if (tr->win_cy[i] > maxy) maxy = tr->win_cy[i];
        }
        float ang_extent = 0.0f;
        for (int i = 0; i < 5; i++)
            for (int j = i + 1; j < 5; j++) {
                float d = fabsf(angle_diff_mod180(tr->win_ang[i], tr->win_ang[j]));
                if (d > ang_extent) ang_extent = d;
            }
        overall_ok = (miss <= TRACK_STABLE_MAX_MISS)
                     && (maxx - minx) <= TRACK_STABLE_EXTENT_PX
                     && (maxy - miny) <= TRACK_STABLE_EXTENT_PX
                     && ang_extent <= TRACK_STABLE_EXTENT_DEG;
    }

    if (overall_ok) { tr->stable_enter_streak++; tr->stable_exit_streak = 0; }
    else            { tr->stable_exit_streak++;  tr->stable_enter_streak = 0; }

    if (!tr->stable && tr->stable_enter_streak >= TRACK_STABLE_ENTER_N) tr->stable = true;
    if (tr->stable  && tr->stable_exit_streak  >= TRACK_STABLE_EXIT_N)  tr->stable = false;
}

bool track_update(track_state_t *tr, const track_box_t *boxes, int count, int64_t capture_ts_us)
{
    if (!tr) return false;
    if (tr->suspended) return false;
    if (capture_ts_us < tr->gate_ts_us) return false;   // 运动帧/挂起前遗留帧: 整帧丢弃

    float dt = 0.0f;
    if (tr->initialized) {
        dt = (float)(capture_ts_us - tr->last_ts_us) / 1e6f;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > TRACK_DT_MAX_S) dt = TRACK_DT_MAX_S;
        kf1_predict(&tr->p_cx, TRACK_LAMBDA_POS * TRACK_R_POS, dt);
        kf1_predict(&tr->p_cy, TRACK_LAMBDA_POS * TRACK_R_POS, dt);
        kf1_predict(&tr->p_w,  TRACK_LAMBDA_WH  * TRACK_R_WH,  dt);
        kf1_predict(&tr->p_h,  TRACK_LAMBDA_WH  * TRACK_R_WH,  dt);
        kf1_predict(&tr->p_c2, TRACK_LAMBDA_ANG * TRACK_R_ANG, dt);
        kf1_predict(&tr->p_s2, TRACK_LAMBDA_ANG * TRACK_R_ANG, dt);
    }
    tr->last_ts_us = capture_ts_us;

    // —— 关联：在门限内找最佳候选 ——
    int best = -1;
    float best_score = -1.0f;
    float min_score = tr->confirmed ? TRACK_MIN_SCORE_UPDATE : TRACK_MIN_SCORE_NEW;

    for (int i = 0; i < count; i++) {
        const track_box_t *b = &boxes[i];
        if (b->score < min_score) continue;
        if (tr->initialized) {
            // 已有估计: 空间门限 + 新息门限
            float dx = b->cx - tr->x_cx, dy = b->cy - tr->x_cy;
            float gate_r = fmaxf(0.5f * sqrtf(fmaxf(tr->x_w * tr->x_h, 0.0f)), TRACK_GATE_MIN_PX);
            if (dx * dx + dy * dy > gate_r * gate_r) continue;
            float thr_cx = TRACK_GATE_NU_K * sqrtf(tr->p_cx + TRACK_R_POS);
            float thr_cy = TRACK_GATE_NU_K * sqrtf(tr->p_cy + TRACK_R_POS);
            if (fabsf(dx) > thr_cx || fabsf(dy) > thr_cy) continue;
        }
        if (b->score > best_score) { best_score = b->score; best = i; }
    }

    bool hit = (best >= 0);

    if (hit) {
        const track_box_t *b = &boxes[best];
        if (!tr->initialized) {
            kf1_init(&tr->x_cx, &tr->p_cx, b->cx, TRACK_R_POS);
            kf1_init(&tr->x_cy, &tr->p_cy, b->cy, TRACK_R_POS);
            kf1_init(&tr->x_w,  &tr->p_w,  b->w,  TRACK_R_WH);
            kf1_init(&tr->x_h,  &tr->p_h,  b->h,  TRACK_R_WH);
            tr->last_nu_cx = 0.0f; tr->last_nu_cy = 0.0f;
            if (b->angle_deg >= 0.0f && b->anisotropy >= TRACK_MIN_ANISOTROPY) {
                float c2, s2; angle_to_vec(b->angle_deg, &c2, &s2);
                kf1_init(&tr->x_c2, &tr->p_c2, c2, TRACK_R_ANG);
                kf1_init(&tr->x_s2, &tr->p_s2, s2, TRACK_R_ANG);
            } else {
                kf1_init(&tr->x_c2, &tr->p_c2, 1.0f, TRACK_R_ANG);   // 默认0度,角度置信不足
                kf1_init(&tr->x_s2, &tr->p_s2, 0.0f, TRACK_R_ANG);
            }
            tr->initialized = true;
            tr->hits = 1;
        } else {
            tr->last_nu_cx = kf1_update(&tr->x_cx, &tr->p_cx, b->cx, TRACK_R_POS);
            tr->last_nu_cy = kf1_update(&tr->x_cy, &tr->p_cy, b->cy, TRACK_R_POS);
            kf1_update(&tr->x_w, &tr->p_w, b->w, TRACK_R_WH);
            kf1_update(&tr->x_h, &tr->p_h, b->h, TRACK_R_WH);
            if (b->angle_deg >= 0.0f && b->anisotropy >= TRACK_MIN_ANISOTROPY) {
                float c2, s2; angle_to_vec(b->angle_deg, &c2, &s2);
                kf1_update(&tr->x_c2, &tr->p_c2, c2, TRACK_R_ANG);
                kf1_update(&tr->x_s2, &tr->p_s2, s2, TRACK_R_ANG);
            }
            tr->hits++;
        }
        tr->last_score = b->score;
        tr->last_cls = b->cls;
        tr->last_hit_ts_us = capture_ts_us;
        tr->coasting = false;
        tr->reject_count = 0;
        if (!tr->confirmed && tr->hits >= TRACK_MIN_HITS) tr->confirmed = true;
    } else if (tr->initialized) {
        tr->coasting = true;
        bool timed_out = (capture_ts_us - tr->last_hit_ts_us) > TRACK_MAX_AGE_US;
        if (timed_out) {
            reset_estimate(tr);   // LOST: 下一帧起按全新目标处理(简化设计: 不预置新位置,省去二次关联)
        } else if (count > 0) {
            // 挪动重锁判定: 把本帧最高分但被门限拒绝的候选记入 reject 缓冲
            int rj = -1; float rj_score = -1.0f;
            for (int i = 0; i < count; i++) {
                if (boxes[i].score >= min_score && boxes[i].score > rj_score) { rj_score = boxes[i].score; rj = i; }
            }
            if (rj >= 0) {
                if (tr->reject_count < 3) {
                    tr->reject_cx[tr->reject_count] = boxes[rj].cx;
                    tr->reject_cy[tr->reject_count] = boxes[rj].cy;
                    tr->reject_count++;
                }
                if (tr->reject_count >= TRACK_RECAPTURE_COUNT) {
                    bool clustered = true;
                    for (int i = 0; i < tr->reject_count && clustered; i++)
                        for (int j = i + 1; j < tr->reject_count; j++) {
                            float dx = tr->reject_cx[i] - tr->reject_cx[j];
                            float dy = tr->reject_cy[i] - tr->reject_cy[j];
                            if (dx * dx + dy * dy > TRACK_RECAPTURE_CLUSTER_PX * TRACK_RECAPTURE_CLUSTER_PX) { clustered = false; break; }
                        }
                    if (clustered) reset_estimate(tr);   // 目标被挪动: 硬重置,下一帧起按新目标重新确认
                }
            }
        }
    }
    // tr->initialized==false 且无命中: 什么都不做,继续等待首帧候选

    advance_stability_window(tr, hit);
    return true;
}

void track_get(const track_state_t *tr, track_output_t *out) {
    if (!out) return;
    if (!tr || !tr->initialized) {
        out->confirmed = false; out->coasting = false; out->stable = false;
        out->cls = -1;
        out->cx = out->cy = out->w = out->h = out->angle_deg = out->score = 0.0f;
        out->hits = 0;
        return;
    }
    out->confirmed = tr->confirmed;
    out->coasting  = tr->coasting;
    out->stable    = tr->confirmed && tr->stable;
    out->cls = tr->last_cls;
    out->cx = tr->x_cx; out->cy = tr->x_cy;
    out->w  = tr->x_w;  out->h  = tr->x_h;
    out->angle_deg = vec_to_angle(tr->x_c2, tr->x_s2);
    out->score = tr->last_score;
    out->hits  = tr->hits;
}
