#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 纯 C 目标跟踪器：把逐帧带噪声/丢检/误检的检测框，滤成一个平稳的抓取目标估计。
// 无 ESP 依赖，可 host gcc 单测(同 kinematics/armlink_frame 的约定)。
// 设计：每个数值通道(cx,cy,w,h,cos2θ,sin2θ)各一个标量恒位置(随机游走)卡尔曼滤波,
// 见 docs/superpowers/specs/2026-07-06-final-submission-cleanup-design.md §4。

// —— 输入：单帧一个候选框(与 ai_box_t 解耦，调用方负责从 ai_box_t 转换)——
typedef struct {
    float cx, cy;       // 中心像素坐标
    float w, h;         // 框宽高(像素)
    float angle_deg;    // 长轴角 [0,180)；<0 = 无效(该框不参与角度关联/更新)
    float score;        // 置信度 0..1
    float anisotropy;   // 角度置信 0..1
} track_box_t;

// —— 输出：跟踪器当前估计 ——
typedef struct {
    bool     confirmed;    // 已过 min_hits，可用作抓取目标
    bool     coasting;     // 本帧未关联到检测，靠 predict 滑行(陈旧值，不可用于开始新动作)
    bool     stable;       // 已过稳定判据(含滞回)，armctrl 可据此结束 ACQUIRE
    float    cx, cy;       // 滤波后中心(像素)
    float    w, h;         // 滤波后宽高(像素)
    float    angle_deg;    // 滤波后长轴角 [0,180)
    float    score;        // 最近一次真实关联(非滑行)帧的分数，滑行期间保持不变
    uint32_t hits;         // 累计确认命中数(供调试/日志)
} track_output_t;

#define TRACK_MAX_EXCLUSIONS 8

// —— 内部状态：字段全暴露供调用方静态分配(同 armcal_t 惯例)，调用方不应直接改字段，用下方 API ——
typedef struct {
    bool  initialized;         // 是否已收到过首帧(false 时全部估计无意义)
    bool  confirmed;
    bool  coasting;
    bool  stable;              // 稳定判据锁存值(含滞回)
    bool  suspended;           // track_suspend() 期间为 true，track_update 直接忽略

    uint32_t hits;             // 连续命中计数(达 TRACK_MIN_HITS 转 confirmed)

    // 每通道标量 KF：估计值 x, 方差 P
    float x_cx, p_cx;
    float x_cy, p_cy;
    float x_w,  p_w;
    float x_h,  p_h;
    float x_c2, p_c2;          // cos(2*angle) 通道
    float x_s2, p_s2;          // sin(2*angle) 通道

    int64_t last_ts_us;        // 上次成功 predict/update 的时间戳(算 dt)
    int64_t last_hit_ts_us;    // 上次真实关联命中的时间戳(算 max_age)
    int64_t gate_ts_us;        // track_resume() 设置；早于此的检测整帧丢弃

    float last_nu_cx, last_nu_cy;   // 最近一次关联的新息(供稳定判据)
    float last_score;

    // 挪动重锁缓冲：连续门外候选(最多看最近 3 个，判断是否彼此聚集)
    float reject_cx[3], reject_cy[3];
    int   reject_count;

    // 稳定判据滑窗(环形，长度 5)
    bool  win_good[5];
    float win_cx[5], win_cy[5], win_ang[5];
    int   win_idx;
    int   win_filled;
    int   stable_enter_streak;
    int   stable_exit_streak;

    // 连续模式防重抓：初始化排除区(px 域)；跨 track_resume 持续，只由 track_set_exclusions 管理
    float excl_px[TRACK_MAX_EXCLUSIONS][2];
    int   excl_count;
} track_state_t;

// 清零到 LOST/未初始化状态，含排除区一并清空(用于开机/全新会话)。
void track_init(track_state_t *tr);

// 设置本轮排除区(连续模式防重抓)。pts_px[i]={px,py}；n=0 清空。n>TRACK_MAX_EXCLUSIONS 截断到上限。
void track_set_exclusions(track_state_t *tr, const float pts_px[][2], int n);

// 挂起：armctrl 抓取序列第一个运动原语起调用。挂起期间 track_update 立即返回 false，不做任何计算。
void track_suspend(track_state_t *tr);

// 恢复=硬重置：清估计、清 confirmed/hits/滑窗/稳定锁存，记录 gate_ts_us=now_us
// (早于此的检测按运动帧丢弃)。排除区(excl_px/excl_count)不受影响——那是跨轮状态。
void track_resume(track_state_t *tr, int64_t now_us);

// 喂一帧检测(count 可为 0，boxes 对应可为 NULL)。capture_ts_us 早于 gate_ts_us 或 tr->suspended
// 时整帧丢弃，返回 false。否则跑一次 predict+关联+update(或滑行)+稳定判据推进，返回 true。
bool track_update(track_state_t *tr, const track_box_t *boxes, int count, int64_t capture_ts_us);

// 读取当前估计到 out。tr 从未 initialized 时 out 全零、confirmed/coasting/stable 皆 false。
void track_get(const track_state_t *tr, track_output_t *out);

#ifdef __cplusplus
}
#endif
