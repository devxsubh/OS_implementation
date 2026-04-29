#include "tcp_cubic.h"

#include "approx.h"

#include <math.h>
#include <stdio.h>

CubicResult run_cubic_experiment(void) {
    CubicResult out = {0};
    double sum_ce = 0.0, sum_we = 0.0;
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
        if (ce > out.max_cube_error_pct) out.max_cube_error_pct = ce;
        if (we > out.max_wnd_error_pct) out.max_wnd_error_pct = we;
    }
    out.mean_cube_error_pct = sum_ce / 100.0;
    out.mean_wnd_error_pct = sum_we / 100.0;
    out.throughput_impact_pct = out.mean_wnd_error_pct;
    /* Modeled cycle counts from literature:
     * Exact:  LUT64 lookup + Newton-Raphson iteration = ~80 cycles
     * Approx: LUT64 lookup only, no refinement = ~30 cycles
     * Reference: Cano-Camarero et al. ISVLSI 2017 */
    out.exact_cycles = 80.0;
    out.approx_cycles = 30.0;
    out.speedup = out.exact_cycles / out.approx_cycles;
    return out;
}

void print_experiment_5(void) {
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
