// test/correctness.cpp
//
// Correctness gate: every solution must produce the ordering 1->2->3 regardless
// of the order its threads are launched in. A benchmark of an incorrect
// solution is meaningless, so this runs first (ctest) and the benchmark only
// matters once these pass.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include "problems/print-in-order/solutions/ConditionVariable.hpp"
#include "problems/print-in-order/solutions/Spinlock.hpp"
#include "problems/print-in-order/solutions/SpinlockPause.hpp"
#include "problems/print-in-order/solutions/SpinlockYield.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using namespace pio;

namespace {
// All six launch permutations, so we never accidentally only test the easy one.
const std::array<std::array<int, 3>, 6> kOrders{{
    {1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1},
}};
} // namespace

TEMPLATE_TEST_CASE("ordering holds for every launch permutation", "[correctness]",
                   FooCV, FooSpin, FooSpinPause, FooSpinYield) {
    for (auto order : kOrders) {
        CAPTURE(order);
        REQUIRE(engine::runWorkload<TestType>(testcases::smoke(order)));
    }
}
