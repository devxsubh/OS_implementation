# APEX Simulator (Phase 1 Research)

This project is a research simulator for approximate computing in OS-kernel internals.

It models two subsystems:

- Linux CFS scheduler load-tracking decay (`e^(-lambda t)`) approximations
- TCP CUBIC cube-root approximations used in window update flow

The simulator is provided in two forms:

- Single-file: `apex_sim.c`
- Modular: `src/*.c` + `include/*.h`

## Theorem Mapping

- **Theorem 1 (starvation prevention)**: controller keeps approximation strictness bounded by fairness thresholds tied to `w_min`.
  - fairness metric = maximum pairwise vruntime gap
  - starvation monitor = no task should exceed `2 x scheduling_period` equivalent threshold
- **Theorem 2 (cumulative error bound)**: Experiment 4 validates per-tick decay approximation errors against theoretical bounds for each mode.

## Approximation Modes

- `EXACT`: exact decay and exact CUBIC (LUT + Newton step)
- `LINEAR`: `1 - lambda t` for scheduler decay
- `LUT256`: 256-entry scheduler decay lookup
- `POLY2`: `1 - lambda t + (lambda t)^2/2` via Horner form
- `ADAPTIVE`: safety controller state machine:
  - `SAFE` -> decay=`POLY2`, CUBIC=`LUT-only`
  - `CAUTION` -> decay=`LUT256`, CUBIC=`LUT-only`
  - `STRICT` -> exact decay and exact CUBIC

Controller monitor interval is **8 ticks** with transitions:

- `SAFE -> CAUTION` when fairness violation `> 0.05 * w_min`
- `CAUTION -> STRICT` when fairness violation `> 0.10 * w_min`
- `CAUTION -> SAFE` when fairness violation `< 0.02 * w_min`
- `STRICT -> CAUTION` when fairness violation `< 0.04 * w_min`

## Experiments

- **Experiment 1**: Equal-weight fairness (`N=10, 50, 100`)
- **Experiment 2**: Mixed-weight starvation test (`nice=-10` task + 10 `nice=0` tasks)
- **Experiment 3**: Safety controller dynamics across phased load
- **Experiment 4**: Error bound verification (Theorem 2)
- **Experiment 5**: TCP CUBIC approximation (unique contribution)

All experiment outputs are printed as markdown tables.

## Current State Snapshot (Latest Run)

The latest exported report (`apex_outputs_for_claude.json`) was generated from:

- build command: `make all`
- run command: `./apex_sim_modular --experiment all`
- build status: success (`returncode = 0`)
- run status: success (`returncode = 0`)

Observed behavior summary:

- **Experiment 1**
  - all modes show very small `vdev%` values (roughly `0.0014` to `0.0038`)
  - `ADAPTIVE` currently matches `LUT256` in this workload (not `EXACT`)
- **Experiment 2**
  - modes are non-identical
  - `ADAPTIVE` is currently close to `EXACT`
  - no starvation events observed
- **Experiment 3**
  - controller reacts quickly (`4` ticks)
  - transitions observed in all key directions (`SAFE->CAUTION`, `CAUTION->STRICT`, `STRICT->CAUTION`, `CAUTION->SAFE`)
  - fairness values are printed as normalized percentages and can be large under burst contention
- **Experiment 4**
  - all modes pass theoretical bound checks (`Verified = YES`)
  - no violations observed
- **Experiment 5**
  - CUBIC approximation remains strong and stable
  - reported speedup: `2.67x`

Important interpretation note:

- In Experiment 3, the printed fairness numbers are **normalized max pairwise vruntime gap vs mean vruntime** (not CPU share percentages), so high values during heavy contention are expected.

## File Structure

```text
.
├── apex_sim.c
├── src/
│   ├── main.c
│   ├── approx.c
│   ├── sim.c
│   ├── experiments.c
│   └── tcp_cubic.c
├── include/
│   ├── apex_types.h
│   ├── approx.h
│   ├── sim.h
│   ├── experiments.h
│   └── tcp_cubic.h
├── Makefile
├── apex_analysis.ipynb
└── README.md
```

## Build and Run

```bash
make single
make modular
make all
make run
make clean
```

CLI:

```bash
./apex_sim --experiment all
./apex_sim --experiment 1
./apex_sim --experiment 5
```

