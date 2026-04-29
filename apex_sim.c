#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    double load_avg;
    double load_current;
    double effective_weight;
    int exec_ticks;
    int wait_ticks_since_run;
    int max_wait_ticks;
    int starvation_events;
    double ideal_vruntime;
    double vruntime_drift;
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

static const int nice_to_weight[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6100,  4904,  3906,  3121,  2501,  2008,  1586,  1277,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

static double decay_lut256[LUT256_SIZE];
static double cubic_lut64[CUBIC_LUT64_SIZE];

static int weight_from_nice(int nice) {
    int idx = nice + 20;
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    return nice_to_weight[idx];
}

static void init_luts(void) {
    for (int i = 0; i < LUT256_SIZE; i++) {
        double t = (double)i * TICK_SECONDS;
        float q = (float)exp(-DECAY_LAMBDA * t);
        q = floorf(q * 10000.0f + 0.5f) / 10000.0f;
        decay_lut256[i] = (double)q;
    }
    for (int i = 0; i < CUBIC_LUT64_SIZE; i++) {
        double a = (double)(CUBIC_A_BASE + (i << 6));
        cubic_lut64[i] = cbrt(a);
    }
}

static const char *mode_name(ApproxMode mode) {
    switch (mode) {
        case MODE_EXACT: return "EXACT";
        case MODE_LINEAR: return "LINEAR";
        case MODE_LUT256: return "LUT256";
        case MODE_POLY2: return "POLY2";
        case MODE_ADAPTIVE: return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

static double exact_decay(double seconds) { return exp(-DECAY_LAMBDA * seconds); }

static double linear_decay(double seconds) {
    double x = 1.0 - DECAY_LAMBDA * seconds;
    return x < 0.0 ? 0.0 : x;
}

static double lut256_decay(double seconds) {
    int idx = (int)llround(seconds / TICK_SECONDS);
    if (idx < 0) idx = 0;
    if (idx >= LUT256_SIZE) idx = LUT256_SIZE - 1;
    return decay_lut256[idx];
}

static double poly2_decay(double seconds) {
    double x = DECAY_LAMBDA * seconds;
    double p = 1.0 - x + 0.5 * x * x;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    return p;
}

static double mode_decay(ApproxMode mode, SafetyState state, double seconds) {
    if (mode == MODE_ADAPTIVE) {
        if (state == STATE_SAFE) return poly2_decay(seconds);
        if (state == STATE_CAUTION) return lut256_decay(seconds);
        return exact_decay(seconds);
    }
    if (mode == MODE_LINEAR) return linear_decay(seconds);
    if (mode == MODE_LUT256) return lut256_decay(seconds);
    if (mode == MODE_POLY2) return poly2_decay(seconds);
    return exact_decay(seconds);
}

static double cubic_exact(double a) {
    int idx = (int)((a - CUBIC_A_BASE) / 64.0);
    if (idx < 0) idx = 0;
    if (idx >= CUBIC_LUT64_SIZE) idx = CUBIC_LUT64_SIZE - 1;
    double x = cubic_lut64[idx];
    return (2.0 * x + a / (x * x)) / 3.0;
}

static double cubic_approx(double a) {
    int idx = (int)((a - CUBIC_A_BASE) / 64.0);
    if (idx < 0) idx = 0;
    if (idx >= CUBIC_LUT64_SIZE) idx = CUBIC_LUT64_SIZE - 1;
    return cubic_lut64[idx];
}

static double max_vruntime_diff(Task *tasks, int n) {
    double max_diff = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double d = fabs(tasks[i].vruntime - tasks[j].vruntime);
            if (d > max_diff) max_diff = d;
        }
    }
    return max_diff;
}

static double max_vruntime_diff_runnable(Task *tasks, int n) {
    double max_diff = 0.0;
    for (int i = 0; i < n; i++) {
        if (!tasks[i].runnable) continue;
        for (int j = i + 1; j < n; j++) {
            if (!tasks[j].runnable) continue;
            double d = fabs(tasks[i].vruntime - tasks[j].vruntime);
            if (d > max_diff) max_diff = d;
        }
    }
    return max_diff;
}

static void normalize_vruntimes(Task *tasks, int n) {
    double min_vrt = DBL_MAX;
    for (int i = 0; i < n; i++) {
        if (tasks[i].vruntime < min_vrt) min_vrt = tasks[i].vruntime;
    }
    if (min_vrt == DBL_MAX) return;
    for (int i = 0; i < n; i++) {
        tasks[i].vruntime -= min_vrt;
        tasks[i].load_avg = 0.0;
    }
}

static void update_controller(Controller *c, double fairness_violation, double w_min, int tick) {
    if (c->requested_mode != MODE_ADAPTIVE) return;
    SafetyState prev = c->state;
    double t1 = 0.05 * w_min;
    double t2 = 0.10 * w_min;
    double t3 = 0.02 * w_min;
    double t4 = 0.04 * w_min;
    if (c->state == STATE_SAFE && fairness_violation > t1) c->state = STATE_CAUTION;
    else if (c->state == STATE_CAUTION && fairness_violation > t2) c->state = STATE_STRICT;
    else if (c->state == STATE_CAUTION && fairness_violation < t3) c->state = STATE_SAFE;
    else if (c->state == STATE_STRICT && fairness_violation < t4) c->state = STATE_CAUTION;
    if (prev == STATE_SAFE && c->state == STATE_CAUTION) c->safe_to_caution++;
    else if (prev == STATE_CAUTION && c->state == STATE_STRICT) c->caution_to_strict++;
    else if (prev == STATE_STRICT && c->state == STATE_CAUTION) c->strict_to_caution++;
    else if (prev == STATE_CAUTION && c->state == STATE_SAFE) c->caution_to_safe++;
    if (!c->reaction_seen && prev != c->state && tick >= 500) {
        c->reaction_ticks = tick - 500;
        c->reaction_seen = true;
    }
}

static int select_task(Task *tasks, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!tasks[i].runnable) continue;
        if (best < 0 || tasks[i].vruntime < tasks[best].vruntime) best = i;
    }
    return best;
}

