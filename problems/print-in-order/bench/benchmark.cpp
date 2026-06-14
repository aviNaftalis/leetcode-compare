// bench/benchmark.cpp
//
// Catch2 statistical micro-benchmark. Catch2 runs each BENCHMARK many times,
// discards warm-up, and reports mean/std-dev/outliers — so this is the
// rigorous, apples-to-apples timing comparison of the four solutions on the
// same workload. Built with -O3 (see CMakeLists.txt).
//
// For the per-solution *isolated* run (separate optimized process, plus memory)
// see runner/runner.cpp and scripts/compare.sh.

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "problems/print-in-order/solutions/ConditionVariable.hpp"
#include "problems/print-in-order/solutions/Spinlock.hpp"
#include "problems/print-in-order/solutions/SpinlockPause.hpp"
#include "problems/print-in-order/solutions/SpinlockYield.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using namespace pio;

TEST_CASE("print-in-order: oversubscribed contention", "[bench]") {
    const auto tc = testcases::contention();

    BENCHMARK("condition_variable") { return engine::runWorkload<FooCV>(tc); };
    BENCHMARK("spinlock (naked)") { return engine::runWorkload<FooSpin>(tc); };
    BENCHMARK("spinlock (pause)") { return engine::runWorkload<FooSpinPause>(tc); };
    BENCHMARK("spinlock (yield)") { return engine::runWorkload<FooSpinYield>(tc); };
}

TEST_CASE("print-in-order: fast path (control)", "[bench]") {
    const auto tc = testcases::fastPath();

    BENCHMARK("condition_variable") { return engine::runWorkload<FooCV>(tc); };
    BENCHMARK("spinlock (naked)") { return engine::runWorkload<FooSpin>(tc); };
    BENCHMARK("spinlock (pause)") { return engine::runWorkload<FooSpinPause>(tc); };
    BENCHMARK("spinlock (yield)") { return engine::runWorkload<FooSpinYield>(tc); };
}
