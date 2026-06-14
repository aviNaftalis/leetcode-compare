#!/usr/bin/env bash
# compare.sh — build every solution as its own optimized binary and run each in
# isolation, then print a comparison table sorted by median wall time.
#
# Usage: ./scripts/compare.sh [build-dir]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"

echo ">> Configuring (Release) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo ">> Building optimized per-solution runners"
cmake --build "$BUILD" --target runners -j "$(nproc)" >/dev/null

RUNNERS=(condition_variable spinlock spinlock_pause spinlock_yield)

echo ">> Running each solution in its own process"
echo
rows=()
for r in "${RUNNERS[@]}"; do
  bin="$BUILD/problems/print-in-order/runner_${r}"
  line="$("$bin" | grep '^CSV,')"
  rows+=("$line")
done

# Pretty table, sorted by median (column 4).
{
  printf 'solution\tsizeof(B)\tmedian(ms)\tmin(ms)\tmax(ms)\tpeakRSS(KiB)\n'
  printf '%s\n' "${rows[@]}" | sed 's/^CSV,//' | tr ',' '\t' | sort -t$'\t' -k3 -g
} | column -t -s$'\t'

echo
echo ">> Baseline = condition_variable; higher median = slower."