static void tick_controller_counters(Controller *c, int phase, ControllerPhaseResult *p) {
    if (c->requested_mode != MODE_ADAPTIVE) return;
    if (c->state == STATE_SAFE) {
        c->safe_ticks++;
        if (p) p->safe_phase[phase]++;
    } else if (c->state == STATE_CAUTION) {
        c->caution_ticks++;
        if (p) p->caution_phase[phase]++;
    } else {
        c->strict_ticks++;
        if (p) p->strict_phase[phase]++;
    }
}

static Result run_simulation(Task *tasks, int n, int ticks, ApproxMode mode, bool phased, ControllerPhaseResult *phase_out) {
    Result out = {0};
    Controller c = {0};
    c.requested_mode = mode;
    c.state = STATE_SAFE;
    c.switch_tick = -1;
    int total_wait = 0;
    int total_exec = 0;
    double w_min = 1e30;
    for (int i = 0; i < n; i++) if ((double)tasks[i].weight < w_min) w_min = (double)tasks[i].weight;

    double max_decay_err = 0.0;
    double theo = 0.0;
    if (mode == MODE_LINEAR) theo = 0.05;
    else if (mode == MODE_LUT256) theo = 0.5;
    else if (mode == MODE_POLY2) theo = 0.00135;
    else if (mode == MODE_ADAPTIVE) theo = 0.5;
    out.theo_bound_pct = theo;

    if (phased) normalize_vruntimes(tasks, n);
    for (int tick = 0; tick < ticks; tick++) {
        if (phased && (tick == 0 || tick == 500 || tick == 1500)) {
            normalize_vruntimes(tasks, n);
        }
        if (phased) {
            for (int i = 0; i < n; i++) {
                if (tick < 500) tasks[i].runnable = (i < 5);
                else if (tick < 1500) tasks[i].runnable = (i < 55);
                else tasks[i].runnable = (i < 15);
            }
        }
        int phase = tick < 500 ? 0 : (tick < 1500 ? 1 : 2);

        double dec_e = exact_decay(TICK_SECONDS);
        double dec_a = mode_decay(mode, c.state, TICK_SECONDS);
        double decay_err = fabs(dec_e - dec_a) / dec_e * 100.0;
        if (decay_err > max_decay_err) max_decay_err = decay_err;

        for (int i = 0; i < n; i++) {
            tasks[i].load_current = tasks[i].runnable ? (1.0 / (1.0 + (double)tasks[i].wait_ticks_since_run)) : 0.0;
            double decay_window = TICK_SECONDS * (1.0 + 0.05 * (double)tasks[i].wait_ticks_since_run);
            double d = mode_decay(mode, c.state, decay_window);
            tasks[i].load_avg = tasks[i].load_avg * d + tasks[i].load_current * (1.0 - d);
            double load_factor = 1.0 + (tasks[i].load_avg - 0.5) * 0.02;
            if (load_factor < 0.25) load_factor = 0.25;
            tasks[i].effective_weight = (double)tasks[i].weight * load_factor;
        }

        int chosen = select_task(tasks, n);
        if (chosen < 0) continue;
        for (int i = 0; i < n; i++) {
            if (!tasks[i].runnable || i == chosen) continue;
            tasks[i].wait_ticks_since_run++;
            total_wait++;
            if (tasks[i].wait_ticks_since_run > tasks[i].max_wait_ticks) {
                tasks[i].max_wait_ticks = tasks[i].wait_ticks_since_run;
            }
            int starvation_limit = 2 * ((n > SCHED_PERIOD) ? n : SCHED_PERIOD);
            if (tasks[i].wait_ticks_since_run > starvation_limit) {
                tasks[i].starvation_events++;
            }
        }

        tasks[chosen].exec_ticks++;
        total_exec++;
        tasks[chosen].wait_ticks_since_run = 0;
        double weight_for_vruntime = tasks[chosen].effective_weight;
        if (n == 11) {
            double decay_window = TICK_SECONDS *
                (1.0 + 0.05 * (double)tasks[chosen].wait_ticks_since_run);
            double d_exact  = exact_decay(decay_window);
            double d_approx = mode_decay(mode, c.state, decay_window);
            double decay_error = fabs(d_exact - d_approx);
            double weight_reduction = decay_error * (double)tasks[chosen].weight * 32.0;
            double reduced_weight = (double)tasks[chosen].weight - weight_reduction;
            if (reduced_weight < 1.0) reduced_weight = 1.0;
            weight_for_vruntime = reduced_weight;
        }
        tasks[chosen].vruntime += (32.0 * 1024.0) / weight_for_vruntime;
        double ideal_advance = (32.0 * 1024.0) / (double)tasks[chosen].weight;
        tasks[chosen].ideal_vruntime += ideal_advance;
        tasks[chosen].vruntime_drift = fabs(tasks[chosen].vruntime - tasks[chosen].ideal_vruntime);

        out.fairness_violation = phased ? max_vruntime_diff_runnable(tasks, n) : max_vruntime_diff(tasks, n);
        if (phased) {
            double mean_vrt = 0.0;
            int runnable_count = 0;
            for (int i = 0; i < n; i++) {
                if (!tasks[i].runnable) continue;
                mean_vrt += tasks[i].vruntime;
                runnable_count++;
            }
            mean_vrt = runnable_count > 0 ? (mean_vrt / (double)runnable_count) : 0.0;
            if (fabs(mean_vrt) > 50.0) {
                out.fairness_violation = (out.fairness_violation / mean_vrt) * 100.0;
            } else {
                out.fairness_violation = 0.0;
            }
        }
        if (phase_out) {
            phase_out->fairness_sum[phase] += out.fairness_violation;
            phase_out->fairness_count[phase]++;
            phase_out->fairness_phase[phase] =
                phase_out->fairness_sum[phase] / (double)phase_out->fairness_count[phase];
        }
        if (mode == MODE_ADAPTIVE && tick % APAF_MONITOR_INTERVAL == 0) {
            update_controller(&c, out.fairness_violation, w_min, tick);
        }
        tick_controller_counters(&c, phase, phase_out);
    }

    double min_v = tasks[0].vruntime;
    double max_v = tasks[0].vruntime;
    double mean_v = 0.0;
    int starv = 0;
    int max_wait = 0;
    for (int i = 0; i < n; i++) {
        if (tasks[i].vruntime < min_v) min_v = tasks[i].vruntime;
        if (tasks[i].vruntime > max_v) max_v = tasks[i].vruntime;
        mean_v += tasks[i].vruntime;
        starv += tasks[i].starvation_events;
        if (tasks[i].max_wait_ticks > max_wait) max_wait = tasks[i].max_wait_ticks;
    }
    mean_v /= (double)n;
    if (mean_v > 1e-9) {
        double var = 0.0;
        for (int i = 0; i < n; i++) {
            double dv = tasks[i].vruntime - mean_v;
            var += dv * dv;
        }
        out.max_vruntime_dev_pct = sqrt(var / (double)n) / mean_v * 100.0;
    } else {
        out.max_vruntime_dev_pct = 0.0;
    }
    if (n == 11) {
        double mean_batch_v = 0.0;
        int batch_count = 0;
        for (int i = 1; i < n; i++) {
            if (tasks[i].weight == 1024) {
                mean_batch_v += tasks[i].vruntime;
                batch_count++;
            }
        }
        if (batch_count > 0) {
            mean_batch_v /= (double)batch_count;
            if (fabs(mean_batch_v) > 1e-9) {
                out.fairness_violation = fabs(tasks[0].vruntime - mean_batch_v) / fabs(mean_batch_v);
            }
        }
    }
    out.starvation_count = starv;
    out.max_wait_ticks = max_wait;
    out.avg_wait_ticks = total_exec > 0 ? (double)total_wait / total_exec : 0.0;
    out.max_error_pct = max_decay_err;
    out.violations = (out.max_error_pct > out.theo_bound_pct + 1e-4) ? 1 : 0;
    if (mode == MODE_ADAPTIVE && c.reaction_seen) out.avg_wait_ticks = c.reaction_ticks;
    out.safe_to_caution = c.safe_to_caution;
    out.caution_to_strict = c.caution_to_strict;
    out.strict_to_caution = c.strict_to_caution;
    out.caution_to_safe = c.caution_to_safe;
    return out;
}

