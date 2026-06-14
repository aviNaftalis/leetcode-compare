// runner/runner.cpp
//
// The "test engine" entry point: a single source file compiled ONCE PER
// SOLUTION into its own optimized binary (CMake passes SOLUTION_HEADER /
// SOLUTION_TYPE / SOLUTION_NAME). Each binary therefore contains exactly one
// solution, fully inlined under -O3 -march=native, and is run as a separate
// process by scripts/compare.sh.
//
// Separate processes make the memory number meaningful (peak RSS is
// per-process) and stop one solution's thread/cache state from contaminating
// another's timing.
//
// Usage: runner_<solution> [trivial|cpu|sleep|fastpath]   (default: trivial)
// Output: one human line and one CSV line the compare script parses.

#include <print>
#include <string_view>

#include SOLUTION_HEADER
#include "engine/Engine.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using Sol = SOLUTION_TYPE;

namespace {
engine::TestCase pick(std::string_view name) {
    if (name == "cpu") return pio::testcases::contentionCpu;
    if (name == "sleep") return pio::testcases::contentionSleep;
    if (name == "fastpath") return pio::testcases::fastPath;
    return pio::testcases::contention;
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view which = argc > 1 ? argv[1] : "trivial";
    const engine::TestCase tc = pick(which);
    constexpr int kTrials = 11;

    if (!engine::runWorkload<Sol>(tc)) { // never time a broken solution
        std::println(stderr, "{}: ORDERING VIOLATION", SOLUTION_NAME);
        return 1;
    }

    const engine::Timing t = engine::timeWorkload<Sol>(tc, kTrials);
    const long rss = engine::peakRSSKiB();
    const auto objsz = sizeof(Sol);

    std::println(
        "{:<20}  sizeof={:>3}B  median={:>9.2f} ms  (min {:>8.2f}, max {:>8.2f})  peakRSS={} KiB",
        std::string_view{SOLUTION_NAME}, objsz, t.median_ms, t.min_ms, t.max_ms, rss);
    std::println("CSV,{},{},{},{:.3f},{:.3f},{:.3f},{}", tc.name, SOLUTION_NAME,
                 objsz, t.median_ms, t.min_ms, t.max_ms, rss);
    return 0;
}