Modular binary:

```bash
./apex_sim_modular --experiment all
```

## Python Analysis Notebook

Notebook: `apex_analysis.ipynb`

It:

- runs `make all`
- captures simulator output
- parses markdown tables into pandas DataFrames
- creates requested plots:
  - CPU share by mode (Experiment 2)
  - error over ticks / bound summary (Experiment 4)
  - cube-root vs window-size error (Experiment 5)
  - adaptive state distribution by phase (Experiment 3)
- reports statistical summary and timing (wall + CPU)

Use `.venv` with `pandas numpy matplotlib jupyter`.

## Architecture Overview

There are two equivalent implementations:

- Single-file implementation: `apex_sim.c`
- Modular implementation: `src/*.c` with declarations in `include/*.h`

Both produce the same experiment tables and are kept functionally aligned.

Execution flow:

1. `main` parses CLI argument (`--experiment all|1|2|3|4|5`)
2. `init_luts()` prepares approximation lookup tables
3. Selected experiment function initializes workload
4. `run_simulation()` executes scheduler ticks and computes metrics
5. Experiment function prints markdown table rows

---

## File-by-File Guide

### Top-level files

- `README.md`: project and experiment documentation
- `Makefile`: build targets for single/modular binaries
- `apex_sim.c`: monolithic all-in-one simulator
- `apex_analysis.ipynb`: analysis notebook (build, parse outputs, plots, JSON export)
- `apex_outputs_for_claude.json`: generated report artifact from notebook

### `include/` headers

- `include/apex_types.h`
  - Core constants and shared structs/enums:
    - modes (`MODE_EXACT`, `MODE_LINEAR`, `MODE_LUT256`, `MODE_POLY2`, `MODE_ADAPTIVE`)
    - controller states (`SAFE`, `CAUTION`, `STRICT`)
    - scheduler task struct (`Task`)
    - result structs (`Result`, `ControllerPhaseResult`, `CubicResult`)
  - Key constants include:
    - `APAF_MONITOR_INTERVAL = 8`
    - `DECAY_LAMBDA = 0.693`
    - `TICK_SECONDS = 0.032`
- `include/approx.h`
  - Approximation and controller APIs (`mode_decay`, `update_controller`, CUBIC helpers)
- `include/sim.h`
  - Simulation engine APIs (`init_equal`, `init_mixed`, `run_simulation`)
- `include/experiments.h`
  - Experiment print entry points (`print_experiment_1` ... `print_experiment_5`)
- `include/tcp_cubic.h`
  - CUBIC experiment API declarations

### `src/` source files

- `src/main.c`
  - CLI entry point and experiment routing
- `src/approx.c`
  - Decay approximations (linear/LUT/poly2/exact)
  - LUT initialization and quantization
  - Adaptive controller transition logic and counters
- `src/sim.c`
  - Tick-level scheduler simulation, task selection, vruntime updates
  - Fairness/starvation/latency/error metrics
  - Phase transitions and controller monitoring
- `src/experiments.c`
  - Experiment setup and markdown table output formatting
- `src/tcp_cubic.c`
  - TCP CUBIC approximation experiment and reporting

---

## Key Implementation Snippets

### 1) Decay LUT quantization (intentional approximation)

From `src/approx.c`:

```c
float q = (float)exp(-DECAY_LAMBDA * t);
q = floorf(q * 10000.0f + 0.5f) / 10000.0f;
decay_lut256[i] = (double)q;
```

Why: ensures LUT256 is not accidentally exact, producing realistic nonzero observed error in Experiment 4.

### 2) Adaptive mode decay selection

From `src/approx.c`:

```c
if (mode == MODE_ADAPTIVE) {
    if (state == STATE_SAFE) return poly2_decay(seconds);
    if (state == STATE_CAUTION) return lut256_decay(seconds);
    return exact_decay(seconds);
}
```

Why: adaptive controller trades speed for strictness based on fairness conditions.

### 3) Controller thresholds and transitions

From `src/approx.c`:

```c
double t1 = 0.05 * w_min;
double t2 = 0.10 * w_min;
double t3 = 0.02 * w_min;
double t4 = 0.04 * w_min;
```

And transitions:

