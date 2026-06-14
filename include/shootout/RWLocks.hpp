// shootout/RWLocks.hpp
//
// Reader-writer primitives for the read-heavy scenario, behind one interface:
// lock()/unlock() for writers, lock_shared()/unlock_shared() for readers.
//
//   StdSharedMutex    std::shared_mutex — real RW; many readers run concurrently
//   ExclusiveAsShared adapter that maps a reader lock onto a plain Mutex — i.e.
//                     "what you get if you used an ordinary mutex for reads too"
//
// The point of the scenario: when readers vastly outnumber writers, the real RW
// lock lets readers proceed in parallel while the exclusive baseline serializes
// them. The adapter lets any Mutex above stand in as the exclusive baseline.

#pragma once

#include <shared_mutex>

#include "shootout/Mutexes.hpp"

namespace shootout {

template <class T>
concept SharedMutex = requires(T t) {
    { t.lock() };
    { t.unlock() };
    { t.lock_shared() };
    { t.unlock_shared() };
};

struct StdSharedMutex {
    std::shared_mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    void lock_shared() { m.lock_shared(); }
    void unlock_shared() { m.unlock_shared(); }
};

// Treat every reader as a writer — the cost of NOT having a reader-writer lock.
template <Mutex M>
struct ExclusiveAsShared {
    M m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
    void lock_shared() { m.lock(); }
    void unlock_shared() { m.unlock(); }
};

} // namespace shootout
