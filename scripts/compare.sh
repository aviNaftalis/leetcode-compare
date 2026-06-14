#!/usr/bin/env bash
# compare.sh — build every solution as its own optimized binary and run each in
# isolation, then print a comparison table sorted by median wall time.
#
# Usage: ./scripts/compare.sh [workload] [build-dir]
#   workload: trivial (default) | cpu | sleep | fastpath
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKLOAD="${1:-trivial}"
BUILD="${2:-$ROOT/build}"

echo ">> Configuring (Release) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" --preset default >/dev/null 2>&1 \
  || cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo ">> Building optimized per-solution runners"
cmake --build "$BUILD" --target runners -j "$(nproc)" >/dev/null

RUNNERS=(condition_variable atomic_wait spinlock spinlock_pause spinlock_yield)

echo ">> Running each solution in its own process (workload: $WORKLOAD)"
echo
rows=()
for r in "${RUNNERS[@]}"; do
  bin="$(find "$BUILD" -name "runner_${r}" -type f | head -n1)"
  rows+=("$("$bin" "$WORKLOAD" | grep '^CSV,')")
done

# Pretty table, sorted by median (column 4 after stripping the CSV, prefix).
{
  printf 'testcase\tsolution\tsizeof(B)\tmedian(ms)\tmin(ms)\tmax(ms)\tpeakRSS(KiB)\n'
  printf '%s\n' "${rows[@]}" | sed 's/^CSV,//' | tr ',' '\t' | sort -t$'\t' -k4 -g
} | column -t -s$'\t'

echo
echo ">> Baseline = condition_variable; higher median = slower."
