#include "approx.h"

#include <math.h>

static const int nice_to_weight[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
    9548,  7620,  6100,  4904,  3906,  3121,  2501,  2008,  1586,  1277,
    1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
    110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

double decay_lut256[LUT256_SIZE];
double cubic_lut64[CUBIC_LUT64_SIZE];

int weight_from_nice(int nice) {
    int idx = nice + 20;
    if (idx < 0) idx = 0;
    if (idx > 39) idx = 39;
    return nice_to_weight[idx];
}

void init_luts(void) {
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

const char *mode_name(ApproxMode mode) {
    switch (mode) {
        case MODE_EXACT: return "EXACT";
        case MODE_LINEAR: return "LINEAR";
        case MODE_LUT256: return "LUT256";
        case MODE_POLY2: return "POLY2";
        case MODE_ADAPTIVE: return "ADAPTIVE";
        default: return "UNKNOWN";
    }
}

double exact_decay(double seconds) { return exp(-DECAY_LAMBDA * seconds); }

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

double mode_decay(ApproxMode mode, SafetyState state, double seconds) {
    if (mode == MODE_ADAPTIVE) {
        if (state == STATE_SAFE)    return poly2_decay(seconds);
        if (state == STATE_CAUTION) return lut256_decay(seconds);
        return exact_decay(seconds);
    }
    if (mode == MODE_LINEAR) return linear_decay(seconds);
    if (mode == MODE_LUT256) return lut256_decay(seconds);
    if (mode == MODE_POLY2)  return poly2_decay(seconds);
    return exact_decay(seconds);
}

double cubic_exact(double a) {
    int idx = (int)((a - CUBIC_A_BASE) / 64.0);
    if (idx < 0) idx = 0;
    if (idx >= CUBIC_LUT64_SIZE) idx = CUBIC_LUT64_SIZE - 1;
    double x = cubic_lut64[idx];
    return (2.0 * x + a / (x * x)) / 3.0;
}

double cubic_approx(double a) {
    int idx = (int)((a - CUBIC_A_BASE) / 64.0);
    if (idx < 0) idx = 0;
    if (idx >= CUBIC_LUT64_SIZE) idx = CUBIC_LUT64_SIZE - 1;
    return cubic_lut64[idx];
}

void update_controller(Controller *c, double fairness_violation, double w_min, int tick) {
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
