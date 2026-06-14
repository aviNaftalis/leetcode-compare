// bench/benchmark.cpp
//
// Catch2 statistical micro-benchmark. Catch2 runs each BENCHMARK many times,
// discards warm-up, and reports mean / std-dev / outliers — the rigorous,
// apples-to-apples timing comparison of the five solutions across workloads.
// Built with -O3 (see CMakeLists.txt).
//
// For the per-solution *isolated* run (separate optimized process, plus memory)
// see runner/runner.cpp and scripts/compare.sh.

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "problems/print-in-order/solutions/AtomicWait.hpp"
#include "problems/print-in-order/solutions/ConditionVariable.hpp"
#include "problems/print-in-order/solutions/Spinlock.hpp"
#include "problems/print-in-order/solutions/SpinlockPause.hpp"
#include "problems/print-in-order/solutions/SpinlockYield.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using namespace pio;

namespace {
void compareAll(const engine::TestCase& tc) {
    BENCHMARK("condition_variable") { return engine::runWorkload<FooCV>(tc); };
    BENCHMARK("atomic_wait") { return engine::runWorkload<FooAtomicWait>(tc); };
    BENCHMARK("spinlock (naked)") { return engine::runWorkload<FooSpin>(tc); };
    BENCHMARK("spinlock (pause)") { return engine::runWorkload<FooSpinPause>(tc); };
    BENCHMARK("spinlock (yield)") { return engine::runWorkload<FooSpinYield>(tc); };
}
} // namespace

TEST_CASE("print-in-order: contention, trivial action", "[bench]") {
    compareAll(testcases::contention);
}

TEST_CASE("print-in-order: contention, CPU work", "[bench][cpu]") {
    compareAll(testcases::contentionCpu);
}

TEST_CASE("print-in-order: contention, sleep/IO work", "[bench][sleep]") {
    compareAll(testcases::contentionSleep);
}

TEST_CASE("print-in-order: fast path (control)", "[bench]") {
    compareAll(testcases::fastPath);
}
