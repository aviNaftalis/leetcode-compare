# leetcode-compare

A small C++ benchmarking harness for **comparing different solutions to the same
LeetCode problem** — not just "is it accepted?", but *why is one solution faster
than another?*

Each problem ships with multiple solutions, a set of test cases, and a test
engine that compiles every solution **optimized and in isolation**, runs it, and
reports **wall time, peak memory, and object size** so the solutions can be
compared head-to-head. Timing is cross-checked with [Catch2](https://github.com/catchorg/Catch2)'s
statistical micro-benchmarking.

## Architecture

The three pieces the harness is built from:

| piece | what it is | where |
|---|---|---|
| **Solution** | one accepted answer to a problem (a self-contained header) | `problems/<p>/solutions/*.hpp` |
| **Test case** | a workload *shape* — concurrency, ordering, repetitions — independent of any solution | `problems/<p>/testcases/*.hpp` |
| **Test engine** | drives any solution through any test case, verifies correctness, measures time + memory | `engine/Engine.hpp` |

A solution and a test case are combined by the engine in three front-ends:

- **`test/`** — Catch2 correctness tests (every solution must pass before it's timed).
- **`bench/`** — Catch2 statistical benchmark (rigorous mean / std-dev / outliers).
- **`runner/`** — a generic `main` compiled **once per solution** into its own
  `-O3 -march=native` binary, run as a separate process so peak-RSS is
  meaningful and solutions can't contaminate each other. `scripts/compare.sh`
  builds them all and prints a sorted comparison table.

## Problems

| problem | comparison | answer |
|---|---|---|
| [Print in Order](problems/print-in-order/) (LC 1114) | condition variable vs spinlock (vs pause / yield variants) | **the condition variable is ~3× faster under contention; identical when nobody waits** |

## Quick start

```bash
# build everything + fetch Catch2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 1. correctness (must pass before benchmarking means anything)
ctest --test-dir build --output-on-failure

# 2. head-to-head comparison: each solution built optimized, run in isolation
./scripts/compare.sh

# 3. rigorous statistical timing
./build/problems/print-in-order/bench_print_in_order
```

Example `compare.sh` output (12-core WSL2, g++ 15.2):

```
solution            sizeof(B)  median(ms)  min(ms)   max(ms)   peakRSS(KiB)
condition_variable  96         483.105     445.966   518.835   4528
spinlock_yield      4          932.799     828.496   1152.174  4680
spinlock            4          1520.636    1475.058  2130.868  4360
spinlock_pause      4          1569.153    1386.795  1619.913  4652
```

See [`problems/print-in-order/README.md`](problems/print-in-order/) for the full
write-up of *why* the spinlock is slower.

## Requirements

- CMake ≥ 3.20, a C++20 compiler (tested with g++ 15.2)
- Internet on first configure (Catch2 is fetched via `FetchContent`)

## Adding a problem

1. `problems/<name>/solutions/*.hpp` — each solution as a header (same public API).
2. `problems/<name>/testcases/*.hpp` — workload shapes built on `engine::TestCase`.
3. Wire `test/`, `bench/` and the `runner/` targets in a `CMakeLists.txt`
   (copy `print-in-order`'s).
