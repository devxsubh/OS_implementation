\documentclass[conference]{IEEEtran}
\IEEEoverridecommandlockouts
\usepackage{cite}
\usepackage{amsmath,amssymb,amsfonts}
\usepackage{algorithmic}
\usepackage{graphicx}
\usepackage{textcomp}
\usepackage{xcolor}
\def\BibTeX{{\rm B\kern-.05em{\sc i\kern-.025em b}\kern-.08em
    T\kern-.1667em\lower.7ex\hbox{E}\kern-.125emX}}
\begin{document}

\title{APEX-CFS Phase 2: Adaptive Approximation Strategies for CFS-Inspired Scheduler Arithmetic}

\author{\IEEEauthorblockN{Divyanshi Sachan}
\IEEEauthorblockA{\textit{Rishihood University}\\
divyanshi.s23csai@nst.rishihood.edu.in}
\and
\IEEEauthorblockN{Subham Mahapatra}
\IEEEauthorblockA{\textit{Rishihood University}\\
subham.m23csai@nst.rishihood.edu.in}
\and
\IEEEauthorblockN{Manasa Chinnam}
\IEEEauthorblockA{\textit{Rishihood University}\\
manasa.c23csai@nst.rishihood.edu.in}
}

\maketitle

\begin{abstract}
This report presents Phase 2 of APEX-CFS, a userspace simulation study of approximation-aware scheduler arithmetic inspired by Linux CFS load tracking and weighted virtual runtime behavior. We evaluate five computation modes---EXACT, LINEAR, LUT256, POLY2, and ADAPTIVE---across fairness, starvation, controller response, and approximation-error compliance experiments. The simulator integrates load-aware effective weight scaling, adaptive mode transitions with fairness thresholds, phase-boundary normalization for dynamic workloads, explicit theoretical error checks, and (for mixed-weight workloads) ideal-vruntime drift telemetry. In the latest run, equal-weight fairness remains stable with very low vruntime deviation (order of $10^{-3}$\%); mixed-weight runs show identical high-priority CPU share across modes while \emph{vruntime drift} versus exact-arithmetic ideal separates LINEAR and LUT256 from POLY2/ADAPTIVE; starvation counts remain zero and Theorem~1 drift checks pass under the stated threshold. Experiment~3 reports phase fairness as a \emph{time average} of the normalized pairwise gap metric, yielding 3.36\%, 555.32\%, and 92.55\% for phases 1--3; the adaptive controller reacts to the spike in 4 ticks (within one 8-tick monitor interval). All configured Theorem~2 error bounds hold with zero violations, and the CUBIC approximation path sustains a 2.67$\times$ update-speed gain with sub-0.1\% mean cube-root error. These results support controlled arithmetic approximation under explicit fairness and error guardrails.
\end{abstract}

\begin{IEEEkeywords}
approximate computing, Linux CFS, scheduler fairness, PELT, vruntime, adaptive control, error bounds, TCP CUBIC
\end{IEEEkeywords}

\section{Introduction}
Linux CFS relies on weighted virtual runtime and load-tracking style decay behavior for scheduling decisions \cite{cfsdesign,peltlwn}. These operations occur frequently and are arithmetic intensive. Approximate computing has demonstrated favorable efficiency-quality tradeoffs in many domains \cite{meantime,pliant,agrawal}, but direct scheduler-arithmetic approximation remains comparatively underexplored in practical, reproducible studies.

