// testcases/TestCases.hpp
//
// Workload definitions for the "Print in Order" comparison. A test case is just
// a shape (concurrency, launch order, repetitions, and the payload each action
// runs); it is reused verbatim by the correctness tests, the Catch2 benchmark
// and the standalone runners so every front-end measures the same thing.

#pragma once

#include "engine/Engine.hpp"
#include "engine/Work.hpp"

namespace pio::testcases {

using engine::TestCase;
using engine::Work;

// Worst case for busy-waiting: every "third" then every "second" thread is
// launched (and starts waiting) before any "first" thread exists, and the batch
// is heavily oversubscribed (instances*3 threads >> CPU cores).

// Trivial action — measures the pure synchronization cost.
inline constexpr TestCase contention{
    .name = "contention/trivial",
    .instances = 48,
    .repetitions = 30,
    .order = {3, 2, 1},
};

// Each action does real CPU work, so the thread holding the baton needs a core
// — exactly what a spinning waiter is stealing.
inline constexpr TestCase contentionCpu{
    .name = "contention/cpu",
    .instances = 32,
    .repetitions = 12,
    .order = {3, 2, 1},
    .work = Work::cpu(20'000),
};

// Each action sleeps, modelling blocking I/O: the worker is off-CPU while
// holding the baton, so a spinning waiter burns a core for nothing the entire
// time.
inline constexpr TestCase contentionSleep{
    .name = "contention/sleep-io",
    .instances = 32,
    .repetitions = 8,
    .order = {3, 2, 1},
    .work = Work::sleep(200),
};

// Control: threads launched in the order they may run, so almost nobody waits.
// Proves the gap is about *waiting*, not raw per-call overhead.
inline constexpr TestCase fastPath{
    .name = "fast-path/in-order",
    .instances = 48,
    .repetitions = 30,
    .order = {1, 2, 3},
};

// Tiny workload for the correctness tests.
inline constexpr TestCase smoke(std::array<int, 3> order) {
    return {.name = "smoke", .instances = 8, .repetitions = 5, .order = order,
            .work = Work::cpu(256)};
}

} // namespace pio::testcases
