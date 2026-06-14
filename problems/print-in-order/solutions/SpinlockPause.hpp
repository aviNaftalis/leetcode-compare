// solutions/SpinlockPause.hpp
//
// A diagnostic variant: same busy-wait as Spinlock, but with a CPU "pause"
// hint (PAUSE on x86, YIELD on ARM) inside the loop. The pause does NOT release
// the core to the scheduler — the thread is still 100% busy — but it relaxes
// the pipeline, cuts speculative-load pressure on the shared cache line, and
// saves power.
//
// Comparing this against the naked Spinlock isolates how much of the slowdown
// is raw cache-coherence / pipeline contention versus CPU starvation. If pause
// barely helps but yield helps a lot, the dominant cost is starvation, not
// memory traffic. See ../README.md.

#pragma once

#include <atomic>
#include <functional>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define PIO_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define PIO_CPU_RELAX() asm volatile("yield" ::: "memory")
#else
#define PIO_CPU_RELAX() ((void)0)
#endif

namespace pio {

class FooSpinPause {
public:
    FooSpinPause() = default;

    void first(std::function<void()> printFirst) {
        printFirst();
        stage_.store(1, std::memory_order_release);
    }

    void second(std::function<void()> printSecond) {
        while (stage_.load(std::memory_order_acquire) != 1) {
            PIO_CPU_RELAX();
        }
        printSecond();
        stage_.store(2, std::memory_order_release);
    }

    void third(std::function<void()> printThird) {
        while (stage_.load(std::memory_order_acquire) != 2) {
            PIO_CPU_RELAX();
        }
        printThird();
    }

private:
    std::atomic<int> stage_{0};
};

} // namespace pio
