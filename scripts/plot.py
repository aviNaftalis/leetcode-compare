#!/usr/bin/env python3
"""Render the shootout charts from results/sweep.csv into docs/img/.

Two figures, each a throughput + latency pair vs thread count:
  write_contended.png   read=0  cs=0   (mutual exclusion)
  read_mostly.png       read=95 cs=1000 (reader-writer)

Usage: python3 scripts/plot.py [results/sweep.csv]   (needs matplotlib + numpy)
"""
import csv
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "results", "sweep.csv")
IMG = os.path.join(ROOT, "docs", "img")
os.makedirs(IMG, exist_ok=True)
CORES = os.cpu_count() or 12

COLOR = {"cv": "#2ca02c", "std_mutex": "#1f77b4", "shared_mutex": "#9467bd",
         "ticket": "#ff7f0e", "atomic": "#d62728"}
NICE = {"cv": "condition_variable", "std_mutex": "std::mutex",
        "shared_mutex": "std::shared_mutex", "ticket": "ticket (fair spin)",
        "atomic": "lock-free atomic"}
ORDER = ["atomic", "ticket", "std_mutex", "cv", "shared_mutex"]
FLOAT = ("threads", "read", "cs", "throughput_Mops", "ns_per_op", "bytes")


def load():
    rows = []
    with open(CSV, newline="") as f:
        for r in csv.DictReader(f):
            for k in FLOAT:
                r[k] = float(r[k])
            rows.append(r)
    return rows


def figure(rows, cfg, fname, suptitle):
    data = [r for r in rows if r["config"] == cfg]
    prims = [p for p in ORDER if any(r["primitive"] == p for r in data)]
    fig, (a_tp, a_lat) = plt.subplots(1, 2, figsize=(12.5, 5.2))
    for p in prims:
        pts = sorted((r for r in data if r["primitive"] == p), key=lambda r: r["threads"])
        xs = [r["threads"] for r in pts]
        a_tp.plot(xs, [r["throughput_Mops"] for r in pts], "-o", color=COLOR[p],
                  label=NICE[p], lw=2, ms=4)
        a_lat.plot(xs, [r["ns_per_op"] for r in pts], "-o", color=COLOR[p],
                   label=NICE[p], lw=2, ms=4)
    for ax in (a_tp, a_lat):
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.axvline(CORES, color="gray", ls="--", lw=1)
        ax.set_xlabel(f"threads ({CORES} cores)")
        ax.grid(True, which="both", alpha=0.3)
    a_tp.set_ylabel("throughput (Mops/s) — higher is better")
    a_tp.set_title("Throughput")
    a_lat.set_ylabel("latency per op (ns) — lower is better")
    a_lat.set_title("Latency")
    a_tp.legend(fontsize=9)
    fig.suptitle(suptitle, fontsize=13)
    fig.tight_layout()
    fig.savefig(os.path.join(IMG, fname), dpi=130)
    plt.close(fig)


def main():
    rows = load()
    figure(rows, "write", "write_contended.png",
           "Write-contended counter (mutual exclusion): lock-free atomic wins; "
           "the fair spinlock collapses when oversubscribed")
    figure(rows, "read", "read_mostly.png",
           "Read-only with a real read section: std::shared_mutex lets readers "
           "run in parallel (atomic omitted — it guards only one word)")
    print(f"Wrote 2 figures to {IMG}")


if __name__ == "__main__":
    main()
