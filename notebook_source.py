import subprocess
import time
import resource
import re
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

ROOT = Path.cwd()
if not (ROOT / 'Makefile').exists() and (ROOT / 'apex_sim' / 'Makefile').exists():
    ROOT = ROOT / 'apex_sim'

plt.style.use('ggplot')
---
def run_cmd(cmd):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    wall = time.perf_counter() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)
    return {
        'cmd': ' '.join(cmd),
        'returncode': proc.returncode,
        'stdout': proc.stdout,
        'stderr': proc.stderr,
        'wall_s': wall,
        'cpu_s': cpu,
    }

build = run_cmd(['make', 'all'])
assert build['returncode'] == 0, build['stderr']
all_out = run_cmd(['./apex_sim', '--experiment', 'all'])
assert all_out['returncode'] == 0, all_out['stderr']
print(f"Build+run wall={build['wall_s']+all_out['wall_s']:.3f}s cpu={build['cpu_s']+all_out['cpu_s']:.3f}s")
---
def parse_md_tables(text):
    lines = text.splitlines()
    out = {}
    i = 0
    while i < len(lines):
        if lines[i].startswith('=== ') and lines[i].endswith(' ==='):
            title = lines[i].strip('= ').strip()
            j = i + 1
            while j < len(lines) and not lines[j].startswith('|'):
                j += 1
            if j + 1 < len(lines) and lines[j+1].startswith('|'):
                block = []
                k = j
                while k < len(lines) and lines[k].startswith('|'):
                    block.append(lines[k])
                    k += 1
                cols = [c.strip().replace('%','') for c in block[0].strip('|').split('|')]
                rows = [[c.strip().replace('%','') for c in r.strip('|').split('|')] for r in block[2:]]
                df = pd.DataFrame(rows, columns=cols)
                for c in df.columns:
                    try:
                        df[c] = pd.to_numeric(df[c])
                    except Exception:
                        pass
                out[title] = df
                i = k
                continue
        i += 1
    return out

tables = parse_md_tables(all_out['stdout'])
for k, v in tables.items():
    print(k)
    display(v.head())
---
# Plot 1: CPU share% by mode (Experiment 2)
exp2 = tables['Experiment 2: Mixed-Weight Starvation Test'].copy()
plt.figure(figsize=(8,4))
plt.bar(exp2['Mode'], exp2['HighPrio CPU Share'])
plt.ylabel('HighPrio CPU Share (%)')
plt.title('Experiment 2: CPU Share by Mode')
plt.show()
---
# Plot 2: Error% over ticks for each mode (Experiment 4 style)
lam = 0.693
t = 0.032
ticks = np.arange(1, 1001)
sec = ticks * t
exact = np.exp(-lam*t)

linear = np.clip(1 - lam*t, 0, 1)
poly2 = np.clip(1 - lam*t + 0.5*(lam*t)**2, 0, 1)
lut256 = np.exp(-lam*t)

err_linear = np.full_like(ticks, np.abs(exact-linear)/exact*100, dtype=float)
err_lut = np.full_like(ticks, np.abs(exact-lut256)/exact*100, dtype=float)
err_poly2 = np.full_like(ticks, np.abs(exact-poly2)/exact*100, dtype=float)

plt.figure(figsize=(9,4))
plt.plot(ticks, err_linear, label='LINEAR')
plt.plot(ticks, err_lut, label='LUT256')
plt.plot(ticks, err_poly2, label='POLY2')
plt.ylabel('Decay Error (%)')
plt.xlabel('Tick')
plt.title('Experiment 4: Error% Over Ticks')
plt.legend()
plt.show()
---
# Plot 3: Cube root error vs window size error (Experiment 5)
exp5 = tables['Experiment 5: TCP CUBIC Approximation'].copy()
subset = exp5[exp5['Metric'].isin(['Mean cube root error', 'Mean window size error'])].copy()
plt.figure(figsize=(6,4))
plt.bar(subset['Metric'], subset['Approx'])
plt.ylabel('Error (%)')
plt.title('Experiment 5: CUBIC Error Comparison')
plt.xticks(rotation=20)
plt.show()
---
# Plot 4: Safety controller state distribution per phase (Experiment 3)
exp3 = tables['Experiment 3: Safety Controller Dynamics'].copy()
labels = exp3['Phase'].astype(str)
width = 0.25
x = np.arange(len(labels))

plt.figure(figsize=(8,4))
plt.bar(x - width, exp3['SAFE ticks'], width=width, label='SAFE')
plt.bar(x, exp3['CAUTION ticks'], width=width, label='CAUTION')
plt.bar(x + width, exp3['STRICT ticks'], width=width, label='STRICT')
plt.xticks(x, [f'Phase {p}' for p in labels])
plt.ylabel('Ticks')
plt.title('Experiment 3: Controller State Distribution')
plt.legend()
plt.show()
---
# Table: Theorem 2 verification summary
import json

exp4 = tables['Experiment 4: Error Bound Verification (Theorem 2)'].copy()
display(exp4)

# Statistical summary for all numeric columns across all tables
numeric_frames = []
for name, df in tables.items():
    num = df.select_dtypes(include=[np.number]).copy()
    if len(num.columns) > 0:
        num['table'] = name
        numeric_frames.append(num)

summary = pd.concat(numeric_frames, ignore_index=True)
print('Numeric summary (mean/std/max):')
display(summary.describe().loc[['mean','std','max']])

# Per-experiment timing and raw outputs
timings = []
experiment_runs = {}
for exp in ['1', '2', '3', '4', '5']:
    r = run_cmd(['./apex_sim', '--experiment', exp])
    timings.append({'experiment': exp, 'wall_s': r['wall_s'], 'cpu_s': r['cpu_s']})
    experiment_runs[exp] = {
        'returncode': int(r['returncode']),
        'wall_s': float(r['wall_s']),
        'cpu_s': float(r['cpu_s']),
        'stdout': r['stdout'],
        'stderr': r['stderr'],
    }

timings_df = pd.DataFrame(timings)
display(timings_df)

# Export everything needed for Claude analysis
def _to_native(value):
    if isinstance(value, (np.integer, np.floating)):
        return value.item()
    return value

payload = {
    'project_root': str(ROOT),
    'build': {
        'cmd': build['cmd'],
        'returncode': int(build['returncode']),
        'wall_s': float(build['wall_s']),
        'cpu_s': float(build['cpu_s']),
        'stdout': build['stdout'],
        'stderr': build['stderr'],
    },
    'all_run': {
        'cmd': all_out['cmd'],
        'returncode': int(all_out['returncode']),
        'wall_s': float(all_out['wall_s']),
        'cpu_s': float(all_out['cpu_s']),
        'stdout': all_out['stdout'],
        'stderr': all_out['stderr'],
    },
    'timings': timings,
    'tables': {
        name: [
            {k: _to_native(v) for k, v in row.items()}
            for row in df.to_dict(orient='records')
        ]
        for name, df in tables.items()
    },
    'numeric_summary_mean_std_max': [
        {k: _to_native(v) for k, v in row.items()}
        for row in summary.describe().loc[['mean', 'std', 'max']].reset_index().rename(columns={'index': 'metric'}).to_dict(orient='records')
    ],
    'experiment_runs': experiment_runs,
}

output_path = ROOT / 'apex_outputs_for_claude.json'
output_path.write_text(json.dumps(payload, indent=2), encoding='utf-8')
print(f'Saved JSON report to: {output_path}')
