# Print in Order — condition variable vs spinlock

LeetCode [1114. Print in Order](https://leetcode.com/problems/print-in-order/).

Three threads call `first()`, `second()` and `third()` on a shared `Foo` object in
an arbitrary order. The job is to guarantee the prints happen as
`first → second → third`. A thread that arrives early has to **wait**, and *how*
it waits is the whole story.

## The two submissions being compared

| solution | how it waits | object size |
|---|---|---|
| [`ConditionVariable.hpp`](solutions/ConditionVariable.hpp) — accepted answer | parks on a `condition_variable` (kernel futex); CPU is released | 96 B |
| [`Spinlock.hpp`](solutions/Spinlock.hpp) — previous answer | busy-loops on an `atomic<int>`; CPU is held at 100% | 4 B |

Two diagnostic variants isolate the cause of the gap:

| variant | how it waits |
|---|---|
| [`SpinlockPause.hpp`](solutions/SpinlockPause.hpp) | busy-loop **+ `PAUSE` hint** — still 100% busy, but relaxes the pipeline / cache-line pressure |
| [`SpinlockYield.hpp`](solutions/SpinlockYield.hpp) | busy-loop **+ `std::this_thread::yield()`** — gives the core back to the scheduler |

## The workload (`testcases/TestCases.hpp`)

`contention-oversubscribed`: 48 `Foo` instances run at once (**144 threads on
12 cores**), and threads are launched in the order `{third, second, first}` — so
every `second`/`third` thread is already waiting before any `first` thread
exists. This is the worst case for busy-waiting.

## Results (12-core WSL2, g++ 15.2 `-O3 -march=native`)

Measured by the isolated per-solution runner (`scripts/compare.sh`), median of
11 trials:

| solution | sizeof | median | vs CV |
|---|---|---|---|
| **condition_variable** | 96 B | **483 ms** | 1.0× |
| spinlock (yield) | 4 B | 933 ms | 1.9× |
| spinlock (naked) | 4 B | 1521 ms | 3.1× |
| spinlock (pause) | 4 B | 1569 ms | 3.2× |

## Why the spinlock was slower

The workload is **oversubscribed**: there are far more runnable threads than
cores. Under those conditions:

1. A naked spinning `second`/`third` thread pins a core at 100% doing nothing
   useful. But the thread it is waiting for (`first`) needs a core to run on.
   The spinner is literally **starving the thread that would unblock it**, so
   the wait gets *longer*. That is the dominant cost — note the **3.1× gap**.

2. The `PAUSE` variant is **no better than naked spinning** (3.2× vs 3.1×). The
   pause hint helps cache-coherence traffic and power, but it does *not* give
   the core back — so under oversubscription it fixes the wrong problem.

3. The `yield` variant nearly halves the penalty (1.9×) because it deschedules
   the waiter, letting `first` run. It still loses to the condition variable
   because it keeps waking up to re-check (scheduler churn) instead of sleeping
   until actually signalled.

4. The **condition variable wins** because a parked thread consumes zero CPU and
   is woken exactly once, when `notify` fires. The cost is a bigger object
   (96 B vs 4 B) and a syscall on the slow path — a memory-for-CPU trade that
   pays off massively the moment real waiting happens.

**Takeaway:** spinlocks win when waits are *very short* and you have a *spare
core per waiter* (the spinner avoids a syscall and is ready instantly). The
moment either assumption breaks — long waits, or more waiters than cores — the
spinner burns the CPU its own unblocker needs, and a condition variable wins.

### The control test settles it

On the `fast-path-in-order` test case (threads launched `{1,2,3}`, so almost
nobody ever waits) the Catch2 statistical benchmark gives:

| solution | mean |
|---|---|
| condition_variable | 424 ms |
| spinlock (naked) | 423 ms |
| spinlock (pause) | 420 ms |
| spinlock (yield) | 420 ms |

They are **identical**. The synchronization primitive itself is not the cost —
the cost is entirely how a thread behaves *while it waits*, and that only bites
under contention. (Catch2 means on the contention case: CV 456 ms, spin-yield
954 ms, spin-naked 1495 ms, spin-pause 1519 ms — matching the isolated runner.)
