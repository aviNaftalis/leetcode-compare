// testcases/TestCases.hpp
//
// Workload definitions for the "Print in Order" comparison. A test case is just
// a shape (how many concurrent instances, launch order, repetitions); it is
// reused verbatim by the correctness tests, the Catch2 benchmark and the
// standalone runners so every front-end measures the same thing.

#pragma once

#include "engine/Engine.hpp"

namespace pio::testcases {

// Worst case for busy-waiting: every "third" then every "second" thread is
// launched (and starts waiting) before any "first" thread exists, and the batch
// is heavily oversubscribed (instances*3 threads >> CPU cores). This is the
// configuration that makes a spinlock pay for stealing CPU from the threads it
// is waiting on.
inline engine::TestCase contention() {
    return {/*name*/ "contention-oversubscribed",
            /*instances*/ 48,
            /*repetitions*/ 30,
            /*order*/ {3, 2, 1}};
}

// Best case: threads launched in the order they are allowed to run, so almost
// nobody ever waits. Spinlock and condition variable should be close here —
// useful as a control to show the gap is about *waiting*, not raw overhead.
inline engine::TestCase fastPath() {
    return {/*name*/ "fast-path-in-order",
            /*instances*/ 48,
            /*repetitions*/ 30,
            /*order*/ {1, 2, 3}};
}

// Tiny workload used by the correctness tests.
inline engine::TestCase smoke(std::array<int, 3> order) {
    return {/*name*/ "smoke", /*instances*/ 8, /*repetitions*/ 5, order};
}

} // namespace pio::testcases
