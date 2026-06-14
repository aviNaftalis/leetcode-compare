# Findings: which synchronization primitive wins, and when

All numbers are from one 12-core machine (WSL2, g++ 15.2, `-O3 -march=native`),
each point the median of several time-bounded runs. Reproduce with
`./scripts/sweep.sh && python3 scripts/plot.py`. Absolute values are
machine-specific; the **shapes and crossovers** are the point.

## TL;DR — who wins each use case

| use case | winner | why |
|---|---|---|
| **uncontended** (rarely-taken lock) | lock-free `atomic`, `tas` | no syscall, fewest instructions on the fast path |
| **contention, tiny critical section** (≤ cores) | `ttas_backoff`, lock-free `atomic` | avoid the cache-line storm; never block |
| **fairness required** (≤ cores) | `ticket`, `mcs` | strict FIFO — no thread starves |
| **oversubscribed** (threads > cores) | `std::mutex` | parks; the OS runs a *runnable* thread. Fair spin locks **convoy-collapse** |
| **long critical sections** | `std::mutex` | a spinning waiter just wastes a core the holder needs |
| **read-mostly** | `std::shared_mutex` | readers proceed in parallel |
| **low-latency signaling** (a core to spare) | `spin`, `atomic::wait` | wakes in ~100 ns |
| **signaling, CPU-constrained** | `condition_variable`, `atomic::wait` | a parked waiter costs ~0 CPU |

The rest of this page is the evidence.

---

## 1. Contention: throughput, fairness, CPU

N threads hammer one lock around a shared counter (tiny critical section), as the
thread count crosses the 12-core line.

![throughput vs threads](img/contention_throughput.png)

- **Uncontended (1 thread)** the order is pure overhead: lock-free `atomic`
  (141 Mops) and `tas` (111) are cheapest; `mcs` (49) and `std::mutex` (43) carry
  ~3× the per-op cost. *If a lock is almost never contended, the simplest thing wins.*
- **Under contention** `ttas_backoff` (47 Mops @ 8 threads) and lock-free `atomic`
  (25) pull away. Backoff even **beats the lock-free counter** at moderate
  contention, because a single shared `fetch_add` ping-pongs one cache line on
  every op while the backoff lock lets the holder work undisturbed.
- **`tas` is never the answer** — test-and-test-and-set with backoff dominates it
  everywhere.

![CPU cost vs threads](img/contention_cpu.png)

Same runs, CPU cost. Every spin lock pins all the cores it can (`cores` → 12);
`std::mutex` is the only one that *parks*, so it holds steady around ~5–10 cores.
Two locks can post similar throughput while one quietly burns twice the CPU.

![fairness](img/contention_fairness.png)

Fairness (spread of per-thread acquisitions, up to core count). `ticket` and
`mcs` are essentially perfect (everyone gets equal turns); naked `tas`/`ttas`
let whichever thread owns the cache line keep re-grabbing the lock.

> ⚠️ **Fair spin locks collapse when oversubscribed.** Past 12 threads, `ticket`
> and `mcs` fall to ~0 Mops (see the first chart). Their FIFO rule means the
> *next* ticket-holder must run for anyone to proceed — and if the scheduler has
> descheduled it, all the others spin uselessly. Fairness is a benefit only when
> threads fit on cores.

---

## 2. Critical-section length (oversubscribed)

24 threads on 12 cores, sweeping how long the lock is held.

![throughput vs critical-section length](img/cs_length.png)

`ttas_backoff` leads for short sections, but `std::mutex` (parking) pulls ahead
as the section grows: once a thread holds the lock for a while, a spinning waiter
is just burning a core the holder needs. The fair spin locks are at the bottom —
convoy-collapsed, as above. **Oversubscribed + non-trivial hold time ⇒ park.**

---

## 3. Read-heavy

8 threads, a meaty read section, sweeping the write fraction.

![read-heavy](img/readheavy.png)

With **0% writes** `std::shared_mutex` is ~7× the exclusive locks — readers run
concurrently. But the advantage **erodes fast**: by ~7% writes it has fallen
*below* an exclusive `ttas`, and at 20%+ it's ~3× worse. glibc's `shared_mutex`
is writer-preferring, so a trickle of writers repeatedly blocks the reader pool
and serializes it (the CPU chart shows reader parallelism dropping from ~8 cores
to ~1.6). **A reader-writer lock pays off only when writes are genuinely rare.**

---

## 4. Signaling / hand-off

A producer/consumer baton, not a lock. Two hot threads, free cores.

![signaling](img/signaling.png)

- **Latency:** `spin` hands off in ~100 ns; `condition_variable` takes ~54,000 ns
  (futex syscall + context switch on every wake) — a ~550× gap. `atomic::wait`
  sits between (~8,000 ns).
- **CPU:** the picture inverts. `spin`/`spin+yield` burn 1–2 cores busy-waiting;
  the parking primitives use < 1. Spinning buys latency by spending CPU.

So for a hand-off: **spin if you have a dedicated core and need ns-scale wakeup;
park (`condition_variable`/`atomic::wait`) if you're CPU-constrained or the wait
might be long.**

---

## The throughline

There is no universal winner — only matches between a workload and a primitive:

- **No contention?** Anything cheap. Lock-free or a plain spin.
- **Contention but cores to spare?** Backoff spin or lock-free; add FIFO
  (`ticket`/`mcs`) only if you need fairness.
- **More threads than cores, or long holds?** Park — `std::mutex`. Spinning (and
  *especially* FIFO spinning) falls apart here.
- **Read-mostly?** `shared_mutex`, but only if writes are rare.
- **Signaling?** Trade latency against CPU: spin for speed, park for thrift.

And the recurring lesson from the spinlock study that seeded this repo: a
spinlock can match throughput while burning every core — **always look at CPU
cost, not just latency.**
