#!/usr/bin/env python3
"""Render the shootout graphs from results/sweep.csv into docs/img/.

Usage: python3 scripts/plot.py [results/sweep.csv]
Needs matplotlib + numpy (no pandas).
"""
import csv
import os
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "results", "sweep.csv")
IMG = os.path.join(ROOT, "docs", "img")
os.makedirs(IMG, exist_ok=True)
CORES = os.cpu_count() or 12

COLOR = {
    "tas": "#d62728", "ttas": "#ff7f0e", "ttas_backoff": "#8c564b",
    "ticket": "#9467bd", "mcs": "#1f77b4", "std_mutex": "#2ca02c",
    "atomic": "#17becf",
    "shared_mutex": "#2ca02c", "excl_ttas": "#d62728", "excl_std": "#ff7f0e",
    "spin": "#d62728", "spin_yield": "#8c564b", "cv": "#2ca02c", "atomic_wait": "#1f77b4",
}
NICE = {"std_mutex": "std::mutex", "ttas_backoff": "ttas+backoff",
        "shared_mutex": "std::shared_mutex", "excl_ttas": "exclusive (ttas)",
        "excl_std": "exclusive (std::mutex)", "atomic_wait": "atomic::wait",
        "spin_yield": "spin+yield", "cv": "condition_variable"}


def nice(p):
    return NICE.get(p, p)


FLOAT = ("threads", "cs", "write", "wall_ms", "cpu_ms", "cores",
         "throughput_Mops", "ns_per_op", "fairness_cov", "bytes")


def load():
    rows = []
    with open(CSV, newline="") as f:
        for r in csv.DictReader(f):
            for k in FLOAT:
                r[k] = float(r[k])
            rows.append(r)
    return rows


def series(rows, s):
    return [r for r in rows if r["series"] == s]


def order_present(data, key):
    seen = []
    for r in data:
        if r[key] not in seen:
            seen.append(r[key])
    return seen


def lineplot(data, xkey, ykey, fname, title, xlabel, ylabel, xlog=False, ylog=False,
             xscale=1.0, vline=None):
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for p in order_present(data, "primitive"):
        pts = sorted((r for r in data if r["primitive"] == p), key=lambda r: r[xkey])
        xs = [r[xkey] * xscale for r in pts]
        ys = [r[ykey] for r in pts]
        ax.plot(xs, ys, "-o", color=COLOR.get(p, None), label=nice(p), lw=2, ms=4)
    if vline:
        ax.axvline(vline, color="gray", ls="--", lw=1)
    if xlog:
        ax.set_xscale("log", base=2)
    if ylog:
        ax.set_yscale("log")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, fname), dpi=130)
    plt.close(fig)


def main():
    rows = load()

    # 1. Contention: throughput, fairness, CPU vs thread count.
    c = series(rows, "contention")
    lineplot(c, "threads", "throughput_Mops", "contention_throughput.png",
             "Contended counter: throughput vs thread count (tiny critical section)\n"
             "lock-free atomic & backoff spin scale; fair spin locks collapse when oversubscribed",
             f"threads ({CORES} cores)", "throughput (Mops/s) — higher is better",
             xlog=True, vline=CORES)
    # Fairness only means something where the lock actually makes progress, i.e.
    # threads <= cores. (Past that, FIFO spin locks convoy-collapse and the metric
    # measures the collapse, not fairness.)
    lineplot([r for r in c if r["threads"] <= CORES], "threads", "fairness_cov",
             "contention_fairness.png",
             "Fairness up to core count: spread of per-thread acquisitions\n"
             "ticket & MCS are perfectly fair; naked TAS lets threads hog the lock",
             f"threads (<= {CORES} cores)", "coefficient of variation — lower is fairer",
             xlog=True)
    lineplot(c, "threads", "cores", "contention_cpu.png",
             "CPU cost vs thread count (same runs)\n"
             "spinlocks burn every core; std::mutex parks waiters",
             f"threads ({CORES} cores)", "CPU cost = avg cores busied — lower is cheaper",
             xlog=True, vline=CORES)

    # 2. Critical-section length (oversubscribed). Drop cs=0 so x can be log.
    cs = [r for r in series(rows, "cs_length") if r["cs"] > 0]
    lineplot(cs, "cs", "throughput_Mops", "cs_length.png",
             "Critical-section length, oversubscribed (24 threads / %d cores)\n"
             "backoff spin leads for short sections; std::mutex (parking) pulls ahead as they grow" % CORES,
             "critical-section work (iterations, log)", "throughput (Mops/s)",
             xlog=True, ylog=True)

    # 3. Read-heavy vs write fraction.
    rh = series(rows, "readheavy")
    lineplot(rh, "write", "throughput_Mops", "readheavy.png",
             "Read-heavy: throughput vs write fraction (8 threads, meaty reads)\n"
             "shared_mutex dominates with few writes; writer-preference erodes it fast",
             "writes (% of operations)", "throughput (Mops/s)", ylog=True)

    # 4. Signaling hand-off (hot = 1 lane).
    sg = [r for r in series(rows, "signaling") if r["threads"] == 1]
    sols = order_present(sg, "primitive")
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 5))
    lat = [next(r for r in sg if r["primitive"] == p)["ns_per_op"] for p in sols]
    cor = [next(r for r in sg if r["primitive"] == p)["cores"] for p in sols]
    a1.bar([nice(p) for p in sols], lat, color=[COLOR.get(p) for p in sols])
    a1.set_yscale("log"); a1.set_ylabel("ns per hand-off (log) — lower is better")
    a1.set_title("Signaling latency (hot: 2 threads, free cores)")
    a1.tick_params(axis="x", rotation=15)
    a2.bar([nice(p) for p in sols], cor, color=[COLOR.get(p) for p in sols])
    a2.set_ylabel("CPU cost = avg cores busied"); a2.set_title("…and its CPU cost")
    a2.tick_params(axis="x", rotation=15)
    fig.suptitle("Producer/consumer hand-off: spinning is fastest but burns CPU; "
                 "parking is cheap CPU", fontsize=12)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "signaling.png"), dpi=130)
    plt.close(fig)

    print(f"Wrote 6 graphs to {IMG}")


if __name__ == "__main__":
    main()
