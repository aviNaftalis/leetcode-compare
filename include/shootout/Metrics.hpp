// shootout/Metrics.hpp
//
// Cost measurement shared by every scenario. We report a primitive along three
// independent axes — latency/throughput, CPU, and memory — because a method can
// win one and lose another (a spinlock can match latency while burning far more
// CPU). All timing is wall-clock; CPU is user+system time across all threads via
// getrusage; memory is the object's size and process peak RSS.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define SHOOTOUT_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define SHOOTOUT_CPU_RELAX() asm volatile("yield" ::: "memory")
#else
#define SHOOTOUT_CPU_RELAX() ((void)0)
#endif

namespace shootout {

// Total CPU time (all threads, user+system) charged to the process so far, ms.
// getrusage(RUSAGE_SELF) aggregates every thread, including joined ones, so a
// delta across a run captures the CPU its workers burned.
[[nodiscard]] inline double cpuMillis() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
    auto ms = [](timeval v) { return v.tv_sec * 1000.0 + v.tv_usec / 1000.0; };
    return ms(ru.ru_utime) + ms(ru.ru_stime);
#else
    return 0.0;
#endif
}

[[nodiscard]] inline long peakRSSKiB() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return ru.ru_maxrss / 1024;
#else
    return ru.ru_maxrss;
#endif
#else
    return 0;
#endif
}

// A cheap, un-optimizable bit of CPU work, used as a "critical section length"
// or "payload" knob. Returns the mutated seed so callers can sink it.
[[nodiscard]] inline std::uint64_t cpuBurn(std::uint64_t seed, long iters) noexcept {
    for (long i = 0; i < iters; ++i)
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return seed;
}

// One measured run's outcome. Scenarios fill what's meaningful for them.
struct Result {
    double wall_ms = 0;       // wall time of the measured region
    double cpu_ms = 0;        // CPU time (all threads) during the region
    double cores = 0;         // cpu_ms / wall_ms = avg cores kept busy
    double ops = 0;           // operations completed (lock acquisitions, etc.)
    double throughput_Mops = 0;
    double ns_per_op = 0;     // wall_ns * threads / ops (per-op latency under load)
    double fairness_cov = 0;  // stddev/mean of per-thread op counts (0 = perfectly fair)
    std::size_t bytes = 0;    // sizeof the primitive
};

// Pick the median run by throughput so all reported fields stay self-consistent.
[[nodiscard]] inline Result medianByThroughput(std::vector<Result> trials) {
    std::ranges::sort(trials, {}, &Result::throughput_Mops);
    return trials[trials.size() / 2];
}

// Coefficient of variation of per-thread counts: a fairness metric.
[[nodiscard]] inline double coeffVar(const std::vector<long long>& counts) {
    if (counts.empty()) return 0;
    double mean = 0;
    for (auto c : counts) mean += static_cast<double>(c);
    mean /= static_cast<double>(counts.size());
    if (mean == 0) return 0;
    double var = 0;
    for (auto c : counts) var += (c - mean) * (c - mean);
    var /= static_cast<double>(counts.size());
    return (var > 0 ? __builtin_sqrt(var) : 0) / mean;
}

} // namespace shootout
