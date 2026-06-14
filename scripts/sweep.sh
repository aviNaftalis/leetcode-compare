#!/usr/bin/env bash
# sweep.sh — run the full shootout grid and emit results/sweep.csv for plot.py.
# Series (each picked to show a use case where some primitive shines):
#   contention   throughput / fairness / CPU vs thread count (tiny critical section)
#   cs_length    throughput vs critical-section length, oversubscribed
#   readheavy    throughput vs write fraction (reader-writer lock)
#   signaling    hand-off latency + CPU, hot vs oversubscribed
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="$ROOT/results/sweep.csv"
CORES="$(nproc)"

cmake --preset default -B "$BUILD" >/dev/null 2>&1 \
  || cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target shootout -j "$CORES" >/dev/null
BIN="$BUILD/shootout"

mkdir -p "$ROOT/results"
echo "series,scenario,primitive,threads,cs,write,wall_ms,cpu_ms,cores,throughput_Mops,ns_per_op,fairness_cov,bytes" > "$OUT"

emit() { # series + shootout args -> tagged CSV row
  local series="$1"; shift
  local row; row="$("$BIN" "$@" 2>/dev/null | grep '^CSV,' | sed 's/^CSV,//')"
  echo "$series,$row" >> "$OUT"
  echo "  [$series] $*"
}

LOCKS=(tas ttas ttas_backoff ticket mcs std_mutex atomic)

echo ">> Series 1/4: contention vs thread count (tiny critical section)"
for thr in 1 2 4 8 12 16 24 32; do
  for p in "${LOCKS[@]}"; do
    emit contention --scenario contended --primitive "$p" --threads "$thr" --cs 0 --secs 0.25 --trials 5
  done
done

echo ">> Series 2/4: critical-section length, oversubscribed (24 threads)"
for cs in 0 50 200 1000 5000 20000; do
  for p in tas ttas_backoff ticket mcs std_mutex; do
    emit cs_length --scenario contended --primitive "$p" --threads 24 --cs "$cs" --secs 0.25 --trials 5
  done
done

echo ">> Series 3/4: read-heavy vs write fraction (8 threads, meaty reads)"
for w in 0 1 5 20 50; do
  for p in shared_mutex excl_ttas excl_std; do
    emit readheavy --scenario readheavy --primitive "$p" --threads 8 --write "$w" --cs 2000 --secs 0.25 --trials 5
  done
done

echo ">> Series 4/4: signaling hand-off (hot=1 lane, oversubscribed=12 lanes)"
for lanes in 1 12; do
  for p in spin spin_yield cv atomic_wait; do
    emit signaling --scenario signaling --primitive "$p" --threads "$lanes" --rounds 15000 --trials 3
  done
done

echo ">> Wrote $OUT ($(($(wc -l < "$OUT") - 1)) rows). Cores=$CORES."
