// runner/runner.cpp
//
// The "test engine" entry point: compiled ONCE PER SOLUTION into its own
// optimized binary (CMake passes SOLUTION_TYPE = the Foo class and
// SOLUTION_GATE = its relay gate). Each binary contains exactly one solution,
// fully inlined under -O3 -march=native, and is run as a separate process by
// the sweep / compare scripts.
//
// Two modes:
//   --mode oneshot : create 3*x threads, each runs the protocol once (x =
//                    concurrent instances => oversubscription). Heavy thread
//                    churn; this is where parking wins.
//   --mode relay   : x long-lived lanes (3*x threads) relay a baton through
//                    many rounds; pure handoff latency. This is where a hot,
//                    non-oversubscribed spinlock wins.
//
// Emits one CSV line the sweep script parses:
//   CSV,<mode>,<solution>,<x>,<work>,<mag>,<median_ms>,<min_ms>,<max_ms>,<sizeof>,<rss_kib>

#include <charconv>
#include <print>
#include <string>
#include <string_view>

#include SOLUTION_HEADER
#include "engine/Engine.hpp"
#include "problems/print-in-order/relay/Gates.hpp"
#include "problems/print-in-order/testcases/TestCases.hpp"

using Sol = SOLUTION_TYPE;   // the Foo class
using Gate = SOLUTION_GATE;  // its relay gate

namespace {

long toLong(std::string_view s, long fallback) {
    long v{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} ? v : fallback;
}

engine::Work makeWork(std::string_view kind, std::uint64_t mag) {
    if (kind == "cpu") return engine::Work::cpu(mag);
    if (kind == "sleep") return engine::Work::sleep(mag);
    return engine::Work::none();
}

} // namespace

int main(int argc, char** argv) {
    // Defaults
    std::string mode = "oneshot";
    long x = 32;          // instances (oneshot) or lanes (relay)
    long rounds = 50'000; // relay
    long reps = 12;       // oneshot
    std::string work = "none";
    long mag = 0;
    long trials = 7;
    std::array<int, 3> order{3, 2, 1};

    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view k = argv[i], v = argv[i + 1];
        if (k == "--mode") mode = v;
        else if (k == "--x") x = toLong(v, x);
        else if (k == "--rounds") rounds = toLong(v, rounds);
        else if (k == "--reps") reps = toLong(v, reps);
        else if (k == "--work") work = v;
        else if (k == "--mag") mag = toLong(v, mag);
        else if (k == "--trials") trials = toLong(v, trials);
        else if (k == "--order") order = (v == "123") ? std::array{1, 2, 3}
                                                       : std::array{3, 2, 1};
    }

    const engine::Work w = makeWork(work, static_cast<std::uint64_t>(mag));
    engine::Timing t{};
    std::size_t objsz = 0;

    if (mode == "relay") {
        const engine::RelayCase rc{.name = "relay",
                                   .lanes = static_cast<int>(x),
                                   .rounds = static_cast<int>(rounds),
                                   .work = w};
        t = engine::timeRelay<Gate>(rc, static_cast<int>(trials));
        objsz = sizeof(Gate);
    } else {
        const engine::TestCase tc{.name = "oneshot",
                                  .instances = static_cast<int>(x),
                                  .repetitions = static_cast<int>(reps),
                                  .order = order,
                                  .work = w};
        if (!engine::runWorkload<Sol>(tc)) { // never time a broken solution
            std::println(stderr, "{}: ORDERING VIOLATION", SOLUTION_NAME);
            return 1;
        }
        t = engine::timeWorkload<Sol>(tc, static_cast<int>(trials));
        objsz = sizeof(Sol);
    }

    const long rss = engine::peakRSSKiB();
    std::println(stderr,
                 "{:<18} mode={:<7} x={:<3} work={}/{:<6} median={:>9.2f} ms  sizeof={}B",
                 std::string_view{SOLUTION_NAME}, mode, x, work, mag, t.median_ms, objsz);
    std::println("CSV,{},{},{},{},{},{:.4f},{:.4f},{:.4f},{},{}", mode, SOLUTION_NAME,
                 x, work, mag, t.median_ms, t.min_ms, t.max_ms, objsz, rss);
    return 0;
}
