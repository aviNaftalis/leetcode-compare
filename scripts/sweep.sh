#!/usr/bin/env bash
# sweep.sh — run a parameter sweep over all solutions and emit a tidy CSV that
# scripts/plot.py turns into graphs. Three series:
#   oversub      relay, trivial handoff, varying oversubscription (lanes)
#   grid         relay, varying (oversubscription x work) -> winner heatmap
#   oneshot_work one-shot batch, varying work type -> "where parking wins" bars
#
# Usage: ./scripts/sweep.sh [build-dir]
# Output: results/sweep.csv
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="$ROOT/results/sweep.csv"
CORES="$(nproc)"

cmake --preset default -B "$BUILD" >/dev/null 2>&1 \
  || cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target runners -j "$CORES" >/dev/null

BIN="$BUILD/problems/print-in-order"
SOLUTIONS=(condition_variable atomic_wait spinlock spinlock_pause spinlock_yield)

mkdir -p "$ROOT/results"
echo "series,mode,solution,x,work,mag,median_ms,min_ms,max_ms,sizeof,rss_kib,cpu_ms,cores" > "$OUT"

emit() { # series + a runner invocation -> append tagged CSV row
  local series="$1"; shift; local sol="$1"; shift
  local row; row="$("$BIN/runner_$sol" "$@" 2>/dev/null | grep '^CSV,' | sed 's/^CSV,//')"
  echo "$series,$row" >> "$OUT"
  echo "  [$series] $sol $*"
}

echo ">> Series 1/3: oversubscription crossover (relay, trivial handoff)"
for lanes in 1 2 3 4 6 8 12 16 24 32; do
  for s in "${SOLUTIONS[@]}"; do
    emit oversub "$s" --mode relay --x "$lanes" --rounds 4000 --work none --trials 5
  done
done

echo ">> Series 2/3: oversubscription x work grid (relay)"
for lanes in 1 4 12 24; do          # 0.25x, 1x, 3x, 6x cores
  for wm in "none 0" "cpu 500" "cpu 4000"; do
    set -- $wm
    for s in "${SOLUTIONS[@]}"; do
      emit grid "$s" --mode relay --x "$lanes" --rounds 1500 --work "$1" --mag "$2" --trials 3
    done
  done
done

echo ">> Series 3/3: work-type comparison under contention (one-shot)"
for wm in "none 0 30" "cpu 20000 12" "sleep 200 8"; do
  set -- $wm                         # work mag reps
  for s in "${SOLUTIONS[@]}"; do
    emit oneshot_work "$s" --mode oneshot --x 32 --reps "$3" --work "$1" --mag "$2" --trials 5
  done
done

echo ">> Wrote $OUT ($(($(wc -l < "$OUT") - 1)) rows). Cores=$CORES (oversubscription = 3*lanes/$CORES)."
