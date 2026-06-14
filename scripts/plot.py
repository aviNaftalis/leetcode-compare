#!/usr/bin/env python3
"""Turn results/sweep.csv into the learning graphs in docs/img/.

Usage: python3 scripts/plot.py [results/sweep.csv]
No pandas required — just csv + numpy + matplotlib.
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
ROUNDS_OVERSUB = 4000  # must match sweep.sh series 1

# Stable order + colour per solution so every chart is consistent.
ORDER = ["spinlock", "spinlock_pause", "spinlock_yield", "atomic_wait", "condition_variable"]
LABEL = {
    "spinlock": "spinlock (naked)",
    "spinlock_pause": "spinlock (pause)",
    "spinlock_yield": "spinlock (yield)",
    "atomic_wait": "atomic_wait",
    "condition_variable": "condition_variable",
}
COLOR = {
    "spinlock": "#d62728",
    "spinlock_pause": "#ff7f0e",
    "spinlock_yield": "#bcbd22",
    "atomic_wait": "#1f77b4",
    "condition_variable": "#2ca02c",
}
WORKLABEL = {("none", "0"): "trivial", ("cpu", "500"): "cpu light",
             ("cpu", "4000"): "cpu heavy", ("cpu", "20000"): "cpu",
             ("sleep", "200"): "sleep / IO"}
# Short, variant-distinguishing names so the heatmap never hides which kind of
# "spinlock" actually won (yield gives the core back; naked/pause hold it).
SHORT = {"spinlock": "spin-naked", "spinlock_pause": "spin-pause",
         "spinlock_yield": "spin-yield", "atomic_wait": "atomic_wait",
         "condition_variable": "cond_var"}
HOLDS_CORE = {"spinlock", "spinlock_pause"}  # true busy-wait


def load():
    rows = []
    with open(CSV, newline="") as f:
        for r in csv.DictReader(f):
            for k in ("x", "median_ms", "min_ms", "max_ms", "sizeof", "cpu_ms", "cores"):
                r[k] = float(r[k])
            rows.append(r)
    return rows


def by_series(rows, s):
    return [r for r in rows if r["series"] == s]


def plot_oversub(rows):
    data = by_series(rows, "oversub")
    fig, ax = plt.subplots(figsize=(9, 5.5))
    for sol in ORDER:
        pts = sorted((r for r in data if r["solution"] == sol), key=lambda r: r["x"])
        if not pts:
            continue
        xs = [3 * r["x"] / CORES for r in pts]  # oversubscription ratio
        ys = [r["median_ms"] for r in pts]
        ax.plot(xs, ys, "-o", color=COLOR[sol], label=LABEL[sol], lw=2, ms=4)
    ax.axvline(1.0, color="gray", ls="--", lw=1)
    ax.text(1.02, ax.get_ylim()[1], "  cores saturated", color="gray", va="top", fontsize=9)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel(f"oversubscription  =  threads / cores  ({CORES} cores)")
    ax.set_ylabel("median wall time (ms, log) — lower is better")
    ax.set_title("Baton-relay handoff cost vs oversubscription (trivial work)\n"
                 "with trivial waits, spinning leads until ~6× — until then "
                 "parking's per-wakeup syscall dominates")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "oversub_lines.png"), dpi=130)
    plt.close(fig)


def plot_handoff(rows):
    data = [r for r in by_series(rows, "oversub") if r["x"] == 1]
    handoffs = 3 * ROUNDS_OVERSUB  # one lane
    sols = [s for s in ORDER if any(r["solution"] == s for r in data)]
    ns = [next(r for r in data if r["solution"] == s)["median_ms"] * 1e6 / handoffs
          for s in sols]
    fig, ax = plt.subplots(figsize=(9, 5))
    bars = ax.bar([LABEL[s] for s in sols], ns, color=[COLOR[s] for s in sols])
    ax.set_yscale("log")
    ax.set_ylabel("nanoseconds per handoff (log) — lower is better")
    ax.set_title("Pure handoff latency: 3 hot threads, no oversubscription\n"
                 "the regime where spinning beats parking")
    for b, v in zip(bars, ns):
        ax.text(b.get_x() + b.get_width() / 2, v, f"{v:,.0f} ns",
                ha="center", va="bottom", fontsize=9)
    ax.tick_params(axis="x", rotation=12)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "handoff_latency.png"), dpi=130)
    plt.close(fig)


def plot_heatmap(rows):
    data = by_series(rows, "grid")
    lanes = sorted({int(r["x"]) for r in data})
    works = []
    for r in data:
        key = (r["work"], r["mag"])
        if key not in works:
            works.append(key)
    works = sorted(works, key=lambda k: (k[0] != "none", float(k[1])))
    best = {}  # (lane, work) -> (winner, ratio_naked_vs_winner)
    for ln in lanes:
        for wk in works:
            cell = {r["solution"]: r["median_ms"] for r in data
                    if int(r["x"]) == ln and (r["work"], r["mag"]) == wk}
            if not cell:
                continue
            winner = min(cell, key=cell.get)
            naked = cell.get("spinlock", cell[winner])
            best[(ln, wk)] = (winner, naked / cell[winner])
    grid = np.array([[ORDER.index(best[(ln, wk)][0]) for wk in works] for ln in lanes])
    fig, ax = plt.subplots(figsize=(8.5, 5.5))
    cmap = matplotlib.colors.ListedColormap([COLOR[s] for s in ORDER])
    ax.imshow(grid, cmap=cmap, vmin=0, vmax=len(ORDER) - 1, aspect="auto")
    ax.set_xticks(range(len(works)), [WORKLABEL.get(w, "/".join(w)) for w in works])
    ax.set_yticks(range(len(lanes)), [f"{3*ln/CORES:g}×\n({3*ln} thr)" for ln in lanes])
    ax.set_xlabel("work per stage")
    ax.set_ylabel("oversubscription  (steady-state relay = next-in-line waiter)")
    ax.set_title("Who wins each regime (cell = fastest solution)\n"
                 "annotation: how much slower the naked busy-spin is than the winner")
    for i, ln in enumerate(lanes):
        for j, wk in enumerate(works):
            win, ratio = best[(ln, wk)]
            rtxt = f"{ratio:.0f}×" if ratio >= 2 else f"{ratio:.1f}×"
            note = "busy-spin wins" if win in HOLDS_CORE else f"busy-spin {rtxt}"
            ax.text(j, i, f"{SHORT[win]}\n({note})",
                    ha="center", va="center", color="white", fontsize=8, fontweight="bold")
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLOR[s]) for s in ORDER]
    ax.legend(handles, [LABEL[s] for s in ORDER], loc="center left",
              bbox_to_anchor=(1.01, 0.5), fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "winner_heatmap.png"), dpi=130)
    plt.close(fig)


def plot_work_bars(rows):
    data = by_series(rows, "oneshot_work")
    works = []
    for r in data:
        key = (r["work"], r["mag"])
        if key not in works:
            works.append(key)
    works = sorted(works, key=lambda k: (k[0] != "none", float(k[1])))
    sols = [s for s in ORDER if any(r["solution"] == s for r in data)]
    x = np.arange(len(works))
    w = 0.8 / len(sols)
    fig, ax = plt.subplots(figsize=(9.5, 5.5))
    for i, sol in enumerate(sols):
        ys = []
        for wk in works:
            m = [r["median_ms"] for r in data
                 if r["solution"] == sol and (r["work"], r["mag"]) == wk]
            ys.append(m[0] if m else 0)
        ax.bar(x + i * w, ys, w, color=COLOR[sol], label=LABEL[sol])
    ax.set_yscale("log")
    ax.set_xticks(x + 0.4 - w / 2, [WORKLABEL.get(wk, "/".join(wk)) for wk in works])
    ax.set_ylabel("median wall time (ms, log) — lower is better")
    ax.set_xlabel("work per action (one-shot, 96 threads / %d cores)" % CORES)
    ax.set_title("One-shot thundering herd: non-yielding spin loses on every work type\n"
                 "(yield / atomic_wait / condition_variable all give the core back)")
    ax.legend(fontsize=9)
    ax.grid(True, axis="y", which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "work_type_bars.png"), dpi=130)
    plt.close(fig)


def plot_cpu_cost(rows):
    """The hidden third axis: CPU burned (avg cores busied). Uses the one-shot
    series (runs long enough for getrusage to measure CPU reliably). Pairs with
    work_type_bars, which shows the *latency* of the same runs."""
    data = by_series(rows, "oneshot_work")
    works = []
    for r in data:
        key = (r["work"], r["mag"])
        if key not in works:
            works.append(key)
    works = sorted(works, key=lambda k: (k[0] != "none", float(k[1])))
    sols = [s for s in ORDER if any(r["solution"] == s for r in data)]
    x = np.arange(len(works))
    w = 0.8 / len(sols)
    fig, ax = plt.subplots(figsize=(9.5, 5.5))
    for i, sol in enumerate(sols):
        ys = []
        for wk in works:
            m = [r["cores"] for r in data
                 if r["solution"] == sol and (r["work"], r["mag"]) == wk]
            ys.append(m[0] if m else 0)
        ax.bar(x + i * w, ys, w, color=COLOR[sol], label=LABEL[sol])
    ax.axhline(CORES, color="gray", ls=":", lw=1)
    ax.text(x[-1] + 0.4, CORES, f"{CORES} cores", color="gray", va="bottom",
            ha="right", fontsize=9)
    ax.set_xticks(x + 0.4 - w / 2, [WORKLABEL.get(wk, "/".join(wk)) for wk in works])
    ax.set_ylabel("CPU cost = average cores kept busy — lower is cheaper")
    ax.set_xlabel("work per action (one-shot, 96 threads / %d cores)" % CORES)
    ax.set_title("The third axis: CPU cost of each method (same runs as the latency chart)\n"
                 "non-yielding spin pins every core; parking/yield use only what they need")
    ax.legend(fontsize=9)
    ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, "cpu_cost.png"), dpi=130)
    plt.close(fig)


def main():
    rows = load()
    plot_oversub(rows)
    plot_handoff(rows)
    plot_heatmap(rows)
    plot_work_bars(rows)
    plot_cpu_cost(rows)
    print(f"Wrote 5 graphs to {IMG}")


if __name__ == "__main__":
    main()
