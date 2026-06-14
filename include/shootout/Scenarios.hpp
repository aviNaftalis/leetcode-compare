// shootout/Scenarios.hpp
//
// The workloads. Each is time-bounded (run for `seconds`, count what happened)
// so it yields throughput, CPU cost, and — where it makes sense — fairness, all
// from one run. Scenarios are templated on the primitive so -O3 inlines it.
//
//   contended    N threads hammer one lock around a counter. The canonical lock
//                benchmark: sweep threads (contention/oversubscription) and `cs`
//                (critical-section length). threads=1 gives uncontended overhead.
//   atomicCounter the same workload with NO lock (atomic fetch_add) — the
//                lock-free alternative, the bar every lock is trying to reach.
//   readHeavy    mostly readers, few writers — where a reader-writer lock wins.
//   signaling    producer/consumer baton hand-off — where parking primitives win.

#pragma once

#include <chrono>
#include <cstdint>
#include <latch>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

#include "shootout/Gates.hpp"
#include "shootout/Metrics.hpp"
#include "shootout/Mutexes.hpp"
#include "shootout/RWLocks.hpp"

namespace shootout {

struct Params {
    int threads = 4;
    long cs = 0;          // critical-section / payload work (cpuBurn iterations)
    double seconds = 0.3; // measured duration per trial
    int write_pct = 5;    // readHeavy: % of ops that are writes
    int rounds = 50'000;  // signaling: hand-offs per lane
    int trials = 5;
};

// Sink so the optimizer can't delete the work.
inline std::atomic<std::uint64_t> g_sink{0};

namespace detail {
using clock = std::chrono::steady_clock;
inline double ms(clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}
inline std::uint64_t xorshift(std::uint64_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}
} // namespace detail

// --- Contended counter (mutual exclusion) -----------------------------------
template <Mutex M>
Result contendedOnce(const Params& p) {
    M lock;
    alignas(64) std::uint64_t counter = 0;
    std::atomic<bool> stop{false};
    std::vector<long long> counts(p.threads, 0);
    std::latch start{1};
    double c0 = 0;
    detail::clock::time_point t0;
    {
        std::vector<std::jthread> ws;
        ws.reserve(p.threads);
        for (int t = 0; t < p.threads; ++t)
            ws.emplace_back([&, t] {
                start.wait();
                std::uint64_t seed = 0x9e3779b9ULL * (t + 1) + 1;
                long long local = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    lock.lock();
                    ++counter;
                    if (p.cs) seed = cpuBurn(seed, p.cs);
                    lock.unlock();
                    ++local;
                }
                counts[t] = local;
                g_sink.fetch_xor(seed, std::memory_order_relaxed);
            });
        c0 = cpuMillis();
        t0 = detail::clock::now();
        start.count_down();
        std::this_thread::sleep_for(std::chrono::duration<double>(p.seconds));
        stop.store(true, std::memory_order_relaxed);
    } // join
    const double cpu = cpuMillis() - c0;
    const double wall = detail::ms(detail::clock::now() - t0);
    g_sink.fetch_xor(counter, std::memory_order_relaxed);

    Result r;
    r.wall_ms = wall;
    r.cpu_ms = cpu;
    r.cores = wall > 0 ? cpu / wall : 0;
    r.ops = static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0LL));
    r.throughput_Mops = r.ops / (wall * 1000.0);
    r.ns_per_op = r.ops > 0 ? (wall * 1e6 * p.threads) / r.ops : 0;
    r.fairness_cov = coeffVar(counts);
    r.bytes = sizeof(M);
    return r;
}

// --- Lock-free counter (no mutual exclusion at all) -------------------------
inline Result atomicCounterOnce(const Params& p) {
    std::atomic<std::uint64_t> counter{0};
    std::atomic<bool> stop{false};
    std::vector<long long> counts(p.threads, 0);
    std::latch start{1};
    double c0 = 0;
    detail::clock::time_point t0;
    {
        std::vector<std::jthread> ws;
        ws.reserve(p.threads);
        for (int t = 0; t < p.threads; ++t)
            ws.emplace_back([&, t] {
                start.wait();
                long long local = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    counter.fetch_add(1, std::memory_order_relaxed);
                    ++local;
                }
                counts[t] = local;
            });
        c0 = cpuMillis();
        t0 = detail::clock::now();
        start.count_down();
        std::this_thread::sleep_for(std::chrono::duration<double>(p.seconds));
        stop.store(true, std::memory_order_relaxed);
    }
    const double cpu = cpuMillis() - c0;
    const double wall = detail::ms(detail::clock::now() - t0);
    g_sink.fetch_xor(counter.load(), std::memory_order_relaxed);

    Result r;
    r.wall_ms = wall;
    r.cpu_ms = cpu;
    r.cores = wall > 0 ? cpu / wall : 0;
    r.ops = static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0LL));
    r.throughput_Mops = r.ops / (wall * 1000.0);
    r.ns_per_op = r.ops > 0 ? (wall * 1e6 * p.threads) / r.ops : 0;
    r.fairness_cov = coeffVar(counts);
    r.bytes = sizeof(std::atomic<std::uint64_t>);
    return r;
}

