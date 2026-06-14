// src/tests.cpp
//
// Correctness gate. A benchmark of a broken lock is meaningless, so every
// mutual-exclusion primitive must actually exclude: T threads each doing K
// increments of a shared counter must leave exactly T*K. We also check the
// reader-writer locks serialize writers, and that the signaling gates relay a
// baton to completion without deadlock.

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include "shootout/Scenarios.hpp"

using namespace shootout;

namespace {
template <class Lock>
long long exclusionCount(int threads, int per, auto&& crit) {
    Lock lock;
    long long counter = 0;
    std::latch start{1};
    {
        std::vector<std::jthread> ws;
        for (int t = 0; t < threads; ++t)
            ws.emplace_back([&] {
                start.wait();
                for (int i = 0; i < per; ++i) crit(lock, counter);
            });
        start.count_down();
    }
    return counter;
}
} // namespace

TEMPLATE_TEST_CASE("mutex provides mutual exclusion", "[mutex]", TASLock, TTASLock,
                   TTASBackoff, TicketLock, MCSLock, StdMutex) {
    static_assert(Mutex<TestType>);
    constexpr int T = 8, K = 20'000;
    const long long got = exclusionCount<TestType>(T, K, [](auto& l, long long& c) {
        l.lock();
        ++c;
        l.unlock();
    });
    REQUIRE(got == static_cast<long long>(T) * K);
}

TEMPLATE_TEST_CASE("reader-writer lock serializes writers", "[rw]", StdSharedMutex,
                   ExclusiveAsShared<TTASLock>) {
    static_assert(SharedMutex<TestType>);
    constexpr int T = 8, K = 20'000;
    const long long got = exclusionCount<TestType>(T, K, [](auto& l, long long& c) {
        l.lock();
        ++c;
        l.unlock();
    });
    REQUIRE(got == static_cast<long long>(T) * K);
}

TEMPLATE_TEST_CASE("signaling gate relays to completion", "[gate]", SpinGate,
                   SpinGateYield, CVGate, AtomicWaitGate) {
    static_assert(Gate<TestType>);
    Params p;
    p.threads = 2; // 2 lanes
    p.rounds = 2'000;
    p.cs = 0;
    const Result r = signalingOnce<TestType>(p);
    REQUIRE(r.ops == 2.0 * p.rounds * p.threads); // every hand-off happened
}
