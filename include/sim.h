#ifndef SIM_H
#define SIM_H

#include "apex_types.h"

void init_equal(Task *tasks, int n);
void init_mixed(Task *tasks, int *n);
void init_20_mixed(Task *tasks, int *n);
Result run_simulation(Task *tasks, int n, int ticks, ApproxMode mode, bool phased, ControllerPhaseResult *phase_out);

#endif
