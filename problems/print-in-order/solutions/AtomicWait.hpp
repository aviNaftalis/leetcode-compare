// solutions/AtomicWait.hpp
//
// The modern best-of-both: C++20 std::atomic<int>::wait / notify_all. The fast
// path is a single lock-free atomic load (like the spinlock), but when a thread
// has to wait it calls atomic::wait(), which parks on a futex (like the
// condition variable) instead of burning the core.
//
// So it keeps the spinlock's tiny 4-byte object AND the condition variable's
// "consume zero CPU while waiting" behaviour — no mutex required. This is the
// solution that shows the spinlock's slowness was never about the lock-free
// design, only about *busy*-waiting.

#pragma once

#include <atomic>
#include <functional>

namespace pio {

class FooAtomicWait {
public:
    FooAtomicWait() = default;

    void first(std::function<void()> printFirst) {
        printFirst();
        stage_.store(1, std::memory_order_release);
        stage_.notify_all();
    }

    void second(std::function<void()> printSecond) {
        for (int s = stage_.load(std::memory_order_acquire); s != 1;
             s = stage_.load(std::memory_order_acquire)) {
            stage_.wait(s, std::memory_order_acquire); // parks until value changes
        }
        printSecond();
        stage_.store(2, std::memory_order_release);
        stage_.notify_all();
    }

    void third(std::function<void()> printThird) {
        for (int s = stage_.load(std::memory_order_acquire); s != 2;
             s = stage_.load(std::memory_order_acquire)) {
            stage_.wait(s, std::memory_order_acquire);
        }
        printThird();
    }

private:
    std::atomic<int> stage_{0};
};

} // namespace pio
