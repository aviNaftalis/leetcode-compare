// src/shootout.cpp
//
// One binary, runtime dispatch. Picks a scenario + primitive, runs `trials`
// timed runs, prints the median run as a CSV row the sweep script collects.
//
// Usage:
//   shootout --scenario contended --primitive ttas --threads 8 --cs 0 --secs 0.3
//   shootout --scenario readheavy --primitive shared_mutex --threads 8 --write 5
//   shootout --scenario signaling --primitive cv --threads 1 --rounds 50000
//
// Scenario / primitive menu:
//   contended : tas ttas ttas_backoff ticket mcs std_mutex atomic
//   readheavy : shared_mutex excl_ttas excl_std
//   signaling : spin spin_yield cv atomic_wait

#include <charconv>
#include <functional>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "shootout/Scenarios.hpp"

using namespace shootout;

namespace {

long toLong(std::string_view s, long fb) {
    long v{};
    auto [pp, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} ? v : fb;
}
double toDouble(std::string_view s, double fb) {
    try {
        return std::stod(std::string(s));
    } catch (...) {
        return fb;
    }
}

Result median(const std::function<Result()>& once, int trials) {
    std::vector<Result> rs;
    rs.reserve(trials);
    for (int i = 0; i < trials; ++i) rs.push_back(once());
    return medianByThroughput(std::move(rs));
}

// scenario -> (primitive -> runnable)
using Runner = std::function<Result(const Params&)>;

const std::map<std::string, std::map<std::string, Runner>> kMenu = {
    {"contended",
     {
         {"tas", contendedOnce<TASLock>},
         {"ttas", contendedOnce<TTASLock>},
         {"ttas_backoff", contendedOnce<TTASBackoff>},
         {"ticket", contendedOnce<TicketLock>},
         {"mcs", contendedOnce<MCSLock>},
         {"std_mutex", contendedOnce<StdMutex>},
         {"atomic", [](const Params& p) { return atomicCounterOnce(p); }},
     }},
    {"readheavy",
     {
         {"shared_mutex", readHeavyOnce<StdSharedMutex>},
         {"excl_ttas", readHeavyOnce<ExclusiveAsShared<TTASLock>>},
         {"excl_std", readHeavyOnce<ExclusiveAsShared<StdMutex>>},
     }},
    {"signaling",
     {
         {"spin", signalingOnce<SpinGate>},
         {"spin_yield", signalingOnce<SpinGateYield>},
         {"cv", signalingOnce<CVGate>},
         {"atomic_wait", signalingOnce<AtomicWaitGate>},
     }},
};

} // namespace

int main(int argc, char** argv) {
    std::string scenario = "contended", primitive = "ttas";
    Params p;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view k = argv[i], v = argv[i + 1];
        if (k == "--scenario") scenario = v;
        else if (k == "--primitive") primitive = v;
        else if (k == "--threads") p.threads = static_cast<int>(toLong(v, p.threads));
        else if (k == "--cs") p.cs = toLong(v, p.cs);
        else if (k == "--secs") p.seconds = toDouble(v, p.seconds);
        else if (k == "--write") p.write_pct = static_cast<int>(toLong(v, p.write_pct));
        else if (k == "--rounds") p.rounds = static_cast<int>(toLong(v, p.rounds));
        else if (k == "--trials") p.trials = static_cast<int>(toLong(v, p.trials));
    }

    auto sit = kMenu.find(scenario);
    if (sit == kMenu.end()) {
        std::println(stderr, "unknown scenario: {}", scenario);
        return 2;
    }
    auto pit = sit->second.find(primitive);
    if (pit == sit->second.end()) {
        std::println(stderr, "unknown primitive '{}' for scenario '{}'", primitive, scenario);
        return 2;
    }

    const Runner& run = pit->second;
    const Result r = median([&] { return run(p); }, p.trials);

    std::println(stderr,
                 "{:<12} {:<13} thr={:<3} cs={:<5} | {:>8.2f} Mops  {:>6.1f} ns/op  "
                 "{:>5.2f} cores  cov={:.2f}  {}B",
                 scenario, primitive, p.threads, p.cs, r.throughput_Mops, r.ns_per_op,
                 r.cores, r.fairness_cov, r.bytes);
    // CSV: scenario,primitive,threads,cs,write,wall_ms,cpu_ms,cores,throughput_Mops,ns_per_op,fairness_cov,bytes
    std::println("CSV,{},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.6f},{:.4f},{:.5f},{}",
                 scenario, primitive, p.threads, p.cs, p.write_pct, r.wall_ms, r.cpu_ms,
                 r.cores, r.throughput_Mops, r.ns_per_op, r.fairness_cov, r.bytes);
    return 0;
}
