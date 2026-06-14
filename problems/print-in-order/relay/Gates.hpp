// relay/Gates.hpp
//
// A "gate" is the bare waiting primitive at the heart of each solution: wait
// until a shared counter reaches a target, then advance it. These mirror the
// five Foo solutions one-to-one — FooSpin uses the same loop as SpinGate, FooCV
// the same mutex+cv as CVGate, and so on — but stripped of the first/second/
// third wrapper so the relay benchmark can hand a baton off thousands of times
// and measure pure handoff latency.
//
// (The Foo classes are kept separate and intact so they remain copy-pasteable
// LeetCode answers; the gates exist only for the relay sweep.)

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define PIO_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define PIO_CPU_RELAX() asm volatile("yield" ::: "memory")
#else
#define PIO_CPU_RELAX() ((void)0)
#endif

namespace pio {

// Naked busy-wait — 100% CPU while waiting.
struct SpinGate {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        while (stage.load(std::memory_order_acquire) != v) {}
    }
    void advance_to(int v) noexcept { stage.store(v, std::memory_order_release); }
};

// Busy-wait + PAUSE hint — still 100% CPU, relaxes the pipeline / cache line.
struct SpinGatePause {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        while (stage.load(std::memory_order_acquire) != v) PIO_CPU_RELAX();
    }
    void advance_to(int v) noexcept { stage.store(v, std::memory_order_release); }
};

// Busy-wait + yield — gives the core back to the scheduler.
struct SpinGateYield {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        while (stage.load(std::memory_order_acquire) != v) std::this_thread::yield();
    }
    void advance_to(int v) noexcept { stage.store(v, std::memory_order_release); }
};

// Condition variable — parks on a futex, zero CPU while waiting.
struct CVGate {
    std::mutex m;
    std::condition_variable cv;
    int stage = 0;
    void wait_for(int v) {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return stage == v; });
    }
    void advance_to(int v) {
        {
            std::lock_guard<std::mutex> lk(m);
            stage = v;
        }
        cv.notify_all();
    }
};

// std::atomic::wait — lock-free fast path, parks (futex) when it must wait.
struct AtomicWaitGate {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        for (int s = stage.load(std::memory_order_acquire); s != v;
             s = stage.load(std::memory_order_acquire))
            stage.wait(s, std::memory_order_acquire);
    }
    void advance_to(int v) noexcept {
        stage.store(v, std::memory_order_release);
        stage.notify_all();
    }
};

} // namespace pio
