// src/shootout.cpp
//
// Runs one (primitive, configuration) point and prints a CSV row. The metrics
// are throughput (Mops/s) and per-op latency (ns).
//
// Usage:
//   shootout --primitive ticket --threads 8 --read 0  --cs 0     # write-contended
//   shootout --primitive shared_mutex --threads 8 --read 95 --cs 1000  # read-mostly
//
// Primitives: cv  std_mutex  shared_mutex  ticket  atomic

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
    try { return std::stod(std::string(s)); } catch (...) { return fb; }
}

using Runner = std::function<Result(const Params&)>;
const std::map<std::string, Runner> kPrimitives = {
    {"cv", accessOnce<CVLock>},
    {"std_mutex", accessOnce<StdMutex>},
    {"shared_mutex", accessOnce<SharedMutex>},
    {"ticket", accessOnce<TicketLock>},
    {"atomic", [](const Params& p) { return atomicAccessOnce(p); }},
};

} // namespace

int main(int argc, char** argv) {
    std::string primitive = "std_mutex";
    Params p;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string_view k = argv[i], v = argv[i + 1];
        if (k == "--primitive") primitive = v;
        else if (k == "--threads") p.threads = static_cast<int>(toLong(v, p.threads));
        else if (k == "--read") p.read = static_cast<int>(toLong(v, p.read));
        else if (k == "--cs") p.cs = toLong(v, p.cs);
        else if (k == "--secs") p.seconds = toDouble(v, p.seconds);
        else if (k == "--trials") p.trials = static_cast<int>(toLong(v, p.trials));
    }

    auto it = kPrimitives.find(primitive);
    if (it == kPrimitives.end()) {
        std::println(stderr, "unknown primitive: {}", primitive);
        return 2;
    }

    // Median by throughput across trials, so latency stays consistent with it.
    std::vector<Result> rs;
    for (int i = 0; i < p.trials; ++i) rs.push_back(it->second(p));
    const Result r = medianByThroughput(std::move(rs));

    std::println(stderr, "{:<13} thr={:<3} read={:<3} cs={:<5} | {:>8.2f} Mops  {:>8.1f} ns/op  {}B",
                 primitive, p.threads, p.read, p.cs, r.throughput_Mops, r.ns_per_op, r.bytes);
    // CSV: primitive,threads,read,cs,throughput_Mops,ns_per_op,bytes
    std::println("CSV,{},{},{},{},{:.6f},{:.4f},{}", primitive, p.threads, p.read, p.cs,
                 r.throughput_Mops, r.ns_per_op, r.bytes);
    return 0;
}
