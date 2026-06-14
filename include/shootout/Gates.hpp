// shootout/Gates.hpp
//
// Signaling primitives for the producer/consumer-style handoff scenario, behind
// one interface: wait_for(v) blocks until a shared counter reaches v; advance_to(v)
// publishes the new value (and wakes waiters, for the parking variants).
//
// This is a different *use case* from mutual exclusion: a thread waits for an
// event rather than for a lock. It's where parking primitives earn their keep —
// a waiter that sleeps costs ~0 CPU, while a spinner burns a core per wait.
//
//   SpinGate        busy-loop                — lowest latency when a core is free
//   SpinGateYield   busy-loop + yield()      — gives the core back
//   CVGate          mutex + condition_variable — parks; cheap CPU, slow wakeup
//   AtomicWaitGate  std::atomic::wait        — lock-free + parks; tiny object

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "shootout/Metrics.hpp"

namespace shootout {

template <class T>
concept Gate = requires(T t, int v) {
    { t.wait_for(v) };
    { t.advance_to(v) };
};

struct SpinGate {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        while (stage.load(std::memory_order_acquire) != v) SHOOTOUT_CPU_RELAX();
    }
    void advance_to(int v) noexcept { stage.store(v, std::memory_order_release); }
};

struct SpinGateYield {
    std::atomic<int> stage{0};
    void wait_for(int v) noexcept {
        while (stage.load(std::memory_order_acquire) != v) std::this_thread::yield();
    }
    void advance_to(int v) noexcept { stage.store(v, std::memory_order_release); }
};

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

} // namespace shootout
