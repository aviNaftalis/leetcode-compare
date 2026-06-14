# Print in Order — condition variable vs spinlock

LeetCode [1114. Print in Order](https://leetcode.com/problems/print-in-order/).

Three threads call `first()`, `second()` and `third()` on a shared `Foo` object in
an arbitrary order. The job is to guarantee the prints happen as
`first → second → third`. A thread that arrives early has to **wait**, and *how*
it waits is the whole story.

## The solutions

| solution | how it waits | object size |
|---|---|---|
| [`ConditionVariable.hpp`](solutions/ConditionVariable.hpp) — accepted answer | parks on a `condition_variable` (kernel futex); CPU released | 96 B |
| [`Spinlock.hpp`](solutions/Spinlock.hpp) — previous answer | busy-loops on an `atomic<int>`; CPU held at 100% | 4 B |
| [`AtomicWait.hpp`](solutions/AtomicWait.hpp) — modern | lock-free fast path, but `atomic::wait()` **parks** when it must wait | 4 B |
| [`SpinlockPause.hpp`](solutions/SpinlockPause.hpp) — diagnostic | busy-loop **+ `PAUSE` hint** — still 100% busy, relaxes the pipeline | 4 B |
| [`SpinlockYield.hpp`](solutions/SpinlockYield.hpp) — diagnostic | busy-loop **+ `yield()`** — gives the core back to the scheduler | 4 B |

`AtomicWait` and the two diagnostic spins exist to *decompose* the gap: they let
us separate "lock-free vs not", "CPU starvation vs cache traffic", and "spin vs
park" as independent variables.

## The workload (`testcases/TestCases.hpp`)

48 (or 32) `Foo` instances run at once — **~100–144 threads on 12 cores** — all
gated on a `std::latch` and released simultaneously (a thundering herd), launched
in role order `{third, second, first}` so every `second`/`third` thread is
waiting before any `first` exists. Each action runs a configurable payload
instead of LeetCode's trivial print:

- **trivial** — just record the order (pure synchronization cost)
- **cpu** — an un-optimizable compute loop (the worker needs a core)
- **sleep** — `sleep_for`, a stand-in for blocking I/O (the worker is off-CPU)

## Results (12-core WSL2, g++ 15.2 `-std=c++23 -O3 -march=native`, median of 11)

Measured by the isolated per-solution runner (`./scripts/compare.sh <workload>`):

**trivial action**

| solution | sizeof | median | vs CV |
|---|---|---|---|
| spinlock (yield) | 4 B | 512 ms | 0.94× |
| **condition_variable** | 96 B | **543 ms** | 1.0× |
| atomic_wait | 4 B | 544 ms | 1.00× |
| spinlock (pause) | 4 B | 4039 ms | **7.4×** |
| spinlock (naked) | 4 B | 4076 ms | **7.5×** |

**cpu work** (each action = ~20k-iter compute loop)

| solution | median | vs CV |
|---|---|---|
| atomic_wait | 144 ms | 0.99× |
| **condition_variable** | 145 ms | 1.0× |
| spinlock (yield) | 157 ms | 1.08× |
| spinlock (pause) | 1207 ms | **8.3×** |
| spinlock (naked) | 1290 ms | **8.9×** |

**sleep / IO work** (each action sleeps 200 µs)

| solution | median | vs CV |
|---|---|---|
| spinlock (yield) | 98 ms | 0.98× |
| **condition_variable** | 100 ms | 1.0× |
| atomic_wait | 100 ms | 1.00× |
| spinlock (naked) | 747 ms | **7.5×** |
| spinlock (pause) | 757 ms | **7.6×** |

## Why the spinlock was slower

The workload is **oversubscribed**: far more runnable threads than cores. Each
controlled variable points at the same cause:

1. **It's busy-waiting, not the lock-free design.** `atomic_wait` is *also*
   lock-free with a 4-byte object and *also* has no mutex — yet it ties the
   condition variable in every workload, because when it has to wait it **parks**
   instead of spinning. So the 7–9× penalty is entirely about burning CPU while
   waiting, not about atomics vs mutexes.

2. **It's CPU starvation, not cache traffic.** The `PAUSE` variant is **no
   better than naked spinning** (7.4× vs 7.5× trivial; 8.3× vs 8.9× cpu). `PAUSE`
   relaxes cache-coherence/pipeline pressure but does *not* return the core — so
   under oversubscription it fixes the wrong problem. A spinning `second`/`third`
   pins a core doing nothing while starving the `first` thread that would unblock
   it, so the wait gets *longer*.

3. **Giving the core back is what matters.** `yield` deschedules the waiter and
   is competitive with parking here — it just wastes a little CPU re-checking and
   loses on the cpu workload (1.08×) where scheduler churn competes with real
   compute. Parking (CV / `atomic_wait`) is the cleanest: zero CPU while waiting,
   woken exactly once on `notify`.

4. **The sleep/IO workload is the cruelest for spinning.** While the baton-holder
   is asleep (off-CPU), a naked spinner burns a whole core for the *entire* I/O
   duration accomplishing nothing — 7.5× slower.

**The trade-off, concretely:** the condition variable spends memory (96 B vs 4 B)
and a slow-path syscall to avoid burning CPU. `atomic_wait` gets the same CPU
behaviour with a 4-byte object — the modern sweet spot here.

### The control settles it

On the `fast-path/in-order` case (threads launched `{1,2,3}`, so almost nobody
waits) all five solutions land within noise of each other. The primitive itself
costs nothing; the entire gap is how a thread behaves *while it waits*, and that
only bites under contention.

**When is a spinlock actually right?** Very short waits *and* a spare core per
waiter (no oversubscription) — then the spinner avoids a syscall and is ready
instantly. Break either assumption and it loses badly, which is exactly what
happened here.
