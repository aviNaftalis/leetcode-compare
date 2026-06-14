// solutions/Spinlock.hpp
//
// The previous submission: a naked busy-wait on an atomic. A thread that
// arrives early loops on an atomic load, doing nothing useful but never
// releasing the CPU. The object is tiny (one atomic int) and the fast path has
// no syscall — but every waiting thread pins a core at 100%.
//
// Under oversubscription that is exactly the problem: the spinning thread is
// burning the very core that the thread it is waiting for needs in order to
// make progress, so the wait gets *longer*, not shorter. This is the slow
// solution the comparison is built to explain. See ../README.md.

#pragma once

#include <atomic>
#include <functional>

namespace pio {

class FooSpin {
public:
    FooSpin() = default;

    void first(std::function<void()> printFirst) {
        printFirst();
        stage_.store(1, std::memory_order_release);
    }

    void second(std::function<void()> printSecond) {
        while (stage_.load(std::memory_order_acquire) != 1) {
            // naked spin: no pause, no yield
        }
        printSecond();
        stage_.store(2, std::memory_order_release);
    }

    void third(std::function<void()> printThird) {
        while (stage_.load(std::memory_order_acquire) != 2) {
            // naked spin
        }
        printThird();
    }

private:
    std::atomic<int> stage_{0};
};

} // namespace pio
