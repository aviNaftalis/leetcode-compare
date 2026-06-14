// engine/Engine.hpp
//
// The test engine. It is deliberately solution-agnostic: given any type that
// satisfies the PrintInOrder concept it drives a configurable workload, checks
// the ordering held, and reports the metrics we care about (wall time + peak
// RSS + object size).
//
// Shared by three front-ends, so each compiles the chosen solution inline and
// -O3 can fully inline the synchronization primitives:
//   * test/correctness.cpp  -> Catch2 assertions that the ordering holds
//   * bench/benchmark.cpp   -> Catch2 statistical micro-benchmark
//   * runner/runner.cpp      -> a standalone, per-solution optimized binary
//
// Modern bits: C++23, std::jthread (RAII join), a concept to constrain
// solutions, std::latch for a simultaneous start, std::ranges algorithms.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <latch>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "engine/Work.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace engine {

// A valid solution exposes the three LeetCode entry points, each taking the
// print callback by std::function<void()> (the original signature).
template <class T>
concept PrintInOrder = requires(T t, std::function<void()> cb) {
    { t.first(cb) };
    { t.second(cb) };
    { t.third(cb) };
};

// ---------------------------------------------------------------------------
// A test case describes a workload shape, never a solution.
// ---------------------------------------------------------------------------
struct TestCase {
    std::string_view name;
    int instances = 1;                  // concurrent Foo problems
    int repetitions = 1;                // times the whole batch is replayed
    std::array<int, 3> order{1, 2, 3};  // order threads are *launched* in
    Work work{};                        // payload each action runs
};

// ---------------------------------------------------------------------------
// One "Print in Order" sub-problem: the solution object, the sequence of tokens
// its callbacks actually emitted (to verify ordering), and a sink so the work
// payload cannot be optimized away.
// ---------------------------------------------------------------------------
template <PrintInOrder Solution>
struct Instance {
    Solution foo;
    std::array<int, 3> seq{0, 0, 0};
    std::atomic<int> pos{0};
    std::atomic<std::uint64_t> sink{0};

    void call(int role, const Work& work) {
        auto action = [this, role, &work] {
            sink.fetch_xor(run(work), std::memory_order_relaxed);
            seq[pos.fetch_add(1, std::memory_order_relaxed)] = role;
        };
        switch (role) {
        case 1: foo.first(action); break;
        case 2: foo.second(action); break;
        case 3: foo.third(action); break;
        }
    }

    [[nodiscard]] bool ordered() const noexcept {
        return seq == std::array<int, 3>{1, 2, 3};
    }
};

// ---------------------------------------------------------------------------
// Run one batch: spin up `instances * 3` threads, all live at once, gate them
// on a latch so they start together, then release. Launching in role order
// {3,2,1} means every "third"/"second" thread is already waiting before any
// "first" exists — the worst case for busy-waiting.
// ---------------------------------------------------------------------------
template <PrintInOrder Solution>
bool runBatch(const TestCase& tc) {
    auto insts =
        std::views::iota(0, tc.instances) | std::views::transform([](int) {
            return std::make_unique<Instance<Solution>>();
        }) |
        std::ranges::to<std::vector>();

    std::latch start{1};
    {
        std::vector<std::jthread> threads;
        threads.reserve(static_cast<std::size_t>(tc.instances) * 3);
        for (int role : tc.order)
            for (auto& in : insts)
                threads.emplace_back([&start, p = in.get(), role, &tc] {
                    start.wait();
                    p->call(role, tc.work);
                });
        start.count_down(); // release the storm
    } // jthreads join here

    return std::ranges::all_of(insts, [](const auto& in) { return in->ordered(); });
}

template <PrintInOrder Solution>
bool runWorkload(const TestCase& tc) {
    bool ok = true;
    for ([[maybe_unused]] int r : std::views::iota(0, tc.repetitions))
        ok = runBatch<Solution>(tc) && ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

// Peak resident set size in KiB since process start (0 if unsupported).
[[nodiscard]] inline long peakRSSKiB() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
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
    double median_ms = 0;
    double min_ms = 0;
    double max_ms = 0;
};

template <PrintInOrder Solution>
[[nodiscard]] Timing timeWorkload(const TestCase& tc, int trials) {
    using clock = std::chrono::steady_clock;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(trials));
    for ([[maybe_unused]] int t : std::views::iota(0, trials)) {
        const auto t0 = clock::now();
        const bool ok = runWorkload<Solution>(tc);
        const auto t1 = clock::now();
        if (!ok) return {}; // caller treats all-zero as failure
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::ranges::sort(samples);
    return {samples[samples.size() / 2], samples.front(), samples.back()};
}

// ---------------------------------------------------------------------------
// Relay mode
// ---------------------------------------------------------------------------
// The one-shot batch above creates threads per problem, so at low concurrency
// thread-creation cost swamps the synchronization cost. The relay mode instead
// keeps a small set of long-lived threads passing a baton through many stages,
// so pure *handoff latency* dominates. This is the regime where a spinlock can
// win: a hot, non-oversubscribed handoff avoids the futex syscall a parking
// primitive pays on every wakeup.
//
// A "gate" is the underlying waiting primitive each solution is built on
// (wait until the counter reaches a target, then advance it). Each lane owns one
// gate and 3 threads; the threads relay a single counter 0..3*rounds, so every
// stage hands off to the next with no barrier. `lanes` controls oversubscription
// (3*lanes threads vs the core count).
template <class G>
concept SequentialGate = requires(G g, int v) {
    { g.wait_for(v) };
    { g.advance_to(v) };
};

struct RelayCase {
    std::string_view name;
    int lanes = 1;        // parallel relays; 3*lanes threads => oversubscription
    int rounds = 100'000; // full first->second->third cycles per lane
    Work work{};
};

template <SequentialGate Gate>
void runRelay(const RelayCase& rc) {
    const int stages = 3 * rc.rounds;
    auto gates =
        std::views::iota(0, rc.lanes) | std::views::transform([](int) {
            return std::make_unique<Gate>();
        }) |
        std::ranges::to<std::vector>();
    std::atomic<std::uint64_t> sink{0};
    std::latch start{1};
    {
        std::vector<std::jthread> threads;
        threads.reserve(static_cast<std::size_t>(rc.lanes) * 3);
        for (auto& g : gates)
            for (int role : {0, 1, 2})
                threads.emplace_back([&, gate = g.get(), role] {
                    start.wait();
                    std::uint64_t acc = 0;
                    for (int target = role; target < stages; target += 3) {
                        gate->wait_for(target);
                        acc ^= run(rc.work);
                        gate->advance_to(target + 1);
                    }
                    sink.fetch_xor(acc, std::memory_order_relaxed);
                });
        start.count_down();
    } // jthreads join here
}

template <SequentialGate Gate>
[[nodiscard]] Timing timeRelay(const RelayCase& rc, int trials) {
    using clock = std::chrono::steady_clock;
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(trials));
    for ([[maybe_unused]] int t : std::views::iota(0, trials)) {
        const auto t0 = clock::now();
        runRelay<Gate>(rc);
        const auto t1 = clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::ranges::sort(samples);
    return {samples[samples.size() / 2], samples.front(), samples.back()};
}

} // namespace engine