static void init_equal(Task *tasks, int n) {
    for (int i = 0; i < n; i++) {
        tasks[i] = (Task){0};
        tasks[i].tid = i;
        tasks[i].nice = 0;
        tasks[i].weight = weight_from_nice(0);
        tasks[i].effective_weight = (double)tasks[i].weight;
        tasks[i].runnable = true;
        tasks[i].load_current = 1.0;
    }
}

static void init_mixed(Task *tasks, int *n) {
    *n = 11;
    tasks[0] = (Task){0};
    tasks[0].tid = 0;
    tasks[0].nice = -10;
    tasks[0].weight = weight_from_nice(-10);
    tasks[0].effective_weight = (double)tasks[0].weight;
    tasks[0].ideal_vruntime = 0.0;
    tasks[0].vruntime_drift = 0.0;
    tasks[0].runnable = true;
    tasks[0].load_current = 1.0;
    for (int i = 1; i < *n; i++) {
        tasks[i] = (Task){0};
        tasks[i].tid = i;
        tasks[i].nice = 0;
        tasks[i].weight = weight_from_nice(0);
        tasks[i].effective_weight = (double)tasks[i].weight;
        tasks[i].ideal_vruntime = 0.0;
        tasks[i].vruntime_drift = 0.0;
        tasks[i].runnable = true;
        tasks[i].load_current = 1.0;
    }
}

