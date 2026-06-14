// runner/runner.cpp
//
// The "test engine" entry point: a single source file that is compiled ONCE PER
// SOLUTION into its own optimized binary (CMake passes SOLUTION_HEADER /
// SOLUTION_TYPE / SOLUTION_NAME). Each binary therefore contains exactly one
// solution, fully inlined under -O3 -march=native, and is run as a separate
// process by scripts/compare.sh.
//
// Running each solution in its own process is what makes the memory number
// meaningful (peak RSS is per-process) and stops one solution's thread/cache
// state from contaminating another's timing.
//
// Output: one human-readable line and one CSV line ("CSV,<name>,<sizeof>,
// <median_ms>,<min_ms>,<max_ms>,<peak_rss_kib>") that the compare script parses.

#include <cstdio>

#include SOLUTION_HEADER
#include "engine/Engine.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using Sol = SOLUTION_TYPE;

int main() {
    const auto tc = pio::testcases::contention();
    constexpr int kTrials = 11;

    // Correctness guard — never report timings for a broken solution.
    if (!engine::runWorkload<Sol>(tc)) {
        std::fprintf(stderr, "%s: ORDERING VIOLATION\n", SOLUTION_NAME);
        return 1;
    }

    const engine::Timing t = engine::timeWorkload<Sol>(tc, kTrials);
    const long rss = engine::peakRSSKiB();
    const std::size_t objsz = sizeof(Sol);

    std::printf("%-22s  sizeof=%3zuB  median=%8.2f ms  (min %8.2f, max %8.2f)  peakRSS=%ld KiB\n",
                SOLUTION_NAME, objsz, t.median_ms, t.min_ms, t.max_ms, rss);
    std::printf("CSV,%s,%zu,%.3f,%.3f,%.3f,%ld\n",
                SOLUTION_NAME, objsz, t.median_ms, t.min_ms, t.max_ms, rss);
    return 0;
}