// --- Read-heavy (reader-writer) ---------------------------------------------
template <SharedMutex SL>
Result readHeavyOnce(const Params& p) {
    SL lock;
    std::uint64_t shared = 0;
    std::atomic<bool> stop{false};
    std::vector<long long> counts(p.threads, 0);
    std::latch start{1};
    double c0 = 0;
    detail::clock::time_point t0;
    {
        std::vector<std::jthread> ws;
        ws.reserve(p.threads);
        for (int t = 0; t < p.threads; ++t)
            ws.emplace_back([&, t] {
                start.wait();
                std::uint64_t seed = 0x9e3779b9ULL * (t + 1) + 1;
                std::uint64_t acc = 0;
                long long local = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    seed = detail::xorshift(seed);
                    if (seed % 100 < static_cast<unsigned>(p.write_pct)) {
                        lock.lock();
                        ++shared;
                        if (p.cs) shared += cpuBurn(shared, p.cs);
                        lock.unlock();
                    } else {
                        lock.lock_shared();
                        acc += shared;
                        if (p.cs) acc = cpuBurn(acc, p.cs);
                        lock.unlock_shared();
                    }
                    ++local;
                }
                counts[t] = local;
                g_sink.fetch_xor(acc, std::memory_order_relaxed);
            });
        c0 = cpuMillis();
        t0 = detail::clock::now();
        start.count_down();
        std::this_thread::sleep_for(std::chrono::duration<double>(p.seconds));
        stop.store(true, std::memory_order_relaxed);
    }
    const double cpu = cpuMillis() - c0;
    const double wall = detail::ms(detail::clock::now() - t0);

    Result r;
    r.wall_ms = wall;
    r.cpu_ms = cpu;
    r.cores = wall > 0 ? cpu / wall : 0;
    r.ops = static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0LL));
    r.throughput_Mops = r.ops / (wall * 1000.0);
    r.ns_per_op = r.ops > 0 ? (wall * 1e6 * p.threads) / r.ops : 0;
    r.fairness_cov = coeffVar(counts);
    r.bytes = sizeof(SL);
    return r;
}

// --- Signaling / hand-off (event waiting, not mutual exclusion) -------------
// `threads` lanes, each a 2-thread ping-pong relaying a baton `rounds` times.
template <Gate G>
Result signalingOnce(const Params& p) {
    const int lanes = p.threads;
    const int stages = 2 * p.rounds;
    std::vector<std::unique_ptr<G>> gates;
    gates.reserve(lanes);
    for (int i = 0; i < lanes; ++i) gates.push_back(std::make_unique<G>());
    std::latch start{1};
    double c0 = 0;
    detail::clock::time_point t0;
    {
        std::vector<std::jthread> ws;
        ws.reserve(static_cast<std::size_t>(lanes) * 2);
        for (auto& g : gates)
            for (int role : {0, 1})
                ws.emplace_back([&, gate = g.get(), role] {
                    start.wait();
                    std::uint64_t s = 0;
                    for (int target = role; target < stages; target += 2) {
                        gate->wait_for(target);
                        if (p.cs) s = cpuBurn(s, p.cs);
                        gate->advance_to(target + 1);
                    }
                    g_sink.fetch_xor(s, std::memory_order_relaxed);
                });
        c0 = cpuMillis();
        t0 = detail::clock::now();
        start.count_down();
    } // join
    const double cpu = cpuMillis() - c0;
    const double wall = detail::ms(detail::clock::now() - t0);

    Result r;
    r.wall_ms = wall;
    r.cpu_ms = cpu;
    r.cores = wall > 0 ? cpu / wall : 0;
    r.ops = static_cast<double>(stages) * lanes; // total hand-offs
    r.throughput_Mops = r.ops / (wall * 1000.0);
    r.ns_per_op = wall * 1e6 / stages; // per-lane hand-off latency
    r.bytes = sizeof(G);
    return r;
}

} // namespace shootout