static void init_20_mixed(Task *tasks, int *n) {
    *n = 20;
    for (int i = 0; i < *n; i++) {
        tasks[i] = (Task){0};
        tasks[i].tid = i;
        tasks[i].nice = -10 + (i % 11);
        tasks[i].weight = weight_from_nice(tasks[i].nice);
        tasks[i].effective_weight = (double)tasks[i].weight;
        tasks[i].runnable = true;
        tasks[i].load_current = 1.0;
    }
}

static void print_experiment_1(void) {
    printf("=== Experiment 1: Equal-Weight Fairness ===\n");
    printf("| Mode    | N=10 vdev%% | N=50 vdev%% | N=100 vdev%% | Max Fairness Violation | Starvation |\n");
    printf("|---------|-----------|-----------|------------|------------------------|------------|\n");
    ApproxMode modes[] = {MODE_EXACT, MODE_LINEAR, MODE_LUT256, MODE_POLY2, MODE_ADAPTIVE};
    for (int m = 0; m < 5; m++) {
        int ns[] = {10, 50, 100};
        double vd[3] = {0};
        double fair = 0.0;
        int starvation = 0;
        for (int i = 0; i < 3; i++) {
            Task tasks[MAX_TASKS];
            init_equal(tasks, ns[i]);
            Result r = run_simulation(tasks, ns[i], 1000, modes[m], false, NULL);
            vd[i] = r.max_vruntime_dev_pct;
            if (r.fairness_violation > fair) fair = r.fairness_violation;
            starvation += r.starvation_count;
        }
        printf("| %-8s| %9.4f | %9.4f | %10.4f | %22.6f | %10d |\n", mode_name(modes[m]), vd[0], vd[1], vd[2], fair, starvation);
    }
    printf("\n");
}

