# sync-shootout

A **modern C++23** benchmark shootout of synchronization primitives. It runs
spinlocks, blocking locks, fair locks, reader-writer locks, and lock-free code
across scenarios chosen so that **each method gets a use case where it shines**,
and reports the **three real costs of a primitive: latency/throughput, CPU, and
memory** — plus fairness.

The point isn't to crown one winner. It's to show *which tool fits which
situation*, and what you pay for each.

## What it measures

Every scenario is time-bounded (run for a fixed window, count what happened), so
one run yields:

- **throughput** (Mops/s) and **per-op latency** (ns)
- **CPU cost** — average cores kept busy (`getrusage` user+system ÷ wall). This
  is the metric that exposes a spinlock matching latency while burning 8× the CPU.
- **memory** — `sizeof` the primitive
- **fairness** — coefficient of variation of per-thread progress (0 = perfectly fair)

## Primitives

| family | primitives |
|---|---|
| **busy-wait** | `tas` (test-and-set), `ttas` (test-and-test-and-set), `ttas_backoff` (+ exponential backoff) |
| **fair spin** | `ticket` (FIFO), `mcs` (queue lock — each waiter spins on its own cache line) |
| **blocking** | `std_mutex` (parks on a futex) |
| **lock-free** | `atomic` (counter via `fetch_add`) |
| **reader-writer** | `std::shared_mutex` vs an exclusive-lock baseline |
| **signaling** | `spin`, `spin+yield`, `condition_variable`, `std::atomic::wait` |

## Scenarios — and who shines

| scenario | what it stresses | tends to win |
|---|---|---|
| **contended counter** | many threads, tiny critical section | lock-free `atomic`, `ttas_backoff` |
| **fairness** | does every thread make progress? | `ticket`, `mcs` |
| **critical-section length** (oversubscribed) | long hold times, threads > cores | `std::mutex` (parking) |
| **read-heavy** | mostly readers, few writers | `std::shared_mutex` |
| **uncontended** | single thread, raw overhead | `tas` / `atomic` (no syscall) |
| **signaling / hand-off** | wait for an event | `spin` (latency) vs `condition_variable` (CPU) |

See **[docs/findings.md](docs/findings.md)** for the graphs and the full
discussion of each.

## Quick start

```bash
cmake --preset default        # configure (fetches Catch2 on first run)
cmake --build build

ctest --test-dir build        # correctness: every lock must actually exclude

./scripts/sweep.sh            # run the grid -> results/sweep.csv
python3 scripts/plot.py       # render graphs -> docs/img/

# or run a single point yourself:
./build/shootout --scenario contended --primitive ttas_backoff --threads 16 --cs 0
./build/shootout --scenario readheavy --primitive shared_mutex --threads 8 --write 1 --cs 2000
./build/shootout --scenario signaling --primitive cv --threads 1 --rounds 30000
```

## Architecture

| piece | what it is | where |
|---|---|---|
| **Primitives** | each behind a `concept` (`Mutex` / `SharedMutex` / `Gate`) | `include/shootout/{Mutexes,RWLocks,Gates}.hpp` |
| **Scenarios** | templated, time-bounded workloads returning a `Result` | `include/shootout/Scenarios.hpp` |
| **Metrics** | CPU/RSS via `getrusage`, fairness, medians | `include/shootout/Metrics.hpp` |
| **Driver** | one `-O3 -march=native` binary, runtime dispatch by name | `src/shootout.cpp` |
| **Tests** | Catch2 — every lock must provide mutual exclusion | `src/tests.cpp` |

Adding a primitive is one struct satisfying the relevant concept, plus a line in
the dispatch table in `src/shootout.cpp`.

## Requirements

- CMake ≥ 3.28, Ninja, a C++23 compiler (tested with g++ 15.2)
- Python 3 + matplotlib + numpy for the graphs
- Internet on first configure (Catch2 via `FetchContent`)

## Notes & caveats

- Numbers are from one 12-core machine (WSL2). The *shapes and crossovers* are
  portable; absolute values aren't — re-run `sweep.sh` for your box.
- `getrusage` CPU resolution is coarse (~ms), so runs are sized to ≥~100 ms.
- This is a learning/benchmarking tool, not a lock library — the primitives are
  written for clarity.
