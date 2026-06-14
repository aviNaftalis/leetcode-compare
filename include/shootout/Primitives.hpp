// shootout/Primitives.hpp
//
// Five ways to coordinate access to shared data, all behind one interface so the
// same benchmark can drive each: exclusive lock()/unlock() for writers, and
// lock_shared()/unlock_shared() for readers (only the reader-writer lock shares;
// the rest fall back to exclusive). The lock-free atomic is special-cased in the
// scenario — it isn't a lock, it's the bar the locks are measured against.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "shootout/Metrics.hpp"

namespace shootout {

template <class T>
concept Lock = requires(T t) {
    { t.lock() };
    { t.unlock() };
    { t.lock_shared() };
    { t.unlock_shared() };
};

// 1. condition_variable — a blocking lock hand-built from a mutex + CV. This is
//    the textbook way to wait for a condition; as a lock it shows what you get
//    if you roll your own instead of using std::mutex.
struct CVLock {
    std::mutex m;
    std::condition_variable cv;
    bool held = false;
    void lock() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return !held; });
        held = true;
    }
    void unlock() {
        {
            std::lock_guard<std::mutex> lk(m);
            held = false;
        }
        cv.notify_one();
    }
    void lock_shared() { lock(); }
    void unlock_shared() { unlock(); }
};

// 2. std::mutex — the standard blocking lock; spins briefly then parks on a futex.
struct StdMutex {
    std::mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    void lock_shared() { m.lock(); }
    void unlock_shared() { m.unlock(); }
};

// 3. std::shared_mutex — a reader-writer lock: many readers OR one writer.
struct SharedMutex {
    std::shared_mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    void lock_shared() { m.lock_shared(); }
    void unlock_shared() { m.unlock_shared(); }
};

// 4. ticket lock — a fair spinlock: take a number, busy-wait until it's served.
//    Strict FIFO, so nobody starves. (Spins, so it wants a free core.)
struct TicketLock {
    std::atomic<std::uint32_t> next{0};
    std::atomic<std::uint32_t> serving{0};
    void lock() {
        const std::uint32_t my = next.fetch_add(1, std::memory_order_relaxed);
        while (serving.load(std::memory_order_acquire) != my) SHOOTOUT_CPU_RELAX();
    }
    void unlock() {
        serving.store(serving.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }
    void lock_shared() { lock(); }
    void unlock_shared() { unlock(); }
};

// 5. lock-free atomic — no lock at all: a write is fetch_add, a read is load.
//    The fastest option, but it only works for a single word (see Scenarios.hpp).

} // namespace shootout
