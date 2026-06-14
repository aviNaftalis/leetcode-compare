#!/usr/bin/env bash
# sweep.sh — run the shootout across thread counts and emit results/sweep.csv.
# Two configurations, each reporting throughput (Mops) and latency (ns/op):
#   write   write-contended counter (read=0, cs=0)            — mutual exclusion
#   read    read-mostly with a real read section (read=95%, cs=1000) — RW lock shines
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
echo "config,primitive,threads,read,cs,throughput_Mops,ns_per_op,bytes" > "$OUT"

emit() { # config + shootout args -> tagged CSV row
  local cfg="$1"; shift
  local row; row="$("$BIN" "$@" 2>/dev/null | grep '^CSV,' | sed 's/^CSV,//')"
  echo "$cfg,$row" >> "$OUT"
  echo "  [$cfg] $*"
}

THREADS=(1 2 4 8 12 16 24 32)

echo ">> write-contended counter (read=0, cs=0)"
for t in "${THREADS[@]}"; do
  for p in cv std_mutex shared_mutex ticket atomic; do
    emit write --primitive "$p" --threads "$t" --read 0 --cs 0 --secs 0.25 --trials 5
  done
done

echo ">> read-only (read=100%, cs=1000) — atomic omitted (lock-free guards one word only)"
for t in "${THREADS[@]}"; do
  for p in cv std_mutex shared_mutex ticket; do
    emit read --primitive "$p" --threads "$t" --read 100 --cs 1000 --secs 0.25 --trials 5
  done
done

echo ">> Wrote $OUT ($(($(wc -l < "$OUT") - 1)) rows). Cores=$CORES."
