// shootout/Mutexes.hpp
//
// The mutual-exclusion family, all behind one interface: lock() / unlock().
// Each is a different point on the "how do I wait for the lock" spectrum, and
// each shines in a different scenario:
//
//   TASLock        naked test-and-set spin    — simplest; cache-line storms under contention
//   TTASLock       test-and-test-and-set spin — spins on a read; far less bus traffic
//   TTASBackoff    TTAS + pause + backoff     — best uncontended-ish spin
//   TicketLock     FIFO spin                  — fair; no starvation, but bounces one line
//   MCSLock        queue spin                 — scalable + fair; each spins on its own line
//   StdMutex       std::mutex                 — parks (futex); wins when oversubscribed / long CS

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "shootout/Metrics.hpp"

namespace shootout {

template <class T>
concept Mutex = requires(T t) {
    { t.lock() };
    { t.unlock() };
};

// --- Test-and-set: every waiter hammers an atomic RMW on the same line. ------
struct TASLock {
    std::atomic<bool> flag{false};
    void lock() noexcept {
        while (flag.exchange(true, std::memory_order_acquire)) {}
    }
    void unlock() noexcept { flag.store(false, std::memory_order_release); }
};

// --- Test-and-test-and-set: spin on a cheap shared read, RMW only on release. -
struct TTASLock {
    std::atomic<bool> flag{false};
    void lock() noexcept {
        for (;;) {
            while (flag.load(std::memory_order_relaxed)) SHOOTOUT_CPU_RELAX();
            if (!flag.exchange(true, std::memory_order_acquire)) return;
        }
    }
    void unlock() noexcept { flag.store(false, std::memory_order_release); }
};

// --- TTAS with exponential backoff: cuts contention storms further. ----------
struct TTASBackoff {
    std::atomic<bool> flag{false};
    void lock() noexcept {
        int backoff = 1;
        for (;;) {
            while (flag.load(std::memory_order_relaxed)) {
                for (int i = 0; i < backoff; ++i) SHOOTOUT_CPU_RELAX();
                if (backoff < 1024) backoff <<= 1;
            }
            if (!flag.exchange(true, std::memory_order_acquire)) return;
            backoff = 1;
        }
    }
    void unlock() noexcept { flag.store(false, std::memory_order_release); }
};

// --- Ticket lock: take a number, spin until it's served. Strict FIFO. --------
struct TicketLock {
    std::atomic<std::uint32_t> next{0};
    std::atomic<std::uint32_t> serving{0};
    void lock() noexcept {
        const std::uint32_t my = next.fetch_add(1, std::memory_order_relaxed);
        while (serving.load(std::memory_order_acquire) != my) SHOOTOUT_CPU_RELAX();
    }
    void unlock() noexcept {
        serving.store(serving.load(std::memory_order_relaxed) + 1,
                      std::memory_order_release);
    }
};

// --- MCS lock: a queue of waiters, each spinning on its OWN cache line. -------
// Scalable (no shared spin line) and fair (FIFO). Uses a thread_local node, so
// it assumes a thread holds at most one MCS lock at a time — true for every
// scenario here.
struct MCSLock {
    struct Node {
        std::atomic<Node*> next{nullptr};
        std::atomic<bool> waiting{false};
    };
    std::atomic<Node*> tail{nullptr};

    static Node* myNode() noexcept {
        thread_local Node n;
        return &n;
    }

    void lock() noexcept {
        Node* n = myNode();
        n->next.store(nullptr, std::memory_order_relaxed);
        n->waiting.store(true, std::memory_order_relaxed);
        Node* pred = tail.exchange(n, std::memory_order_acq_rel);
        if (pred) {
            pred->next.store(n, std::memory_order_release);
            while (n->waiting.load(std::memory_order_acquire)) SHOOTOUT_CPU_RELAX();
        }
    }
    void unlock() noexcept {
        Node* n = myNode();
        Node* succ = n->next.load(std::memory_order_acquire);
        if (!succ) {
            Node* expected = n;
            if (tail.compare_exchange_strong(expected, nullptr,
                                             std::memory_order_acq_rel))
                return;
            while (!(succ = n->next.load(std::memory_order_acquire)))
                SHOOTOUT_CPU_RELAX();
        }
        succ->waiting.store(false, std::memory_order_release);
    }
};

// --- The blocking baseline: parks on a futex instead of spinning. ------------
struct StdMutex {
    std::mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
};

} // namespace shootout
