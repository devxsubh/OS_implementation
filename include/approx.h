#ifndef APPROX_H
#define APPROX_H

#include "apex_types.h"

extern double decay_lut256[LUT256_SIZE];
extern double cubic_lut64[CUBIC_LUT64_SIZE];

void init_luts(void);
int weight_from_nice(int nice);
const char *mode_name(ApproxMode mode);
double exact_decay(double seconds);
double mode_decay(ApproxMode mode, SafetyState state, double seconds);
double cubic_exact(double a);
double cubic_approx(double a);
void update_controller(Controller *c, double fairness_violation, double w_min, int tick);

#endif