APEX-CFS Phase 2 studies this space through a deterministic userspace simulator with two equivalent implementations: a single-file build (\texttt{apex\_sim.c}) and a modular build (\texttt{src/*.c}), kept in parity for this release. The project focuses on whether simplified decay arithmetic can preserve fairness and starvation safety while maintaining bounded numerical error.

Contributions of this phase:
\begin{itemize}
    \item A five-mode approximation framework: EXACT, LINEAR, LUT256, POLY2, ADAPTIVE.
    \item A fairness-thresholded adaptive controller with state-transition telemetry.
    \item Mixed-weight Experiment~2 extended with ideal-vruntime drift (\texttt{vDrift}) and a Theorem~1 style drift threshold check, alongside CPU share.
    \item Phase-averaged fairness reporting for Experiment~3 (running mean per phase rather than peak hold).
    \item A phased-load experiment with reaction-time characterization.
    \item A mode-specific theoretical-vs-observed error verification pipeline.
    \item A reproducible analysis export path (`apex\_outputs\_for\_claude.json`) and notebook parsing of Markdown tables.
\end{itemize}

\section{Problem Definition and Scope}
\subsection{Task}
The task is to simulate scheduler-like arithmetic under approximation and evaluate:
\begin{enumerate}
    \item fairness behavior under equal and mixed weights,
    \item starvation safety,
    \item adaptive controller stability and responsiveness,
    \item numerical error compliance versus configured bounds,
    \item separate CUBIC approximation speed/accuracy tradeoff.
\end{enumerate}

\subsection{Scope of Phase 2}
Phase 2 is userspace-only and does not patch kernel code directly. It targets algorithmic behavior and guardrail validation as a pre-kernel stage.

\section{Methodology}
\subsection{Simulation Architecture}
Each tick performs: runnable-set update, exact/mode decay evaluation, `load\_avg` update, `effective\_weight` calculation, minimum-vruntime task selection, vruntime advance, and metric updates.

Key constants:
\begin{itemize}
    \item $\texttt{APAF\_MONITOR\_INTERVAL}=8$
    \item $\lambda=\texttt{DECAY\_LAMBDA}=0.693$
    \item $\Delta t=\texttt{TICK\_SECONDS}=0.032$
\end{itemize}

\subsection{Approximation Modes}
\begin{itemize}
    \item EXACT: baseline exponential decay.
    \item LINEAR: first-order approximation $(1-\lambda t)$.
    \item LUT256: quantized table lookup.
    \item POLY2: second-order polynomial $(1-x+x^2/2)$ with $x=\lambda t$.
    \item ADAPTIVE:
    \begin{itemize}
        \item SAFE $\rightarrow$ POLY2
        \item CAUTION $\rightarrow$ LUT256
        \item STRICT $\rightarrow$ EXACT
    \end{itemize}
\end{itemize}

\subsection{Controller and Thresholds}
Controller transitions use fairness violation against $w_{\min}$-scaled thresholds:
\begin{align}
\text{SAFE}\rightarrow\text{CAUTION} &: f > 0.05w_{\min} \\
\text{CAUTION}\rightarrow\text{STRICT} &: f > 0.10w_{\min} \\
\text{CAUTION}\rightarrow\text{SAFE} &: f < 0.02w_{\min} \\
\text{STRICT}\rightarrow\text{CAUTION} &: f < 0.04w_{\min}
\end{align}
Transition counts are logged for interpretability.

\subsection{Phase-Boundary Stabilization}
For phased experiment runs (Exp 3), at phase boundaries (ticks 500 and 1500), vruntime is normalized to runqueue minimum and `load\_avg` is reset to reduce historical carryover.

\subsection{Phase Fairness Aggregation (Experiment 3)}
Each tick computes a normalized fairness metric for the runnable set (max pairwise vruntime gap divided by mean vruntime when the mean exceeds a stability threshold). For controller phase telemetry, the simulator accumulates a running sum and count per phase and reports the \emph{arithmetic mean} over ticks in that phase, rather than the historical maximum. This better reflects typical contention intensity during long bursts.

\subsection{Ideal Vruntime Drift (Experiment 2)}
For the fixed $n{=}11$ mixed-weight workload, each scheduled task advances an \emph{ideal} vruntime as if decay were exact: per tick, $\Delta v_{\mathrm{ideal}} = (32\cdot 1024)/w_{\mathrm{static}}$ using the task's nominal CFS weight. The reported \texttt{vDrift} is $|v_{\mathrm{runtime}} - v_{\mathrm{ideal}}|$. Experiment~2 prints hiprio and mean batch drift and labels a Theorem~1 style check against $0.05\,w_{\min}$ with $w_{\min}{=}1024$ for the batch tasks.

\subsection{Mixed-Weight Decay-Window Correction}
Still for $n{=}11$, vruntime advance uses a per-task decay window $\Delta t\,(1 + 0.05\cdot \texttt{wait\_ticks\_since\_run})$; the mismatch $|e^{-\lambda t_{\mathrm{win}}} - d_{\mathrm{approx}}(t_{\mathrm{win}})|$ scales a weight perturbation (factor 32) so approximation error in load decay affects scheduling pressure in a controlled, workload-specific way.

\subsection{Error-Bound Setup}
The configured bounds used in Experiment 4 are:
\begin{itemize}
    \item LINEAR: 0.05\%
    \item LUT256: 0.5\%
    \item POLY2: 0.00135\%
    \item ADAPTIVE: 0.5\%
\end{itemize}

\section{Experimental Setup}
All binaries are compiled using GCC (`-O2 -Wall -Wextra`). The latest report snapshot is produced from:
\begin{itemize}
    \item Build: `make all` (builds both \texttt{apex\_sim} and \texttt{apex\_sim\_modular})
    \item Run: `./apex\_sim --experiment all` or `./apex\_sim\_modular --experiment all` (outputs match for the experiments tabulated here)
\end{itemize}

Experiment lengths:
\begin{itemize}
    \item Exp 1: 1000 ticks
    \item Exp 2: 2000 ticks
    \item Exp 3: 2000 ticks with phased runnable counts (5, 55, 15)
    \item Exp 4: 1000 ticks
    \item Exp 5: 100 CUBIC samples
\end{itemize}

\section{Results}
\subsection{Experiment 1: Equal-Weight Fairness}
All modes maintain low vruntime deviation and zero starvation (representative snapshot):
\begin{itemize}
    \item EXACT: N=10/50/100 vdev\% 0.0014 / 0.0025 / 0.0038; max fairness violation 0.123074
    \item LINEAR: 0.0014 / 0.0025 / 0.0038; max fairness 0.124076
    \item LUT256: 0.0019 / 0.0025 / 0.0036; max fairness 0.189590
    \item POLY2: 0.0014 / 0.0025 / 0.0038; max fairness 0.123065
    \item ADAPTIVE: 0.0014 / 0.0025 / 0.0038; max fairness 0.123065
\end{itemize}

\begin{figure}[htbp]
\centerline{\includegraphics[width=\linewidth]{apex_notebook_outputs/exp1_fairness.png}}
\caption{Experiment 1 visualization: vruntime deviation and max fairness violation across different approximation modes.}
\label{fig:exp1_fairness}
\end{figure}

\subsection{Experiment 2: Mixed-Weight Starvation}
One high-priority task (nice $-10$, weight 9548) competes with ten equal nice-0 tasks (weight 1024 each). \textbf{CPU share} is dominated by the static weight ratio: all modes report the same high-priority share (48.05\%) in the current configuration, with max wait 19 ticks and zero starvation events. \textbf{Mode separation} appears in \texttt{vDrift}---deviation of actual vruntime from the ideal exact-arithmetic trajectory:
\begin{itemize}
    \item EXACT: HiPrio vDrift 0.0000, mean batch vDrift 0.0000; Theorem~1 check PASS
    \item LINEAR: 25.9626 / 26.1730; PASS (below $0.05\cdot 1024 = 51.2$)
    \item LUT256: 3.3724 / 3.3998; PASS
    \item POLY2: 0.1908 / 0.1923; PASS
    \item ADAPTIVE: 0.1908 / 0.1923; PASS
\end{itemize}
Thus LINEAR accumulates the largest scheduling-time drift; LUT256 is intermediate; POLY2 and ADAPTIVE track the ideal path closely while ADAPTIVE retains controller headroom for other workloads.

\begin{figure}[htbp]
\centerline{\includegraphics[width=\linewidth]{apex_notebook_outputs/exp2_vdrift.png}}
\caption{Experiment 2 visualization: CPU share under skew (often flat across modes when weights dominate) and vruntime drift versus ideal EXACT arithmetic (analysis notebook).}
\label{fig:exp2_cpu_share}
\end{figure}

\subsection{Experiment 3: Adaptive Controller Dynamics}
Phased contention results (fairness column = \emph{mean} normalized metric over ticks in each phase):
\begin{itemize}
    \item Phase 1 (5 tasks): SAFE 484, CAUTION 16, STRICT 0, mean fairness 3.36\%
    \item Phase 2 (55 tasks): SAFE 4, CAUTION 8, STRICT 988, mean fairness 555.32\%
    \item Phase 3 (15 tasks): SAFE 0, CAUTION 16, STRICT 484, mean fairness 92.55\%
\end{itemize}
Ordering $\text{Phase 1} < \text{Phase 3} < \text{Phase 2}$ matches light load, recovery, then heavy burst average intensity.

Controller diagnostics:
\begin{itemize}
    \item Reaction time: 4 ticks
    \item One monitor interval: 8 ticks
    \item Transition counts: SAFE$\rightarrow$CAUTION: 2, CAUTION$\rightarrow$STRICT: 1, STRICT$\rightarrow$CAUTION: 1, CAUTION$\rightarrow$SAFE: 1
\end{itemize}

Fairness here is the normalized max pairwise vruntime gap divided by mean vruntime among runnable tasks; large percentages are expected under burst contention. The table prints this value with a percent sign; footnotes in the simulator output clarify the definition.

\begin{figure}[htbp]
\centerline{\includegraphics[width=\linewidth]{apex_notebook_outputs/exp3_controller.png}}
\caption{Adaptive controller state occupancy with fairness trend across load phases (Experiment 3).}
\label{fig:exp3_controller}
\end{figure}

\subsection{Experiment 4: Error Bound Verification}
All configured bounds are satisfied:
\begin{itemize}
    \item LINEAR: 0.0500\% bound, 0.0250\% observed, verified
    \item LUT256: 0.5000\% bound, 0.0033\% observed, verified
    \item POLY2: 0.0014\% bound, 0.0002\% observed, verified
    \item ADAPTIVE: 0.5000\% bound, 0.0002\% observed, verified
\end{itemize}

\begin{figure}[htbp]
\centerline{\includegraphics[width=\linewidth]{apex_notebook_outputs/exp4_errorbounds.png}}
\caption{Empirical approximation drift against theoretical bounds (Experiment 4 visualization).}
\label{fig:exp4_bounds}
\end{figure}

\subsection{Experiment 5: TCP CUBIC Approximation}
\begin{itemize}
    \item Average cycles/update: 80.00 (exact) vs 30.00 (approx)
    \item Speedup: 2.67x
    \item Mean cube-root error: 0.0592\%
    \item Max cube-root error: 0.0651\%
    \item Mean window-size error: 0.0069\%
\end{itemize}

\begin{figure}[htbp]
\centerline{\includegraphics[width=\linewidth]{apex_notebook_outputs/exp5_cubic.png}}
\caption{Experiment 5 visualization: Cycle count speedup and error propagation for TCP CUBIC cube root approximation.}
\label{fig:exp5_cubic}
\end{figure}

\section{Discussion}
The Phase 2 simulator demonstrates that arithmetic approximation can be integrated into scheduler-like paths with:
\begin{itemize}
    \item bounded and verified numerical error (Theorem~2 style checks),
    \item zero starvation events in tested workloads,
    \item meaningful adaptive-state transitions under load spikes,
    \item complementary metrics for mixed weights: CPU share can be weight-dominated while vruntime drift exposes approximation fidelity.
\end{itemize}

Limitations:
\begin{itemize}
    \item userspace simulation (not kernel-patched validation),
    \item workload-specific adaptive behavior (can resemble one fixed mode under some regimes),
    \item fairness normalization magnitude in burst regimes requires careful interpretation,
    \item Theorem~1 style drift threshold is an artifact-level guardrail for this simulator, not a claim of a proved Linux starvation theorem.
\end{itemize}

\section{Conclusion}
APEX-CFS Phase 2 provides a reproducible framework for studying approximation-aware scheduler arithmetic with explicit fairness and error guardrails. The current results show stable equal-weight fairness, clear mixed-weight differentiation in ideal-vruntime drift (with aligned single-file and modular code), phase-mean fairness and responsive adaptive transitions in Experiment~3, full error-bound compliance, and strong CUBIC speedup with low approximation error. This establishes a practical baseline for future kernel-level implementation and evaluation.

\section*{Acknowledgment}
This work is based on a course/research prototype and publicly available Linux scheduling references. The authors acknowledge the Linux kernel community and prior approximate-computing research that informed this implementation.

\begin{thebibliography}{00}
\bibitem{cfsdesign} Linux Kernel Documentation, ``CFS Scheduler Design.'' [Online]. Available: https://docs.kernel.org/scheduler/sched-design-CFS.html
\bibitem{peltlwn} P. Turner, ``Per-entity load tracking,'' LWN.net, 2013. [Online]. Available: https://lwn.net/Articles/531853/
\bibitem{meantime} A. Farrell and H. Hoffmann, ``MEANTIME: Achieving Both Minimal Energy and Timeliness with Approximate Computing,'' in \textit{Proc. USENIX ATC}, 2016.
\bibitem{pliant} N. Kulkarni et al., ``Pliant: Leveraging Approximation to Improve Datacenter Resource Efficiency,'' in \textit{Proc. IEEE HPCA}, 2019.
\bibitem{agrawal} A. Agrawal et al., ``Approximate Computing: Challenges and Opportunities,'' in \textit{Proc. IEEE ICRC}, 2016.
\bibitem{cano} B. Cano-Camarero et al., ``Introducing Approximate Memory Support in the Linux Kernel,'' in \textit{Proc. IEEE ISVLSI}, 2017.
\bibitem{linuxsrc} Linux Kernel Source Tree, ``Scheduler implementation (fair.c and related files).'' [Online]. Available: https://github.com/torvalds/linux
\bibitem{apex} D. Sachan, S. Mahapatra, and M. Chinnam, ``APEX-CFS Phase 2: Simulation Artifacts and Analysis,'' project repository artifact, 2026.
\end{thebibliography}

\end{document}
