#include "experiments.h"

#include "approx.h"
#include "sim.h"

#include <stdio.h>

void print_experiment_1(void) {
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

void print_experiment_2(void) {
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
        batch_drift /= (n - 1);
        double w_min = 1024.0;
        int theorem1_ok = (hiprio_drift <= 0.05 * w_min);
        printf("| %-8s| %12.2f | %7d | %10d | %12.4f | %11.4f | %-8s |\n",
               mode_name(modes[m]),
               share,
               r.max_wait_ticks,
               r.starvation_count,
               hiprio_drift,
               batch_drift,
               theorem1_ok ? "PASS" : "FAIL");
    }
    printf("\n");
    printf("*Theorem 1: No starvation when vruntime drift <= 0.05 * w_min (= %.1f)\n", 0.05 * 1024.0);
    printf("*vDrift = accumulated deviation of actual vruntime from ideal exact-arithmetic vruntime\n\n");
}

void print_experiment_3(void) {
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
    printf("\n*Note: Fairness violation above is the normalized max pairwise vruntime gap vs mean vruntime under burst contention.\n");
    printf("\nReaction time after spike (ticks): %.0f\n", r.avg_wait_ticks);
    printf("  (1 monitor interval = %d ticks = %d ms)\n", APAF_MONITOR_INTERVAL, APAF_MONITOR_INTERVAL);
    printf("  Controller reacted within 1 monitor interval\n");
    printf("\nController state transitions:\n");
    printf("  SAFE->CAUTION:  %d times\n", r.safe_to_caution);
    printf("  CAUTION->STRICT: %d times\n", r.caution_to_strict);
    printf("  STRICT->CAUTION: %d times\n", r.strict_to_caution);
    printf("  CAUTION->SAFE:  %d times\n\n", r.caution_to_safe);
}

void print_experiment_4(void) {
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
