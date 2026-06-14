// shootout/Scenarios.hpp
//
// One benchmark: N threads repeatedly access a shared counter for a fixed time
// window. Each operation is a read (with probability `read`%) or a write; a read
// takes the shared lock, a write takes the exclusive lock. We report the two
// metrics that matter: throughput (Mops/s) and per-operation latency (ns).
//
// Two knobs cover every regime:
//   read = 0                -> write-contended counter (mutual exclusion)
//   read high, cs > 0       -> read-mostly with a real read section (where a
//                              reader-writer lock can let readers run in parallel)
//
// The lock-free atomic is special-cased: a write is fetch_add, a read is load.
// It only guards a single word, so it has no `cs` critical section.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <latch>
#include <numeric>
#include <thread>
#include <vector>

#include "shootout/Metrics.hpp"
#include "shootout/Primitives.hpp"

namespace shootout {

struct Params {
    int threads = 4;
    int read = 0;         // % of operations that are reads
    long cs = 0;          // work inside the critical section (cpuBurn iterations)
    double seconds = 0.3; // measured window per trial
    int trials = 5;
};

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
template <class Body>
Result measure(const Params& p, Body&& body) {
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
                std::uint64_t acc = body(seed, stop, local);
                counts[t] = local;
                g_sink.fetch_xor(acc, std::memory_order_relaxed);
            });
        c0 = cpuMillis();
        t0 = detail::clock::now();
        start.count_down();
        std::this_thread::sleep_for(std::chrono::duration<double>(p.seconds));
        stop.store(true, std::memory_order_relaxed);
    } // join
    const double cpu = cpuMillis() - c0;
    const double wall = detail::ms(detail::clock::now() - t0);

    Result r;
    r.wall_ms = wall;
    r.cpu_ms = cpu;
    r.cores = wall > 0 ? cpu / wall : 0;
    r.ops = static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0LL));
    r.throughput_Mops = r.ops / (wall * 1000.0);
    r.ns_per_op = r.ops > 0 ? (wall * 1e6 * p.threads) / r.ops : 0;
    return r;
}
} // namespace detail

// Lock-based access (works for any Lock; only SharedMutex actually shares).
template <Lock L>
Result accessOnce(const Params& p) {
    L lock;
    std::uint64_t shared = 0;
    Result r = detail::measure(p, [&](std::uint64_t seed, std::atomic<bool>& stop, long long& local) {
        std::uint64_t acc = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            seed = detail::xorshift(seed);
            if (static_cast<int>(seed % 100) < p.read) {
                lock.lock_shared();
                acc += shared;
                if (p.cs) acc = cpuBurn(acc, p.cs);
                lock.unlock_shared();
            } else {
                lock.lock();
                ++shared;
                if (p.cs) shared += cpuBurn(shared, p.cs);
                lock.unlock();
            }
            ++local;
        }
        return acc;
    });
    r.bytes = sizeof(L);
    return r;
}

// Lock-free access: write = fetch_add, read = load. Single word only (no cs).
inline Result atomicAccessOnce(const Params& p) {
    std::atomic<std::uint64_t> shared{0};
    Result r = detail::measure(p, [&](std::uint64_t seed, std::atomic<bool>& stop, long long& local) {
        std::uint64_t acc = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            seed = detail::xorshift(seed);
            if (static_cast<int>(seed % 100) < p.read)
                acc += shared.load(std::memory_order_relaxed);
            else
                shared.fetch_add(1, std::memory_order_relaxed);
            ++local;
        }
        return acc;
    });
    r.bytes = sizeof(std::atomic<std::uint64_t>);
    return r;
}

} // namespace shootout
