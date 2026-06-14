// engine/Engine.hpp
//
// The test engine. It is deliberately solution-agnostic: it knows how to drive
// *any* "Print in Order" solution (a type exposing first/second/third taking a
// std::function<void()>) through a configurable workload, verify correctness,
// and report the metrics we care about (wall time + peak RSS + object size).
//
// The same engine is consumed by three front-ends:
//   * test/correctness.cpp  -> Catch2 assertions that the ordering holds
//   * bench/benchmark.cpp   -> Catch2 statistical micro-benchmark
//   * runner/runner.cpp     -> a standalone, per-solution optimized binary that
//                              the compare script builds & runs in isolation
//
// Keeping the engine header-only means every front-end compiles the chosen
// solution inline, so -O3 can inline the synchronization primitives — exactly
// the conditions LeetCode-style benchmarking is supposed to model.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace engine {

// ---------------------------------------------------------------------------
// A single "Print in Order" sub-problem: one solution object plus the sequence
// of tokens its callbacks actually emitted, so we can check ordering.
// ---------------------------------------------------------------------------
template <class Solution>
struct Instance {
    Solution foo;
    std::array<int, 3> seq{0, 0, 0};
    std::atomic<int> pos{0};

    void call(int role) {
        // The callback records *the order in which prints happened*. The slot
        // index comes from a shared counter, so a correct solution yields
        // {1,2,3} and a broken one yields something else.
        auto record = [this, role] {
            seq[pos.fetch_add(1, std::memory_order_relaxed)] = role;
        };
        switch (role) {
            case 1: foo.first(record); break;
            case 2: foo.second(record); break;
            case 3: foo.third(record); break;
        }
    }

    bool ordered() const { return seq == std::array<int, 3>{1, 2, 3}; }
};

// ---------------------------------------------------------------------------
// A test case describes a workload shape, not a solution.
// ---------------------------------------------------------------------------
struct TestCase {
    std::string name;
    int instances;            // how many Foo problems run concurrently
    int repetitions;          // how many times the whole batch is replayed
    std::array<int, 3> order; // the order threads are *launched* in (1,2,3 = roles)
};

// ---------------------------------------------------------------------------
// Run one batch: spin up `instances * 3` threads, all live at once, launched in
// the configured role order, then join. Launching e.g. {3,2,1} means every
// "third" and "second" thread is created (and, for a spinlock, already busy
// waiting) before any "first" thread exists — the worst case for busy-waiting.
// ---------------------------------------------------------------------------
template <class Solution>
bool runBatch(const TestCase& tc) {
    std::vector<std::unique_ptr<Instance<Solution>>> insts;
    insts.reserve(tc.instances);
    for (int i = 0; i < tc.instances; ++i)
        insts.push_back(std::make_unique<Instance<Solution>>());

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(tc.instances) * 3);

    for (int role : tc.order)
        for (auto& in : insts) {
            Instance<Solution>* p = in.get();
            threads.emplace_back([p, role] { p->call(role); });
        }

    for (auto& t : threads) t.join();

    for (auto& in : insts)
        if (!in->ordered()) return false;
    return true;
}

// Run the full workload (all repetitions). Returns true iff every batch was
// correctly ordered.
template <class Solution>
bool runWorkload(const TestCase& tc) {
    bool ok = true;
    for (int r = 0; r < tc.repetitions; ++r)
        ok &= runBatch<Solution>(tc);
    return ok;
}

// ---------------------------------------------------------------------------
// Metrics helpers
// ---------------------------------------------------------------------------

// Peak resident set size in KiB since process start (0 if unsupported).
inline long peakRSSKiB() {
#if defined(__unix__) || defined(__APPLE__)
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return ru.ru_maxrss / 1024; // macOS reports bytes
#else
    return ru.ru_maxrss;        // Linux reports KiB
#endif
#else
    return 0;
#endif
}

struct Timing {
    double median_ms;
    double min_ms;
    double max_ms;
};

// Time a workload over `trials` runs and return median/min/max in milliseconds.
template <class Solution>
Timing timeWorkload(const TestCase& tc, int trials) {
    std::vector<double> samples;
    samples.reserve(trials);
    for (int t = 0; t < trials; ++t) {
        auto t0 = std::chrono::steady_clock::now();
        volatile bool ok = runWorkload<Solution>(tc);
        (void)ok;
        auto t1 = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(samples.begin(), samples.end());
    return Timing{samples[samples.size() / 2], samples.front(), samples.back()};
}

} // namespace engine
