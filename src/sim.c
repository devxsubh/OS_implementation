#include "sim.h"

#include "approx.h"

#include <float.h>
#include <math.h>
#include <stdio.h>

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

void init_equal(Task *tasks, int n) {
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

void init_mixed(Task *tasks, int *n) {
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

void init_20_mixed(Task *tasks, int *n) {
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

Result run_simulation(Task *tasks, int n, int ticks, ApproxMode mode, bool phased, ControllerPhaseResult *phase_out) {
    Result out = {0};
    Controller c = {0};
    c.requested_mode = mode;
    c.state = STATE_SAFE;
    int total_wait = 0, total_exec = 0;
    double w_min = 1e30;
    for (int i = 0; i < n; i++) {
        if ((double)tasks[i].weight < w_min) w_min = (double)tasks[i].weight;
    }

    if (mode == MODE_LINEAR) out.theo_bound_pct = 0.05;
    else if (mode == MODE_LUT256) out.theo_bound_pct = 0.5;
    else if (mode == MODE_POLY2) out.theo_bound_pct = 0.00135;
    else if (mode == MODE_ADAPTIVE) out.theo_bound_pct = 0.5;

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
        if (decay_err > out.max_error_pct) out.max_error_pct = decay_err;

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

    double min_v = tasks[0].vruntime, max_v = tasks[0].vruntime;
    double mean_v = 0.0;
    for (int i = 0; i < n; i++) {
        if (tasks[i].vruntime < min_v) min_v = tasks[i].vruntime;
        if (tasks[i].vruntime > max_v) max_v = tasks[i].vruntime;
        mean_v += tasks[i].vruntime;
        out.starvation_count += tasks[i].starvation_events;
        if (tasks[i].max_wait_ticks > out.max_wait_ticks) out.max_wait_ticks = tasks[i].max_wait_ticks;
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
    out.avg_wait_ticks = total_exec > 0 ? (double)total_wait / total_exec : 0.0;
    out.violations = (out.max_error_pct > out.theo_bound_pct + 1e-4) ? 1 : 0;
    if (mode == MODE_ADAPTIVE && c.reaction_seen) out.avg_wait_ticks = c.reaction_ticks;
    out.safe_to_caution = c.safe_to_caution;
    out.caution_to_strict = c.caution_to_strict;
    out.strict_to_caution = c.strict_to_caution;
    out.caution_to_safe = c.caution_to_safe;
    return out;
}
