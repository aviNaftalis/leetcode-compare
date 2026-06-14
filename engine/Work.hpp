// engine/Work.hpp
//
// The "payload" a thread runs in place of LeetCode's trivial print(). Making
// the action heavy is what turns this into a realistic comparison: the longer a
// thread holds the baton, the longer everyone behind it waits — and *how* they
// wait (spin vs park) is the whole point of the study.
//
// Two kinds model the two regimes that matter:
//   * Cpu   — a tight, un-optimizable compute loop. The worker needs a core;
//             a spinning waiter is fighting it for that core.
//   * Sleep — std::this_thread::sleep_for, i.e. a stand-in for blocking I/O.
//             The worker holds the baton while off-CPU, so a spinning waiter
//             burns a core for the *entire* I/O duration, doing nothing.

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace engine {

enum class WorkKind : std::uint8_t { None, Cpu, Sleep };

struct Work {
    WorkKind kind = WorkKind::None;
    std::uint64_t magnitude = 0; // Cpu: loop iterations. Sleep: microseconds.

    [[nodiscard]] static constexpr Work none() noexcept { return {}; }
    [[nodiscard]] static constexpr Work cpu(std::uint64_t iters) noexcept {
        return {WorkKind::Cpu, iters};
    }
    [[nodiscard]] static constexpr Work sleep(std::uint64_t micros) noexcept {
        return {WorkKind::Sleep, micros};
    }
};

// Runs the payload and returns a value derived from it. Callers must sink the
// result (e.g. xor into an atomic) so the optimizer cannot delete the loop.
[[nodiscard]] inline std::uint64_t run(const Work& w) noexcept {
    switch (w.kind) {
    case WorkKind::None:
        return 0;
    case WorkKind::Cpu: {
        // 64-bit LCG: data-dependent, so -O3 can't fold or hoist it.
        std::uint64_t acc = 0x9E3779B97F4A7C15ULL;
        for (std::uint64_t i = 0; i < w.magnitude; ++i)
            acc = acc * 6364136223846793005ULL + 1442695040888963407ULL;
        return acc;
    }
    case WorkKind::Sleep:
        std::this_thread::sleep_for(std::chrono::microseconds(w.magnitude));
        return 0;
    }
    return 0;
}

} // namespace engine