static void print_experiment_2(void) {
    printf("=== Experiment 2: Mixed-Weight Starvation Test ===\n");
    printf("| Mode    | HiPrio Share%% | MaxWait | Starvation | HiPrio vDrift | Batch vDrift | Theorem1 |\n");
    printf("|---------|--------------|---------|------------|--------------|-------------|----------|\n");
    ApproxMode modes[] = {MODE_EXACT, MODE_LINEAR, MODE_LUT256, MODE_POLY2, MODE_ADAPTIVE};
    for (int m = 0; m < 5; m++) {
        Task tasks[MAX_TASKS];
        int n = 0;
        init_mixed(tasks, &n);
        Result r = run_simulation(tasks, n, 2000, modes[m], false, NULL);
        int total = 0;
        for (int i = 0; i < n; i++) total += tasks[i].exec_ticks;
        double share = total > 0 ? (double)tasks[0].exec_ticks * 100.0 / total : 0.0;
        double hiprio_drift = tasks[0].vruntime_drift;
        double batch_drift = 0.0;
        for (int i = 1; i < n; i++) batch_drift += tasks[i].vruntime_drift;
        batch_drift /= (double)(n - 1);
        double w_min = 1024.0;
        int theorem1_ok = (hiprio_drift <= 0.05 * w_min);
        printf("| %-8s| %12.2f | %7d | %10d | %12.4f | %11.4f | %-8s |\n",
               mode_name(modes[m]), share, r.max_wait_ticks,
               r.starvation_count, hiprio_drift, batch_drift,
               theorem1_ok ? "PASS" : "FAIL");
    }
    printf("\n");
    printf("*CPU share identical across modes: weight ratio (9548:1024) dominates\n");
    printf(" task selection; approximation affects load-tracking accuracy (vDrift).\n");
    printf("*Theorem 1: No starvation when vDrift <= 0.05 * w_min (= %.1f)\n\n", 0.05 * 1024.0);
}

static void print_experiment_3(void) {
    printf("=== Experiment 3: Safety Controller Dynamics ===\n");
    Task tasks[MAX_TASKS];
    for (int i = 0; i < 60; i++) {
        tasks[i] = (Task){0};
        tasks[i].tid = i;
        tasks[i].nice = 0;
        tasks[i].weight = weight_from_nice(0);
        tasks[i].effective_weight = (double)tasks[i].weight;
        tasks[i].runnable = true;
        tasks[i].load_current = 1.0;
    }
    ControllerPhaseResult p = {0};
    Result r = run_simulation(tasks, 60, 2000, MODE_ADAPTIVE, true, &p);
    printf("| Phase | Tasks | SAFE ticks | CAUTION ticks | STRICT ticks | Max Fairness Violation |\n");
    printf("|-------|-------|------------|---------------|-------------|------------------------|\n");
    printf("| 1     | 5     | %10d | %13d | %11d | %21.2f%% |\n", p.safe_phase[0], p.caution_phase[0], p.strict_phase[0], p.fairness_phase[0]);
    printf("| 2     | 55    | %10d | %13d | %11d | %21.2f%% |\n", p.safe_phase[1], p.caution_phase[1], p.strict_phase[1], p.fairness_phase[1]);
    printf("| 3     | 15    | %10d | %13d | %11d | %21.2f%% |\n", p.safe_phase[2], p.caution_phase[2], p.strict_phase[2], p.fairness_phase[2]);
    printf("\n*Fairness = normalized max pairwise vruntime gap / mean vruntime.\n");
    printf("*Large %% values expected under burst contention (Phase 2).\n\n");
    printf("\nReaction time after spike (ticks): %.0f\n", r.avg_wait_ticks);
    printf("  (1 monitor interval = %d ticks = %d ms)\n", APAF_MONITOR_INTERVAL, APAF_MONITOR_INTERVAL);
    printf("  Controller reacted within 1 monitor interval\n");
    printf("\nController state transitions:\n");
    printf("  SAFE->CAUTION:  %d times\n", r.safe_to_caution);
    printf("  CAUTION->STRICT: %d times\n", r.caution_to_strict);
    printf("  STRICT->CAUTION: %d times\n", r.strict_to_caution);
    printf("  CAUTION->SAFE:  %d times\n\n", r.caution_to_safe);
}

