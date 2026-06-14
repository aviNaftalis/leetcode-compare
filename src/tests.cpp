// src/tests.cpp
//
// Correctness gate. A benchmark of a broken lock is meaningless: T threads each
// doing K exclusive increments of a shared counter must leave exactly T*K.

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shootout/Primitives.hpp"

using namespace shootout;

TEMPLATE_TEST_CASE("lock provides mutual exclusion", "[lock]", CVLock, StdMutex,
                   SharedMutex, TicketLock) {
    static_assert(Lock<TestType>);
    constexpr int T = 8, K = 20'000;
    TestType lock;
    long long counter = 0;
    std::latch start{1};
    {
        std::vector<std::jthread> ws;
        for (int t = 0; t < T; ++t)
            ws.emplace_back([&] {
                start.wait();
                for (int i = 0; i < K; ++i) {
                    lock.lock();
                    ++counter;
                    lock.unlock();
                }
            });
        start.count_down();
    }
    REQUIRE(counter == static_cast<long long>(T) * K);
}