```c
if (c->state == STATE_SAFE && fairness_violation > t1) c->state = STATE_CAUTION;
else if (c->state == STATE_CAUTION && fairness_violation > t2) c->state = STATE_STRICT;
else if (c->state == STATE_CAUTION && fairness_violation < t3) c->state = STATE_SAFE;
else if (c->state == STATE_STRICT && fairness_violation < t4) c->state = STATE_CAUTION;
```

Why: enforces Theorem 1-inspired safety bounds.

### 4) Load-aware effective weight and vruntime update

From `src/sim.c`:

```c
tasks[i].load_avg = tasks[i].load_avg * d + tasks[i].load_current * (1.0 - d);
double load_factor = 1.0 + (tasks[i].load_avg - 0.5) * 0.02;
tasks[i].effective_weight = (double)tasks[i].weight * load_factor;
...
tasks[chosen].vruntime += (32.0 * 1024.0) / weight_for_vruntime;
```

Why: makes approximation mode affect scheduling behavior instead of being a dead calculation.

### 5) Phase-boundary normalization (Exp 3)

From `src/sim.c`:

```c
if (phased && (tick == 500 || tick == 1500)) {
    normalize_vruntimes(tasks, n);
}
```

And normalization:

```c
tasks[i].vruntime -= min_vrt;
tasks[i].load_avg = 0.0;
```

Why: prevents old-phase history from dominating new-phase dynamics.

### 6) Fairness normalization for phased output

From `src/sim.c`:

```c
out.fairness_violation = (out.fairness_violation / mean_vrt) * 100.0;
```

Why: prints fairness in percentage-like normalized units for easier interpretation across phases.

### 7) Experiment 2 mixed-workload fairness metric

From `src/sim.c`:

```c
out.fairness_violation = fabs(tasks[0].vruntime - mean_batch_v) / fabs(mean_batch_v);
```

Why: compares high-priority task progress against mean batch-task progress in mixed workload.

---

## Build/Run Behavior and Common Confusion

`make` targets:

- `make single`: builds `./apex_sim`
- `make modular`: builds `./apex_sim_modular`
- `make all`: builds both
- `make run`: builds/runs only `./apex_sim --experiment all`
- `make clean`: deletes both binaries

Important:

- After `make clean`, running `./apex_sim` or `./apex_sim_modular` fails until rebuilt.
- `./apex_sim.c` is source code, not an executable.

---

## Notebook and JSON Report Workflow

`apex_analysis.ipynb`:

1. Uses project root dynamically (`Path.cwd()` fallback-safe)
2. Runs `make all` and simulator commands
3. Parses markdown tables into pandas DataFrames
4. Builds summary statistics and plots
5. Exports full artifact as `apex_outputs_for_claude.json`

The JSON includes:

- build metadata
- full `--experiment all` stdout/stderr
- parsed tables per experiment
- per-experiment timing
- numeric summary stats
- raw output for each experiment invocation

---

## What Was Updated During This Iteration

- Path robustness:
  - notebook root handling no longer hardcoded to a brittle absolute path
  - README file tree root corrected after workspace move
- Scheduler/metrics/controller updates:
  - monitor interval constant unified as `APAF_MONITOR_INTERVAL`
  - effective load-aware weight path active in scheduler loop
  - Experiment 1 vdev metric computed from stddev/mean vruntime
  - Experiment 2 mixed-workload fairness formula uses hiprio vs batch mean
  - Experiment 3 phase-boundary vruntime/load normalization added
  - Experiment 3 state transition counters printed
  - Experiment 3 reaction-time interpretation lines added
- Error-model realism:
  - LUT256 quantization made explicit to avoid zero observed error
  - POLY2 bound used as fixed realistic value (`0.00135%`)
- JSON reporting:
  - notebook now writes a structured shareable report for external review
  - current report execution uses `./apex_sim_modular --experiment all` in the exported metadata

---

## Validation Checklist

For a clean local validation:

```bash
make clean
make all
./apex_sim --experiment all
./apex_sim_modular --experiment all
```

Then regenerate notebook outputs:

1. Run `apex_analysis.ipynb` top-to-bottom
2. Run final export cell
3. Confirm `apex_outputs_for_claude.json` updates

# OS_implementation
# OS_implementation
