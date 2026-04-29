#ifndef APEX_TYPES_H
#define APEX_TYPES_H

#include <stdbool.h>

#define MAX_TASKS 160
#define APAF_MONITOR_INTERVAL 8
#define SCHED_PERIOD 20
#define DECAY_LAMBDA 0.693
#define TICK_SECONDS 0.032
#define LUT256_SIZE 256
#define CUBIC_LUT64_SIZE 64
#define CUBIC_A_BASE 16384

typedef enum {
    MODE_EXACT = 0,
    MODE_LINEAR,
    MODE_LUT256,
    MODE_POLY2,
    MODE_ADAPTIVE
} ApproxMode;

typedef enum {
    STATE_SAFE = 0,
    STATE_CAUTION,
    STATE_STRICT
} SafetyState;

typedef struct {
    int tid;
    int nice;
    int weight;
    bool runnable;
    double vruntime;
    double ideal_vruntime;
    double vruntime_drift;
    double load_avg;
    double load_current;
    double effective_weight;
    int exec_ticks;
    int wait_ticks_since_run;
    int max_wait_ticks;
    int starvation_events;
} Task;

typedef struct {
    ApproxMode requested_mode;
    SafetyState state;
    int safe_ticks;
    int caution_ticks;
    int strict_ticks;
    int reaction_ticks;
    bool reaction_seen;
    int switch_tick;
    int safe_to_caution;
    int caution_to_strict;
    int strict_to_caution;
    int caution_to_safe;
} Controller;

typedef struct {
    double fairness_violation;
    double max_vruntime_dev_pct;
    int starvation_count;
    double high_prio_cpu_share_pct;
    int max_wait_ticks;
    double avg_wait_ticks;
    double max_error_pct;
    double theo_bound_pct;
    int violations;
    int safe_to_caution;
    int caution_to_strict;
    int strict_to_caution;
    int caution_to_safe;
} Result;

typedef struct {
    int safe_phase[3];
    int caution_phase[3];
    int strict_phase[3];
    double fairness_phase[3];
    double fairness_sum[3];
    int fairness_count[3];
} ControllerPhaseResult;

typedef struct {
    double mean_cube_error_pct;
    double max_cube_error_pct;
    double mean_wnd_error_pct;
    double max_wnd_error_pct;
    double throughput_impact_pct;
    double exact_cycles;
    double approx_cycles;
    double speedup;
} CubicResult;

#endif