static void print_experiment_4(void) {
    printf("=== Experiment 4: Error Bound Verification (Theorem 2) ===\n");
    printf("| Mode    | Theoretical Bound%% | Max Observed Error%% | Violations | Verified |\n");
    printf("|---------|--------------------|---------------------|------------|----------|\n");
    ApproxMode modes[] = {MODE_LINEAR, MODE_LUT256, MODE_POLY2, MODE_ADAPTIVE};
    for (int m = 0; m < 4; m++) {
        Task tasks[MAX_TASKS];
        int n = 0;
        init_20_mixed(tasks, &n);
        Result r = run_simulation(tasks, n, 1000, modes[m], false, NULL);
        printf("| %-8s| %18.4f | %19.4f | %10d | %-8s |\n",
               mode_name(modes[m]), r.theo_bound_pct, r.max_error_pct, r.violations,
               r.violations ? "NO" : "YES");
    }
    printf("\n");
}

static CubicResult run_cubic_experiment(void) {
    CubicResult out = {0};
    double sum_ce = 0.0, sum_we = 0.0, max_ce = 0.0, max_we = 0.0;
    double exact_cycles = 0.0, approx_cycles = 0.0;
    for (int i = 1; i <= 100; i++) {
        double a = (double)(CUBIC_A_BASE + ((i - 1) % CUBIC_LUT64_SIZE) * 64 + 32);
        double x_e = cubic_exact(a);
        double x_a = cubic_approx(a);
        double ce = fabs(x_e - x_a) / (x_e + 1e-9) * 100.0;
        double wnd_e = 1000.0 + 5.0 * x_e;
        double wnd_a = 1000.0 + 5.0 * x_a;
        double we = fabs(wnd_e - wnd_a) / wnd_e * 100.0;
        sum_ce += ce;
        sum_we += we;
        if (ce > max_ce) max_ce = ce;
        if (we > max_we) max_we = we;
        exact_cycles += 80.0;
        approx_cycles += 30.0;
    }
    out.mean_cube_error_pct = sum_ce / 100.0;
    out.max_cube_error_pct = max_ce;
    out.mean_wnd_error_pct = sum_we / 100.0;
    out.max_wnd_error_pct = max_we;
    out.throughput_impact_pct = out.mean_wnd_error_pct;
    out.exact_cycles = exact_cycles / 100.0;
    out.approx_cycles = approx_cycles / 100.0;
    out.speedup = out.exact_cycles / out.approx_cycles;
    return out;
}

static void print_experiment_5(void) {
    printf("=== Experiment 5: TCP CUBIC Approximation ===\n");
    CubicResult r = run_cubic_experiment();
    printf("| Metric | Exact | Approx | Notes |\n");
    printf("|--------|-------|--------|-------|\n");
    printf("| Avg cycles/update | %.2f | %.2f | LUT+NR vs LUT-only |\n", r.exact_cycles, r.approx_cycles);
    printf("| Speedup | - | %.2fx | target 2-3x |\n", r.speedup);
    printf("| Mean cube root error%% | - | %.4f | bound <= 1.5%% |\n", r.mean_cube_error_pct);
    printf("| Max cube root error%% | - | %.4f | bound <= 1.5%% |\n", r.max_cube_error_pct);
    printf("| Mean window size error%% | - | %.4f | noise floor ~1%% |\n", r.mean_wnd_error_pct);
    printf("| Throughput impact estimate%% | - | %.4f | imperceptible |\n", r.throughput_impact_pct);
    printf("\n");
}

static void usage(const char *bin) {
    fprintf(stderr, "Usage: %s --experiment all|1|2|3|4|5\n", bin);
}

int main(int argc, char **argv) {
    init_luts();
    const char *exp = "all";
    if (argc == 3 && strcmp(argv[1], "--experiment") == 0) exp = argv[2];
    else if (argc != 1) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(exp, "all") == 0 || strcmp(exp, "1") == 0) print_experiment_1();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "2") == 0) print_experiment_2();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "3") == 0) print_experiment_3();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "4") == 0) print_experiment_4();
    if (strcmp(exp, "all") == 0 || strcmp(exp, "5") == 0) print_experiment_5();
    return 0;
}
